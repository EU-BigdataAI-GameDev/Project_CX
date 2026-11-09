"""제미니 LLM 서버. 실행: uvicorn app.main:app --port 8000"""
from __future__ import annotations

import io
import json
import random
import time
import wave
from contextlib import asynccontextmanager
from typing import Any, AsyncIterator, Awaitable, Callable, Optional

from fastapi import Depends, FastAPI, File, Form, Header, HTTPException, UploadFile
from pydantic import ValidationError

from . import llm, stt
from .config import Settings, load_settings
from .logger import write_log
from .mock import mock_call
from .schemas import CommandRequest, CommandResponse, GameState, SttPartialResponse
from .validator import LlmCall, validate

FALLBACK_LINES = [
    "앗, 신호가 잠깐 끊겼어요. 다시 말해 줄래요?",
    "지지직... 회로가 버벅여요. 한 번만 더요!",
    "잠깐 연결이 불안정해요. 다시 부탁해요!",
]

@asynccontextmanager
async def _lifespan(_app: FastAPI) -> AsyncIterator[None]:
    # 실시간 자막 모델은 백그라운드에서 읽는다(서버는 바로 요청을 받는다)
    _start_stt(get_settings())
    yield


app = FastAPI(title="Jemini LLM Server", version="2.1.0", lifespan=_lifespan)

# 테스트에서 교체할 수 있도록 함수로 둔다
_settings: Optional[Settings] = None
LlmFn = Callable[..., Awaitable[LlmCall]]
llm_fn: LlmFn = llm.call_gemini


def get_settings() -> Settings:
    global _settings
    if _settings is None:
        _settings = load_settings()
    return _settings


def check_token(settings: Settings = Depends(get_settings),
                x_api_key: Optional[str] = Header(default=None)) -> None:
    if settings.api_token and x_api_key != settings.api_token:
        raise HTTPException(status_code=401, detail="invalid API key")


def _start_stt(settings: Settings) -> None:
    stt.engine.start_loading(enabled=settings.stt_enabled, model=settings.stt_model,
                             model_dir=settings.stt_model_dir,
                             cpu_threads=settings.stt_cpu_threads or stt.default_cpu_threads(),
                             auto_download=settings.stt_auto_download)


@app.get("/health")
def health(settings: Settings = Depends(get_settings)) -> dict[str, Any]:
    return {"status": "ok", "mock": settings.mock_mode, "model": settings.gemini_model,
            "stt": stt.engine.state}


async def _run(settings: Settings, state: GameState, *, kind: str, text: Optional[str],
               audio: Optional[bytes], audio_sec: Optional[float],
               mock_text: Optional[str]) -> CommandResponse:
    start = time.perf_counter()
    raw: Optional[dict[str, Any]] = None
    error: Optional[str] = None
    use_mock = settings.mock_mode
    mock_input = text if kind == "text" else mock_text
    try:
        if use_mock:
            call = mock_call(mock_input, state, voice=kind == "voice")
        else:
            try:
                call = await llm_fn(settings, state, text=text, audio=audio)
            except llm.RateLimitError as e:
                # 모든 모델 하루 한도 초과 → 글자 입력이 있으면 mock 키워드 판별로 대신(시연 안전장치).
                # 음성은 mock이 받아 적지 못하므로 그대로 fallback.
                if not settings.mock_on_rate_limit or not mock_input:
                    raise
                error = f"{e} -> mock"
                use_mock = True
                call = mock_call(mock_input, state, voice=kind == "voice")
        raw = {"name": call.name, "args": call.args, "model": call.model or ("mock" if use_mock else settings.gemini_model)}
        response = validate(call, state, settings)
        response.mock = use_mock
        if kind == "text":
            # heard는 음성 전용 — 모델이 텍스트 입력을 그대로 받아 적는 경우가 있다
            response.heard = None
    except llm.LlmError as e:
        error = str(e)
        # 오류·지연: 대체 대사, 배터리 차감 없음, HTTP 200
        response = CommandResponse(status="fallback", dialogue=random.choice(FALLBACK_LINES),
                                   command=None, battery_after=state.battery, reason="LLM_ERROR")

    write_log(settings.log_path, {
        "kind": kind,
        "text": text,
        "audio_sec": audio_sec,
        "state": state.model_dump(),
        "mock": use_mock,
        "model_raw": raw,
        "error": error,
        "status": response.status,
        "reason": response.reason,
        "response": response.model_dump(),
        "latency_ms": round((time.perf_counter() - start) * 1000),
    })
    return response


@app.post("/command", response_model=CommandResponse, dependencies=[Depends(check_token)])
async def command(req: CommandRequest, settings: Settings = Depends(get_settings)) -> CommandResponse:
    return await _run(settings, req.state, kind="text", text=req.text, audio=None,
                      audio_sec=None, mock_text=None)


def _wav_seconds(data: bytes) -> float:
    try:
        with wave.open(io.BytesIO(data), "rb") as w:
            frames, rate = w.getnframes(), w.getframerate()
    except (wave.Error, EOFError) as e:
        raise HTTPException(status_code=422, detail=f"audio must be a WAV file: {e}") from e
    if rate <= 0:
        raise HTTPException(status_code=422, detail="invalid WAV sample rate")
    return frames / rate


@app.post("/command/voice", response_model=CommandResponse, dependencies=[Depends(check_token)])
async def command_voice(audio: UploadFile = File(...), state: str = Form(...),
                        mock_text: Optional[str] = Form(default=None),
                        settings: Settings = Depends(get_settings)) -> CommandResponse:
    try:
        game_state = GameState.model_validate(json.loads(state))
    except (json.JSONDecodeError, ValidationError) as e:
        raise HTTPException(status_code=422, detail=f"invalid state: {e}") from e

    data = await audio.read(settings.max_audio_bytes + 1)
    if len(data) > settings.max_audio_bytes:
        raise HTTPException(status_code=422, detail="audio too large")
    seconds = _wav_seconds(data)
    if seconds > settings.max_audio_sec:
        raise HTTPException(status_code=422, detail=f"audio longer than {settings.max_audio_sec}s")

    return await _run(settings, game_state, kind="voice", text=None, audio=data,
                      audio_sec=round(seconds, 2), mock_text=mock_text)


@app.post("/stt/partial", response_model=SttPartialResponse, dependencies=[Depends(check_token)])
async def stt_partial(audio: UploadFile = File(...),
                      settings: Settings = Depends(get_settings)) -> SttPartialResponse:
    """말하는 도중 실시간 자막: 지금까지 녹음된 WAV → 받아 적은 글자. 명령은 실행하지 않는다(mock 모드 무관).

    모델이 읽는 중·꺼짐·실패·무음이면 text=null(HTTP 200) — UE는 무시하고 지금처럼 동작한다.
    """
    start = time.perf_counter()
    # lifespan 없이 띄운 경우(테스트 등)에도 첫 요청에서 읽기 시작
    if stt.engine.state == "idle":
        _start_stt(settings)
    data = await audio.read(settings.max_audio_bytes + 1)
    if len(data) > settings.max_audio_bytes:
        raise HTTPException(status_code=422, detail="audio too large")
    try:
        samples, seconds = stt.decode_wav(data)
    except ValueError as e:
        raise HTTPException(status_code=422, detail=str(e)) from e
    if seconds > settings.max_audio_sec + 0.05:
        raise HTTPException(status_code=422, detail=f"audio longer than {settings.max_audio_sec}s")
    text, state = await stt.engine.transcribe(samples)
    return SttPartialResponse(text=text, ms=round((time.perf_counter() - start) * 1000), state=state)
