"""실시간 자막용 로컬 STT(faster-whisper). POST /stt/partial 이 쓴다.

- 말하는 도중 UE가 '지금까지 녹음된 WAV'를 0.5~0.7초마다 보내면 받아 적은 글자만 돌려준다.
  실제 명령 판별은 지금처럼 /command/voice(Gemini)가 한다 — 이건 "내 목소리가 들어가고 있나" 확인용 자막.
- 모델은 서버 시작 때 백그라운드 스레드에서 읽는다. 읽는 중·실패·꺼짐이면 text=None(UE는 무시).
- 받아 적기는 전용 스레드 풀(1개)에서 돌려 /command 요청을 막지 않는다.
- 테스트는 실제 모델 없이 set_transcriber()로 가짜를 꽂는다.
"""
from __future__ import annotations

import asyncio
import io
import logging
import os
import threading
import time
import wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any, Callable, Optional, Protocol

log = logging.getLogger("jemini.stt")

SAMPLE_RATE = 16000

# 게임 단어로 인식을 기울인다(제미니 기능 이름·호칭)
# 2026-11-09: 단어 목록("회복, 힐, 보호막, ...")은 작은 소리+잡음에서 목록을 그대로 반복해 지어냈다 → 실제 명령 문장 형태로.
INITIAL_PROMPT = ("제미니, 재장전해 줘. 공격력 올려 줘. 힐 해 줘. 보호막 쳐 줘. 단말기 해킹해 줘. "
                  "적 공격해. 어그로 끌어 줘. 오빠 빨리 살려 줘.")
# 자막 한 번에 나올 최대 토큰(8초 명령이면 충분, 반복 환각이 길게 늘어나는 것을 막는다)
MAX_NEW_TOKENS = 32

# 이 RMS(float -1~1) 미만이면 무음으로 보고 받아 적지 않는다(무음에서 프롬프트를 지어내는 환각 방지).
# 2026-11-09 실측: 블루투스 헤드셋 실제 말소리가 RMS 0.003~0.005(peak 0.07)로 0.004 게이트에 걸려 자막이 안 나왔다 → 낮춤.
SILENCE_RMS = 0.0015
# 작은 마이크 소리는 받아 적기 전에 최대 진폭이 이 값이 되도록 키운다(최대 MAX_GAIN배)
TARGET_PEAK = 0.7
MAX_GAIN = 20.0


class Transcriber(Protocol):
    def __call__(self, samples: Any) -> str: ...


def model_folder(model_dir: Path, model: str) -> Path:
    """모델 폴더(예: models/faster-whisper-small). 경로를 직접 주면 그대로."""
    candidate = Path(model)
    if candidate.is_absolute() or candidate.exists():
        return candidate
    return model_dir / f"faster-whisper-{model}"


def has_model(folder: Path) -> bool:
    return (folder / "model.bin").is_file() and (folder / "config.json").is_file()


def download(model_dir: Path, model: str) -> Path:
    """Hugging Face(Systran/faster-whisper-<model>)에서 model_dir/faster-whisper-<model>로 내려받는다."""
    from faster_whisper.utils import download_model

    folder = model_folder(model_dir, model)
    folder.mkdir(parents=True, exist_ok=True)
    download_model(model, output_dir=str(folder))
    return folder


def decode_wav(data: bytes) -> tuple[Any, float]:
    """WAV(16bit PCM) → 16kHz 모노 float32 numpy 배열, 길이(초). 형식이 다르면 ValueError."""
    import numpy as np

    try:
        with wave.open(io.BytesIO(data), "rb") as w:
            channels, width, rate, frames = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
            raw = w.readframes(frames)
    except (wave.Error, EOFError) as e:
        raise ValueError(f"audio must be a WAV file: {e}") from e
    if rate <= 0 or channels <= 0:
        raise ValueError("invalid WAV header")
    if width != 2:
        raise ValueError("WAV must be 16-bit PCM")
    pcm = np.frombuffer(raw[: len(raw) - len(raw) % (2 * channels)], dtype="<i2").astype(np.float32) / 32768.0
    if channels > 1:
        pcm = pcm.reshape(-1, channels).mean(axis=1)
    seconds = len(pcm) / rate
    if rate != SAMPLE_RATE and len(pcm) > 1:
        # 선형 보간 리샘플(자막용이라 충분)
        out_len = max(1, int(len(pcm) * SAMPLE_RATE / rate))
        pcm = np.interp(np.linspace(0, len(pcm) - 1, out_len), np.arange(len(pcm)), pcm).astype(np.float32)
    return pcm, seconds


# Whisper가 잡음에서 자주 지어내는 문장(유튜브 자막 학습 흔적) — 이것만 나오면 버린다
HALLUCINATIONS = {"고맙습니다", "감사합니다", "시청해주셔서감사합니다", "시청해주셔서고맙습니다",
                  "구독과좋아요부탁드립니다", "MBC뉴스", "MBC뉴스이덕영입니다", "자막제공배달의민족", "다음영상에서만나요"}


def _bare(text: str) -> str:
    return "".join(ch for ch in text if ch not in " ,.!?~·…")


def _clean(text: str) -> Optional[str]:
    text = " ".join(text.split()).strip()
    # 말끝 마침표는 자막에 필요 없다(물음표·느낌표는 둔다)
    text = text.rstrip(".。").strip()
    if not text:
        return None
    bare = _bare(text)
    if not bare or bare in HALLUCINATIONS:
        return None
    # 프롬프트를 그대로 읊는 환각(무음·잡음에서 가끔). 프롬프트가 명령 문장이라 명령 하나("공격력올려줘" 6자)는
    # 진짜 말일 수 있다 → 문장 두 개 이상 길이(12자+)를 이어 읊을 때만 버린다.
    if len(bare) >= 12 and bare in _bare(INITIAL_PROMPT):
        return None
    # 같은 구절 반복("공격력, 공격력", "힐, 보호막, 힐, 보호막")은 한 번만
    items = [p.strip() for p in text.split(",") if p.strip()]
    if len(items) > 1:
        unique: list[str] = []
        for item in items:
            if item not in unique:
                unique.append(item)
        if len(unique) < len(items):
            text = ", ".join(unique)
    return text


class SttEngine:
    """모델 상태: disabled / idle(아직 안 읽음) / loading / ready / error."""

    def __init__(self) -> None:
        self.state = "idle"
        self.error: Optional[str] = None
        self.load_ms: Optional[int] = None
        self._transcriber: Optional[Transcriber] = None
        self._lock = threading.Lock()
        self._pool = ThreadPoolExecutor(max_workers=1, thread_name_prefix="stt")
        self._queued = 0

    # ---------------------------------------------------------------- 모델

    def set_transcriber(self, fn: Optional[Transcriber], state: str = "ready") -> None:
        """테스트용: 가짜 받아 적기 함수를 꽂는다(None이면 상태만)."""
        with self._lock:
            self._transcriber = fn
            self.state = state
            self.error = None

    def start_loading(self, *, enabled: bool, model: str, model_dir: Path, cpu_threads: int,
                      auto_download: bool = True) -> None:
        """백그라운드 스레드에서 모델을 읽는다(한 번만). 꺼져 있으면 disabled."""
        with self._lock:
            if not enabled:
                self.state = "disabled"
                return
            if self.state != "idle":
                return
            self.state = "loading"
        threading.Thread(target=self._load, name="stt-load", daemon=True,
                         args=(model, model_dir, cpu_threads, auto_download)).start()

    def _load(self, model: str, model_dir: Path, cpu_threads: int, auto_download: bool) -> None:
        start = time.perf_counter()
        try:
            folder = model_folder(model_dir, model)
            if not has_model(folder):
                if not auto_download:
                    raise FileNotFoundError(f"STT model not found: {folder} (run tools/download_stt_model.py)")
                log.warning("STT: downloading faster-whisper %s into %s (first run only)", model, folder)
                folder = download(model_dir, model)
            from faster_whisper import WhisperModel

            whisper = WhisperModel(str(folder), device="cpu", compute_type="int8", cpu_threads=cpu_threads)
            fn = _make_transcriber(whisper)
            # 첫 호출 준비(내부 초기화) — 0.5초 무음
            import numpy as np
            fn(np.zeros(SAMPLE_RATE // 2, dtype=np.float32), warmup=True)
            with self._lock:
                self._transcriber = fn
                self.state = "ready"
                self.load_ms = round((time.perf_counter() - start) * 1000)
            log.warning("STT: faster-whisper %s ready in %d ms (%s)", model, self.load_ms, folder)
        except Exception as e:  # noqa: BLE001 - STT는 있으면 좋은 기능, 실패해도 서버는 계속
            with self._lock:
                self.state = "error"
                self.error = f"{type(e).__name__}: {e}"
            log.warning("STT: unavailable (%s)", self.error)

    # ---------------------------------------------------------------- 받아 적기

    async def transcribe(self, samples: Any) -> tuple[Optional[str], str]:
        """(글자 또는 None, 상태). 준비 안 됐거나 이미 2개 밀려 있으면 바로 None."""
        with self._lock:
            fn = self._transcriber
            if self.state != "ready" or fn is None:
                return None, self.state
            if self._queued >= 2:
                return None, "busy"
            self._queued += 1
        try:
            loop = asyncio.get_running_loop()
            text = await loop.run_in_executor(self._pool, fn, samples)
            return _clean(text or ""), "ready"
        except Exception as e:  # noqa: BLE001
            log.warning("STT: transcribe failed (%s: %s)", type(e).__name__, e)
            return None, "error"
        finally:
            with self._lock:
                self._queued -= 1


# 마지막 받아 적기의 구간별 결과(진단 로그용 — logs/stt.jsonl). 단일 스레드 풀이라 경쟁 없음.
last_segments: list[dict[str, Any]] = []


def _make_transcriber(whisper: Any) -> Callable[..., str]:
    import numpy as np

    def transcribe(samples: Any, warmup: bool = False) -> str:
        if not warmup:
            if len(samples) == 0 or float(np.sqrt(np.mean(np.square(samples)))) < SILENCE_RMS:
                last_segments.clear()
                return ""
            peak = float(np.max(np.abs(samples)))
            if 0.0 < peak < TARGET_PEAK:
                samples = (samples * min(TARGET_PEAK / peak, MAX_GAIN)).astype(np.float32)
        segments, _info = whisper.transcribe(
            samples, language="ko", task="transcribe", beam_size=1, best_of=1, temperature=0.0,
            initial_prompt=INITIAL_PROMPT, condition_on_previous_text=False, without_timestamps=True,
            vad_filter=False, max_new_tokens=MAX_NEW_TOKENS)
        parts = []
        debug = []
        for seg in segments:
            # 말이 아닌 구간(높은 no_speech + 낮은 확신)은 버린다.
            # 실측: 잡음 환각 nsp 0.98 / lp -0.93, 짧은 실제 말(0.9초 "공격력") nsp 0.87 / lp -0.16
            dropped = ((seg.no_speech_prob > 0.6 and seg.avg_logprob < -1.0) or (seg.no_speech_prob > 0.9 and seg.avg_logprob < -0.5)
                       or getattr(seg, "compression_ratio", 0.0) > 2.2)
            debug.append({"text": seg.text, "nsp": round(seg.no_speech_prob, 2), "lp": round(seg.avg_logprob, 2), "dropped": dropped})
            if dropped:
                continue
            parts.append(seg.text)
        last_segments.clear()
        last_segments.extend(debug)
        return "".join(parts)

    return transcribe


def default_cpu_threads() -> int:
    # 게임이 같은 PC에서 돈다 → 코어 절반, 최대 4
    return max(1, min(4, (os.cpu_count() or 2) // 2))


engine = SttEngine()
