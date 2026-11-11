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

from . import advice, llm, persuasion, rules, stt
from .config import Settings, load_settings
from .logger import write_log
from .mock import mock_call
from .schemas import (CommandRequest, CommandResponse, GameState, PersuadeMeta, PersuadeRequest,
                      PersuadeResponse, SttPartialResponse, ValidateRequest, ValidateResponse)
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


app = FastAPI(title="Jemini LLM Server", version="2.2.0", lifespan=_lifespan)

# 테스트에서 교체할 수 있도록 함수로 둔다
_settings: Optional[Settings] = None
LlmFn = Callable[..., Awaitable[LlmCall]]
llm_fn: LlmFn = llm.call_gemini
# 기능 B. WARDEN 판정(테스트에서 교체)
warden_fn: Callable[..., Awaitable[tuple[Any, str]]] = llm.call_warden


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
    shortcut = advice.quick_answer(text, state) if kind == "text" else None
    try:
        if shortcut is not None:
            # 기능 C. 작전 제안에 대한 짧은 대답("응", "아니")은 LLM 없이(호출 수·지연 절약)
            call = shortcut
            use_mock = False
        elif use_mock:
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


@app.post("/validate", response_model=ValidateResponse, dependencies=[Depends(check_token)])
def validate_action(req: ValidateRequest, settings: Settings = Depends(get_settings)) -> ValidateResponse:
    """규칙 발동 직전 재검증(기능 A). LLM 없이 기존 검증기 + 예비 배터리. mock 모드와 무관."""
    start = time.perf_counter()
    response = rules.check_action(req.action, req.who, req.reserve_battery, req.state, settings)
    write_log(settings.log_path, {
        "kind": "validate",
        "action": req.action,
        "who": req.who,
        "reserve_battery": req.reserve_battery,
        "state": req.state.model_dump(),
        "status": response.status,
        "reason": response.reason,
        "response": response.model_dump(),
        "latency_ms": round((time.perf_counter() - start) * 1000),
    })
    return response


# ---------------------------------------------------------------- 기능 B. WARDEN 설득

async def _persuade(settings: Settings, acquired: list[str], session: str, *, kind: str,
                    text: Optional[str], audio: Optional[bytes], audio_sec: Optional[float],
                    transcribe: Optional[Callable[[], Awaitable[Optional[str]]]] = None,
                    credited: Optional[list[str]] = None) -> PersuadeResponse:
    """텍스트: text. 음성: audio(LLM이 직접 듣는다) + transcribe(오프라인 폴백 때만 부르는 로컬 받아 적기/mock_text)."""
    start = time.perf_counter()
    raw: Optional[dict[str, Any]] = None
    error: Optional[str] = None
    use_mock = settings.mock_mode
    stt_text: Optional[str] = None

    async def offline_input() -> Optional[str]:
        nonlocal stt_text
        if kind == "text":
            return text
        if transcribe is not None:
            stt_text = await transcribe()
        return stt_text
    credited = [e for e in persuasion.normalize_evidence(credited or []) if e in acquired]
    prompt = persuasion.build_prompt(text if kind == "text" else None, acquired, credited)
    if not persuasion.try_begin_attempt(session, settings.persuade_cooldown_sec):
        response = persuasion.simple_response("cooldown", "RATE_LIMITED", acquired, credited=credited)
    else:
        try:
            if use_mock:
                offline_text = await offline_input()
                if not offline_text:
                    response = persuasion.simple_response("rejected", "NO_SPEECH", acquired, mock=True, credited=credited)
                else:
                    checklist, reply = persuasion.judge_offline(offline_text, acquired, credited)
                    response = persuasion.build_response(checklist, reply, acquired, mock=True,
                                                         heard=stt_text if kind == "voice" else None,
                                                         credited=credited)
            else:
                try:
                    out, model = await warden_fn(settings, prompt, audio=audio if kind == "voice" else None)
                    raw = {**out.model_dump(), "model": model}
                    checklist = persuasion.map_llm_checklist(out.model_dump(), acquired)
                    heard = (out.heard.strip() or None) if kind == "voice" else None
                    response = persuasion.build_response(checklist, out.reply, acquired, mock=False, heard=heard,
                                                         credited=credited)
                except llm.RateLimitError as e:
                    # 모든 모델 한도 초과 → 키워드 판정(시연 안전장치). 음성은 로컬 받아 적기가 있을 때만.
                    offline_text = await offline_input() if settings.mock_on_rate_limit else None
                    if not offline_text:
                        raise
                    error = f"{e} -> mock"
                    use_mock = True
                    checklist, reply = persuasion.judge_offline(offline_text, acquired, credited)
                    response = persuasion.build_response(checklist, reply, acquired, mock=True,
                                                         heard=stt_text if kind == "voice" else None,
                                                         credited=credited)
        except llm.LlmError as e:
            error = str(e)
            response = persuasion.simple_response("fallback", "LLM_ERROR", acquired, credited=credited)

    write_log(settings.log_path, {
        "kind": f"persuade_{kind}",
        "text": text,
        "stt_text": stt_text,
        "audio_sec": audio_sec,
        "evidence": acquired,
        "credited_in": credited,
        "session": session,
        "mock": use_mock,
        "model_raw": raw,
        "error": error,
        "status": response.status,
        "reason": response.reason,
        "response": response.model_dump(),
        "latency_ms": round((time.perf_counter() - start) * 1000),
    })
    return response


@app.post("/persuade", response_model=PersuadeResponse, dependencies=[Depends(check_token)])
async def persuade(req: PersuadeRequest, settings: Settings = Depends(get_settings)) -> PersuadeResponse:
    """WARDEN 설득 판정(기능 B). 판정은 체크리스트 + 서버 규칙(persuasion.decide)."""
    acquired = persuasion.normalize_evidence(req.evidence)
    return await _persuade(settings, acquired, req.session, kind="text", text=req.text, audio=None,
                           audio_sec=None, credited=req.credited)


@app.post("/persuade/voice", response_model=PersuadeResponse, dependencies=[Depends(check_token)])
async def persuade_voice(audio: UploadFile = File(...), meta: str = Form(...),
                         mock_text: Optional[str] = Form(default=None),
                         settings: Settings = Depends(get_settings)) -> PersuadeResponse:
    """음성 설득. /command/voice와 같은 WAV. LLM 모드는 Gemini가 직접 듣고(heard), 오프라인 폴백은 로컬 STT 글자로 판정."""
    try:
        info = PersuadeMeta.model_validate(json.loads(meta))
    except (json.JSONDecodeError, ValidationError) as e:
        raise HTTPException(status_code=422, detail=f"invalid meta: {e}") from e
    data = await audio.read(settings.max_audio_bytes + 1)
    if len(data) > settings.max_audio_bytes:
        raise HTTPException(status_code=422, detail="audio too large")
    seconds = _wav_seconds(data)
    if seconds > settings.max_audio_sec:
        raise HTTPException(status_code=422, detail=f"audio longer than {settings.max_audio_sec}s")
    async def transcribe() -> Optional[str]:
        # 오프라인 판정에 쓸 글자: 테스트용 mock_text, 없으면 로컬 faster-whisper(준비 안 됐으면 None)
        if mock_text and mock_text.strip():
            return mock_text.strip()[:persuasion.TEXT_MAX]
        if stt.engine.state == "idle":
            _start_stt(settings)
        try:
            samples, _ = stt.decode_wav(data)
        except ValueError:
            return None
        result, _state = await stt.engine.transcribe(samples)
        return result

    acquired = persuasion.normalize_evidence(info.evidence)
    return await _persuade(settings, acquired, info.session, kind="voice", text=None, audio=data,
                           audio_sec=round(seconds, 2), transcribe=transcribe, credited=info.credited)


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
    ms = round((time.perf_counter() - start) * 1000)
    # 진단: 요청마다 길이·음량(RMS)·구간별 결과를 logs/stt.jsonl에 남긴다(음성 원본은 저장하지 않음)
    try:
        import numpy as np
        rms = float(np.sqrt(np.mean(np.square(samples)))) if len(samples) else 0.0
        write_log(settings.log_path.with_name("stt.jsonl"), {
            "seconds": round(seconds, 2), "rms": round(rms, 5), "peak": round(float(np.max(np.abs(samples))) if len(samples) else 0.0, 4),
            "state": state, "text": text, "segments": list(stt.last_segments) if state == "ready" else [], "ms": ms})
    except Exception:  # noqa: BLE001 - 진단 로그 실패는 무시
        pass
    return SttPartialResponse(text=text, ms=ms, state=state)
