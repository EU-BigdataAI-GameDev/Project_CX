"""환경변수 설정. 코드에 값을 하드코딩하지 않는다(.env 또는 실제 환경변수)."""
from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from pathlib import Path

from dotenv import load_dotenv

# PyInstaller로 묶은 jemini-server.exe면: .env·logs는 exe 옆, prompts는 묶인 리소스(_MEIPASS)
if getattr(sys, "frozen", False):
    ROOT = Path(sys.executable).resolve().parent
    RESOURCE_ROOT = Path(getattr(sys, "_MEIPASS", ROOT))
else:
    ROOT = Path(__file__).resolve().parent.parent
    RESOURCE_ROOT = ROOT
# 프로젝트 .env가 시스템 환경변수보다 우선(예전 키가 시스템에 남아 있으면 그게 쓰이는 문제).
# 저장소 안에서 만든 exe(Jemini/dist/jemini-server/)는 옆에 .env가 없으면 Jemini/.env를 쓴다.
for _env in (ROOT / ".env", ROOT.parent.parent / ".env"):
    if _env.is_file():
        load_dotenv(_env, override=True)
        break


def _bool(name: str, default: bool) -> bool:
    raw = os.getenv(name)
    if raw is None or raw.strip() == "":
        return default
    return raw.strip().lower() in ("1", "true", "yes", "on")


def _float(name: str, default: float) -> float:
    raw = os.getenv(name)
    return float(raw) if raw not in (None, "") else default


def _int(name: str, default: int) -> int:
    raw = os.getenv(name)
    return int(raw) if raw not in (None, "") else default


@dataclass(frozen=True, repr=False)
class Settings:
    gemini_api_key: str
    gemini_model: str
    fallback_models: tuple[str, ...]
    max_output_tokens: int
    mock_mode: bool
    api_token: str
    llm_timeout_sec: float
    voice_timeout_sec: float
    temperature: float
    cost_reload: int
    cost_hack_terminal: int
    cost_hack_enemy: int
    cost_pulse_attack: int
    cost_damage_boost: int
    cost_heal: int
    cost_decoy: int
    cost_quick_reboot: int
    cost_shield: int
    max_audio_sec: float
    max_audio_bytes: int
    log_path: Path
    # 모든 모델 한도 초과 시 텍스트 요청을 mock 키워드 판별로 처리(시연 안전장치)
    mock_on_rate_limit: bool = True
    # 실시간 자막(POST /stt/partial, 로컬 faster-whisper). 끄면 항상 text=null.
    stt_enabled: bool = True
    stt_model: str = "small"
    # 모델 폴더(models/faster-whisper-<model>). exe면 exe 옆 models/
    stt_model_dir: Path = ROOT / "models"
    stt_cpu_threads: int = 0
    # 모델이 없으면 첫 실행 때 Hugging Face에서 내려받기
    stt_auto_download: bool = True
    # 기능 B. WARDEN 설득(POST /persuade): 세션별 시도 간격(초), 판정 temperature(낮게 — 흔들림 줄이기)
    persuade_cooldown_sec: float = 3.0
    persuade_temperature: float = 0.1
    # 모델 폴백 전체 예산(초). 실제 예산 = min(요청 타임아웃 × 2, 이 값). UE 요청 타임아웃(12초)보다 짧게. 0이면 × 2만.
    llm_total_timeout_sec: float = 11.0

    def __repr__(self) -> str:
        # API 키·토큰이 테스트 실패 출력·로그에 찍히지 않도록 가린다
        fields = []
        for name in self.__dataclass_fields__:
            value = getattr(self, name)
            if name in ("gemini_api_key", "api_token"):
                value = "***" if value else ""
            fields.append(f"{name}={value!r}")
        return f"Settings({', '.join(fields)})"

    __str__ = __repr__


def load_settings() -> Settings:
    log_path = Path(os.getenv("LOG_PATH", "logs/jemini.jsonl"))
    if not log_path.is_absolute():
        log_path = ROOT / log_path
    api_key = os.getenv("GEMINI_API_KEY", "")
    stt_model_dir = Path(os.getenv("STT_MODEL_DIR", "") or "models")
    if not stt_model_dir.is_absolute():
        stt_model_dir = ROOT / stt_model_dir
    return Settings(
        gemini_api_key=api_key,
        # 버전 고정(별칭 gemini-flash-latest는 가리키는 모델이 바뀔 수 있음)
        gemini_model=os.getenv("GEMINI_MODEL", "gemini-3.1-flash-lite"),
        # 기본 모델이 한도 초과(429)·과부하(5xx)·타임아웃이면 순서대로 시도. 쉼표로 구분, 비우면 예비 없음.
        fallback_models=tuple(m.strip() for m in os.getenv("GEMINI_FALLBACK_MODELS", "gemini-3.5-flash-lite").split(",") if m.strip()),
        max_output_tokens=_int("LLM_MAX_OUTPUT_TOKENS", 400),
        # 키가 없으면 자동으로 모의 모드
        mock_mode=_bool("MOCK_MODE", default=not api_key),
        api_token=os.getenv("API_TOKEN", ""),
        llm_timeout_sec=_float("LLM_TIMEOUT_SEC", 5.0),
        voice_timeout_sec=_float("VOICE_TIMEOUT_SEC", 8.0),
        temperature=_float("LLM_TEMPERATURE", 0.4),
        cost_reload=_int("COST_RELOAD", 5),
        cost_hack_terminal=_int("COST_HACK_TERMINAL", 20),
        cost_hack_enemy=_int("COST_HACK_ENEMY", 20),
        cost_pulse_attack=_int("COST_PULSE_ATTACK", 50),
        cost_damage_boost=_int("COST_DAMAGE_BOOST", 10),
        cost_heal=_int("COST_HEAL", 30),
        cost_decoy=_int("COST_DECOY", 15),
        cost_quick_reboot=_int("COST_QUICK_REBOOT", 30),
        cost_shield=_int("COST_SHIELD", 25),
        max_audio_sec=_float("MAX_AUDIO_SEC", 8.0),
        max_audio_bytes=_int("MAX_AUDIO_BYTES", 2_000_000),
        log_path=log_path,
        mock_on_rate_limit=_bool("MOCK_ON_RATE_LIMIT", default=True),
        stt_enabled=_bool("STT_ENABLED", default=True),
        stt_model=os.getenv("STT_MODEL", "small").strip() or "small",
        stt_model_dir=stt_model_dir,
        stt_cpu_threads=_int("STT_CPU_THREADS", 0),
        stt_auto_download=_bool("STT_AUTO_DOWNLOAD", default=True),
        persuade_cooldown_sec=_float("PERSUADE_COOLDOWN_SEC", 3.0),
        persuade_temperature=_float("PERSUADE_TEMPERATURE", 0.1),
        llm_total_timeout_sec=_float("LLM_TOTAL_TIMEOUT_SEC", 11.0),
    )
