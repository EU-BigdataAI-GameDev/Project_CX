"""POST /stt/partial — 실시간 자막(가짜 받아 적기, 실제 모델 없음)."""
import asyncio
import io
import math
import struct
import threading
import time
import wave
from dataclasses import replace

import numpy as np

from app import main, stt
from conftest import make_state, make_wav


def tone_wav(seconds: float, rate: int = 16000, channels: int = 1, amp: float = 0.3) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for i in range(int(seconds * rate)):
            v = int(amp * 32767 * math.sin(2 * math.pi * 440 * i / rate))
            frames += struct.pack("<h", v) * channels
        w.writeframes(bytes(frames))
    return buf.getvalue()


def partial(client, audio, headers=None):
    return client.post("/stt/partial", files={"audio": ("p.wav", audio, "audio/wav")}, headers=headers or {})


def test_partial_returns_text(client, stt_engine):
    seen = {}

    def fake(samples):
        seen["len"] = len(samples)
        return " 공격력 "
    stt_engine.set_transcriber(fake)
    r = partial(client, tone_wav(1.0))
    assert r.status_code == 200
    body = r.json()
    assert body["text"] == "공격력" and body["state"] == "ready" and isinstance(body["ms"], int)
    assert seen["len"] == 16000


def test_partial_resamples_and_downmixes(client, stt_engine):
    seen = {}

    def fake(samples):
        seen["len"] = len(samples)
        return "가"
    stt_engine.set_transcriber(fake)
    assert partial(client, tone_wav(0.5, rate=48000, channels=2)).json()["text"] == "가"
    assert seen["len"] == 8000  # 48kHz 스테레오 0.5초 → 16kHz 모노


def test_partial_disabled_returns_null(client):
    # 설정 STT_ENABLED=false(픽스처 기본) → 첫 요청에서 disabled, 200 + null
    r = partial(client, tone_wav(1.0))
    assert r.status_code == 200 and r.json()["text"] is None and r.json()["state"] == "disabled"


def test_partial_loading_returns_null(client, stt_engine):
    stt_engine.set_transcriber(None, state="loading")
    r = partial(client, tone_wav(1.0)).json()
    assert r["text"] is None and r["state"] == "loading"


def test_partial_error_in_model_returns_null(client, stt_engine):
    def broken(samples):
        raise RuntimeError("boom")
    stt_engine.set_transcriber(broken)
    r = partial(client, tone_wav(1.0))
    assert r.status_code == 200 and r.json()["text"] is None and r.json()["state"] == "error"


def test_partial_empty_text_is_null(client, stt_engine):
    stt_engine.set_transcriber(lambda s: "   ")
    assert partial(client, tone_wav(1.0)).json()["text"] is None


def test_partial_prompt_hallucination_is_null(client, stt_engine):
    stt_engine.set_transcriber(lambda s: "재장전, 해킹, 단말기")
    assert partial(client, tone_wav(1.0)).json()["text"] is None


def test_partial_known_hallucination_is_null(client, stt_engine):
    stt_engine.set_transcriber(lambda s: " 고맙습니다.")
    assert partial(client, tone_wav(1.0)).json()["text"] is None


def test_partial_strips_trailing_period(client, stt_engine):
    stt_engine.set_transcriber(lambda s: " 공격력 올려줘.")
    assert partial(client, tone_wav(1.0)).json()["text"] == "공격력 올려줘"
    stt_engine.set_transcriber(lambda s: "회복해 줄래?")
    assert partial(client, tone_wav(1.0)).json()["text"] == "회복해 줄래?"


def test_partial_too_long_rejected(client, stt_engine):
    stt_engine.set_transcriber(lambda s: "x")
    assert partial(client, make_wav(9.0)).status_code == 422


def test_partial_not_wav_rejected(client, stt_engine):
    stt_engine.set_transcriber(lambda s: "x")
    assert partial(client, b"not a wav file at all").status_code == 422


def test_partial_token_check(client, settings, monkeypatch, stt_engine):
    stt_engine.set_transcriber(lambda s: "회복")
    monkeypatch.setattr(main, "_settings", replace(settings, api_token="secret"))
    assert partial(client, tone_wav(0.5)).status_code == 401
    assert partial(client, tone_wav(0.5), headers={"X-API-Key": "secret"}).json()["text"] == "회복"


def test_partial_ignores_mock_mode(client, settings, monkeypatch, stt_engine):
    stt_engine.set_transcriber(lambda s: "보호막")
    for mock in (True, False):
        monkeypatch.setattr(main, "_settings", replace(settings, mock_mode=mock))
        assert partial(client, tone_wav(0.5)).json()["text"] == "보호막"


def test_partial_does_not_block_command(client, stt_engine):
    """받아 적기가 오래 걸려도 /command는 바로 응답한다(전용 스레드 풀)."""
    release = threading.Event()

    def slow(samples):
        release.wait(5)
        return "느림"
    stt_engine.set_transcriber(slow)
    result = {}
    t = threading.Thread(target=lambda: result.setdefault("r", partial(client, tone_wav(0.5)).json()))
    t.start()
    time.sleep(0.2)
    start = time.perf_counter()
    r = client.post("/command", json={"text": "공격력 올려줘", "state": make_state(battery=50)})
    elapsed = time.perf_counter() - start
    release.set()
    t.join(5)
    assert r.status_code == 200 and elapsed < 1.0
    assert result["r"]["text"] == "느림"


def test_partial_busy_when_queue_full(stt_engine):
    release = threading.Event()

    def slow(samples):
        release.wait(5)
        return "a"
    stt_engine.set_transcriber(slow)

    async def run():
        tasks = [asyncio.create_task(stt_engine.transcribe([0.1])) for _ in range(3)]
        await asyncio.sleep(0.1)
        release.set()
        return await asyncio.gather(*tasks)
    states = [s for _, s in asyncio.run(run())]
    assert states.count("busy") == 1 and states.count("ready") == 2


def test_health_reports_stt_state(client, stt_engine):
    stt_engine.set_transcriber(None, state="loading")
    assert client.get("/health").json()["stt"] == "loading"


def test_silence_gate_skips_model():
    """실제 받아 적기 함수: 무음이면 모델을 부르지 않는다."""
    called = []

    class FakeWhisper:
        def transcribe(self, *a, **k):
            called.append(1)
            return iter(()), None
    fn = stt._make_transcriber(FakeWhisper())
    assert fn(np.zeros(16000, dtype=np.float32)) == ""
    assert not called


def test_segment_filter_drops_non_speech():
    class Seg:
        def __init__(self, text, nsp, lp):
            self.text, self.no_speech_prob, self.avg_logprob = text, nsp, lp

    class FakeWhisper:
        def transcribe(self, samples, **k):
            assert k["language"] == "ko" and k["beam_size"] == 1 and "공격력" in k["initial_prompt"]
            return iter([Seg("공격력", 0.1, -0.3), Seg(" 음악", 0.9, -1.5), Seg(" 고맙습니다", 0.98, -0.93),
                         Seg(" 올려줘", 0.87, -0.16)]), None
    fn = stt._make_transcriber(FakeWhisper())
    assert fn(np.full(16000, 0.1, dtype=np.float32)) == "공격력 올려줘"


def test_model_folder_and_has_model(tmp_path):
    folder = stt.model_folder(tmp_path, "small")
    assert folder == tmp_path / "faster-whisper-small"
    assert not stt.has_model(folder)
    folder.mkdir()
    (folder / "model.bin").write_bytes(b"x")
    (folder / "config.json").write_text("{}")
    assert stt.has_model(folder)


def test_missing_model_without_download_sets_error(tmp_path):
    engine = stt.SttEngine()
    engine.start_loading(enabled=True, model="small", model_dir=tmp_path, cpu_threads=1, auto_download=False)
    for _ in range(100):
        if engine.state != "loading":
            break
        time.sleep(0.05)
    assert engine.state == "error" and "not found" in engine.error
