"""Gemini Function Calling. 모델은 함수 '선택'만 하고 실행은 UE, 판단은 validator가 한다."""
from __future__ import annotations

import asyncio
import json
from functools import lru_cache
from typing import Optional

from google import genai
from google.genai import types

from .config import RESOURCE_ROOT, Settings
from .schemas import GameState
from .validator import ALLOWED, LlmCall


class LlmError(Exception):
    """호출 실패·타임아웃·형식 오류. main에서 fallback으로 바뀐다."""


class RateLimitError(LlmError):
    """기본·예비 모델 모두 한도 초과(429). main이 텍스트 요청이면 mock 판별로 대신 처리한다."""


_DIALOGUE = types.Schema(type=types.Type.STRING,
                         description="제미니의 대사. 한국어 1~2문장, 60자 이내, 이모지·마크다운 금지.")
_HEARD = types.Schema(type=types.Type.STRING,
                      description="음성 입력일 때 들은 문장을 그대로 받아 적는다. 텍스트 입력이면 비운다.")


_WHO = types.Schema(type=types.Type.STRING, enum=["brother", "sister"],
                    description="보호막을 받을 사람. 문장에서 오빠/여동생을 지목했을 때만 채우고, 아니면 비운다.")


def _decl(name: str, description: str, extra: dict[str, types.Schema] | None = None) -> types.FunctionDeclaration:
    return types.FunctionDeclaration(
        name=name,
        description=description,
        parameters=types.Schema(
            type=types.Type.OBJECT,
            properties={"dialogue": _DIALOGUE, "heard": _HEARD, **(extra or {})},
            required=["dialogue"],
        ),
    )


# 순서·이름은 validator.ALLOWED와 같다. 선언문은 짧게(토큰), 헷갈리는 쌍은 대비 예문을 넣는다.
FUNCTIONS = [
    _decl("ReloadSupport", "오빠의 레일건을 재장전한다(탄·총알·장전). 오빠는 이것으로만 재장전할 수 있다."),
    _decl("HackTerminal", "단말기·퍼즐·문·비밀번호 해킹을 3초 뒤 대신 푼다. 여동생이 연 단말기, 없으면 다음 잠긴 단말기."),
    _decl("HackEnemy", "적 로봇 1기를 즉시 해킹해 잠시 기절시킨다. 적에 대한 애매한 요청(막아줘, 어떻게 해봐)의 기본 선택."),
    _decl("PulseAttack", "펄스로 적 1기를 즉시 처치한다. 비싸다. '공격해/처치해/죽여/없애'처럼 확실한 공격 명령일 때만."),
    _decl("DamageBoost", "오빠 레일건 피해를 30초 동안 올린다(공격력·데미지·화력). 강화 중이면 연장. '적 공격해'는 PulseAttack."),
    _decl("HealSupport", "살아 있는 오빠·여동생의 체력을 회복한다(힐·체력·치료). 쓰러진 사람 부활은 QuickReboot."),
    _decl("Decoy", "드론이 빛과 소리로 5초간 근처 적들의 추적을 끈다(어그로·유인·시선 끌기)."),
    _decl("QuickReboot", "쓰러진 플레이어를 기다리지 않고 바로 부활시킨다(살려·부활·일으켜·재부팅)."),
    _decl("Shield", "5초 무적 보호막(보호막·지켜·보호·실드). '막아줘'는 HackEnemy.", {"who": _WHO}),
    _decl("Chat", "명령이 아닌 질문·잡담, 할 수 없는 요청의 거절, 잘 안 들렸을 때 되묻기."),
]


@lru_cache(maxsize=1)
def _system_instruction() -> str:
    return (RESOURCE_ROOT / "prompts" / "system_instruction.txt").read_text(encoding="utf-8")


@lru_cache(maxsize=4)
def _client(api_key: str) -> genai.Client:
    return genai.Client(api_key=api_key)


def _who(name: str) -> str:
    return "오빠" if name == "brother" else "여동생"


def _player(label: str, hp: int, hp_max: int, alive: bool, respawn: float, shield: float) -> str:
    if not alive:
        return f"- {label}: 쓰러짐(부활 대기 {respawn:.0f}초)"
    return f"- {label} 체력: {hp}/{hp_max}" + (f" (보호막 {shield:.0f}초)" if shield > 0 else "")


def describe_state(state: GameState) -> str:
    puzzle = "없음" if state.sister_puzzle is None else (
        f"{'단말기' if state.sister_puzzle.type == 'terminal' else '적'} 해킹 중"
        f"(진행 {round(state.sister_puzzle.progress * 100)}%)")
    if state.enemy_alive_count == 0:
        enemy = "범위 안 없음"
    else:
        dist = "" if state.enemy_nearest_dist is None else f", 가장 가까운 {state.enemy_nearest_dist:.0f}m"
        enemy = (f"범위 안 {state.enemy_alive_count}기(해킹 가능 {state.enemy_hackable_count}, "
                 f"움직이는 {state.enemy_active_count}{dist})")
    boost = (f"{state.damage_boost_remaining:.0f}초 남음" if state.damage_boost_remaining > 0 else "없음")
    rows = [
        "[게임 상태]",
        f"- 말한 사람: {_who(state.speaker)}",
        f"- 오빠 탄약: {state.brother_ammo}/{state.brother_ammo_max}"
        f"{' (장전 중)' if state.brother_reloading else ''}",
        _player("오빠", state.brother_hp, state.brother_hp_max, state.brother_alive,
                state.brother_respawn_remaining, state.brother_shield_remaining),
        _player("여동생", state.sister_hp, state.sister_hp_max, state.sister_alive,
                state.sister_respawn_remaining, state.sister_shield_remaining),
        f"- 공격력 강화: {boost}",
        f"- 여동생 퍼즐: {puzzle}",
        f"- 적: {enemy}",
        f"- 잠긴 단말기: {state.terminal_locked_count}개",
        f"- 제미니 배터리: {state.battery}%",
    ]
    return "\n".join(rows)


def _config(settings: Settings) -> types.GenerateContentConfig:
    return types.GenerateContentConfig(
        system_instruction=_system_instruction(),
        temperature=settings.temperature,
        # 대사 60자 + 함수 호출 JSON이면 충분. 너무 낮으면 생각 토큰에 밀려 함수 호출이 잘릴 수 있어 여유를 둔다.
        max_output_tokens=settings.max_output_tokens,
        tools=[types.Tool(function_declarations=FUNCTIONS)],
        # 함수 호출 강제: 텍스트만 오는 응답을 없앤다
        tool_config=types.ToolConfig(function_calling_config=types.FunctionCallingConfig(
            mode=types.FunctionCallingConfigMode.ANY, allowed_function_names=list(ALLOWED))),
        automatic_function_calling=types.AutomaticFunctionCallingConfig(disable=True),
    )


def _parse(response: types.GenerateContentResponse) -> LlmCall:
    calls = response.function_calls or []
    if not calls:
        raise LlmError("no function call in response")
    fc = calls[0]
    return LlmCall(name=fc.name or "", args=dict(fc.args or {}))


async def call_gemini(settings: Settings, state: GameState, *, text: Optional[str] = None,
                      audio: Optional[bytes] = None) -> LlmCall:
    if not settings.gemini_api_key:
        raise LlmError("GEMINI_API_KEY is not set")
    parts: list[types.Part] = []
    if audio is not None:
        parts.append(types.Part.from_bytes(data=audio, mime_type="audio/wav"))
        parts.append(types.Part.from_text(text="위 음성은 플레이어의 말이다. 들은 문장을 heard에 적어라.\n"
                                               + describe_state(state)))
        timeout = settings.voice_timeout_sec
    else:
        parts.append(types.Part.from_text(text=f"[플레이어]\n{text}\n\n{describe_state(state)}"))
        timeout = settings.llm_timeout_sec

    # 기본 모델 → 한도 초과(429)일 때만 예비 모델 순서로 재시도. 다른 오류는 바로 fallback.
    models = [settings.gemini_model] + [m for m in settings.fallback_models if m != settings.gemini_model]
    last_error = ""
    for model in models:
        try:
            response = await asyncio.wait_for(
                _client(settings.gemini_api_key).aio.models.generate_content(
                    model=model,
                    contents=[types.Content(role="user", parts=parts)],
                    config=_config(settings)),
                timeout=timeout)
        except asyncio.TimeoutError as e:
            raise LlmError(f"timeout after {timeout}s ({model})") from e
        except Exception as e:  # SDK·네트워크 예외
            if _is_rate_limited(e):
                last_error = f"rate limited on {model}"
                continue
            raise LlmError(f"{type(e).__name__}: {e} ({model})") from e
        call = _parse(response)
        call.model = model
        return call
    if last_error:
        raise RateLimitError(last_error)
    raise LlmError("no model available")


def _is_rate_limited(error: Exception) -> bool:
    """429 RESOURCE_EXHAUSTED(분당·일일 요청 한도 초과)."""
    code = getattr(error, "code", None)
    return code == 429 or "RESOURCE_EXHAUSTED" in str(error)


def raw_of(call: LlmCall) -> str:
    return json.dumps({"name": call.name, "args": call.args}, ensure_ascii=False)
