import io
import json
import struct
import sys
import wave
from dataclasses import replace
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app import main, stt  # noqa: E402
from app.config import load_settings  # noqa: E402


@pytest.fixture
def settings(tmp_path):
    # 비용은 기획 4-3절 값으로 고정(로컬 .env 값과 무관하게)
    return replace(load_settings(), mock_mode=True, api_token="",
                   log_path=tmp_path / "jemini.jsonl",
                   cost_reload=5, cost_hack_terminal=20, cost_hack_enemy=20, cost_pulse_attack=50,
                   cost_damage_boost=10, cost_heal=30, cost_decoy=15, cost_quick_reboot=30,
                   cost_shield=25,
                   # 테스트는 실제 STT 모델을 읽지 않는다(가짜는 stt_engine 픽스처로)
                   stt_enabled=False)


@pytest.fixture(autouse=True)
def stt_engine(monkeypatch):
    """테스트마다 새 STT 엔진(모델 없음). set_transcriber()로 가짜를 꽂는다."""
    engine = stt.SttEngine()
    monkeypatch.setattr(stt, "engine", engine)
    return engine


@pytest.fixture
def client(settings, monkeypatch):
    monkeypatch.setattr(main, "_settings", settings)
    return TestClient(main.app)


def make_state(**kw):
    base = {"brother_ammo": 0, "brother_ammo_max": 30, "brother_reloading": False,
            "brother_hp": 100, "sister_puzzle": None, "battery": 100}
    base.update(kw)
    return base


def make_wav(seconds: float, rate: int = 16000) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(struct.pack("<h", 0) * int(seconds * rate))
    return buf.getvalue()


def voice(client, *, seconds=1.0, state=None, mock_text=None, audio=None, headers=None):
    files = {"audio": ("v.wav", audio if audio is not None else make_wav(seconds), "audio/wav")}
    data = {"state": json.dumps(state or make_state())}
    if mock_text is not None:
        data["mock_text"] = mock_text
    return client.post("/command/voice", files=files, data=data, headers=headers or {})
