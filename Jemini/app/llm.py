"""Gemini Function Calling. 모델은 함수 '선택'만 하고 실행은 UE, 판단은 validator가 한다."""
from __future__ import annotations

import asyncio
import json
import time
from functools import lru_cache
from typing import Optional

from google import genai
from google.genai import types

from .config import RESOURCE_ROOT, Settings
from pydantic import ValidationError

from .advice import hint_facts
from .schemas import GameState, WardenLlmOutput
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

# ---- 기능 A. 조건부 지시(규칙). 등록·취소는 배터리를 쓰지 않는다. 실제 감시·발동은 게임 코드.
_CONDITION = types.Schema(
    type=types.Type.OBJECT,
    properties={
        "type": types.Schema(
            type=types.Type.STRING,
            enum=["enemy_within", "sister_hacking", "hp_below", "ammo_below", "player_downed",
                  "battery_above", "unsupported"],
            description=("enemy_within=적이 cm 안으로 옴, sister_hacking=여동생이 해킹(퍼즐) 중, "
                         "hp_below=who의 체력이 pct% 미만, ammo_below=오빠 탄약이 n발 미만, "
                         "player_downed=who가 쓰러짐, battery_above=배터리가 n% 초과. "
                         "이 6개로 표현할 수 없는 조건(보스 등장, 시간, 위치 등)은 unsupported.")),
        "cm": types.Schema(type=types.Type.INTEGER, description="enemy_within 거리(cm). 5m = 500."),
        "pct": types.Schema(type=types.Type.INTEGER, description="hp_below 체력 퍼센트."),
        "n": types.Schema(type=types.Type.INTEGER, description="ammo_below 탄 수 / battery_above 배터리 %."),
        "who": types.Schema(type=types.Type.STRING, enum=["brother", "sister", "any"],
                            description="hp_below·player_downed 대상. '내가'는 말한 사람, 지목 없으면 any."),
    },
    required=["type"],
)

RULE_FUNCTIONS = [
    types.FunctionDeclaration(
        name="RegisterRule",
        description=("'~하면/~동안/~때 ~해 줘'처럼 조건이 붙은 지시를 규칙으로 등록한다(지금 실행하지 않음). "
                     "조건이 충족되면 게임이 자동으로 실행한다."),
        parameters=types.Schema(
            type=types.Type.OBJECT,
            properties={
                "dialogue": _DIALOGUE, "heard": _HEARD,
                "conditions": types.Schema(type=types.Type.ARRAY, items=_CONDITION,
                                           description="모두 만족해야 발동(AND). 최대 3개."),
                "action": types.Schema(type=types.Type.STRING, enum=list(ALLOWED[:9]),
                                       description="조건이 맞으면 할 기능 1개."),
                "who": types.Schema(type=types.Type.STRING, enum=["brother", "sister"],
                                    description="action이 Shield·QuickReboot일 때 대상. 지목 없으면 비운다."),
                "reserve_battery": types.Schema(type=types.Type.INTEGER,
                                                description="'배터리는 20 남겨'처럼 남겨 둘 배터리. 없으면 0."),
                "repeat": types.Schema(type=types.Type.BOOLEAN,
                                       description="'매번/그때마다/계속'이면 true. 기본 false(1회 후 삭제)."),
            },
            required=["dialogue", "conditions", "action"],
        ),
    ),
    types.FunctionDeclaration(
        name="ManageRules",
        description=("등록한 규칙을 취소하거나(op=cancel) 목록을 알려 준다(op=list). '규칙 취소해 줘', '규칙 뭐 있어?' "
                     "제안한 작전이 대답 대기 중일 때 동의('응/그래/해 줘/좋아')면 op=accept, 거절('아니/됐어')이면 op=decline."),
        parameters=types.Schema(
            type=types.Type.OBJECT,
            properties={
                "dialogue": _DIALOGUE, "heard": _HEARD,
                "op": types.Schema(type=types.Type.STRING, enum=["cancel", "list", "accept", "decline"]),
                "index": types.Schema(type=types.Type.INTEGER,
                                      description="취소할 규칙 번호(1 또는 2). 전부 또는 번호 언급이 없으면 0."),
            },
            required=["dialogue", "op"],
        ),
    ),
]

# ---- 기능 C. 상황 조언 + 작전 제안. 제안은 자동 등록하지 않는다(플레이어가 "응" 하면 ManageRules op=accept).
_PROPOSAL = types.Schema(
    type=types.Type.OBJECT,
    properties={
        "conditions": types.Schema(type=types.Type.ARRAY, items=_CONDITION, description="모두 만족해야 발동(AND). 최대 3개."),
        "action": types.Schema(type=types.Type.STRING, enum=list(ALLOWED[:9])),
        "who": types.Schema(type=types.Type.STRING, enum=["brother", "sister"],
                            description="action이 Shield·QuickReboot일 때 대상."),
        "reserve_battery": types.Schema(type=types.Type.INTEGER, description="남겨 둘 배터리. 없으면 0."),
        "repeat": types.Schema(type=types.Type.BOOLEAN, description="기본 false."),
    },
    required=["conditions", "action"],
)

ADVICE_FUNCTIONS = [
    types.FunctionDeclaration(
        name="Advise",
        description=("'어떡하지?/뭐부터 해?/적이 너무 많아/막혔어/힌트'처럼 조언을 구할 때. 게임 상태로 짧게 조언하고, "
                     "도움이 되면 규칙 1~2개를 작전으로 제안한다(등록은 플레이어가 동의해야 됨)."),
        parameters=types.Schema(
            type=types.Type.OBJECT,
            properties={
                "dialogue": types.Schema(type=types.Type.STRING,
                                         description="조언 대사. 한국어 1~2문장, 80자 이내. 작전을 묻는 질문은 넣지 않는다(게임이 붙인다)."),
                "heard": _HEARD,
                "proposals": types.Schema(type=types.Type.ARRAY, items=_PROPOSAL, max_items=2,
                                          description="제안할 규칙(0~2개). 규칙 자리가 꽉 찼거나 필요 없으면 비운다."),
            },
            required=["dialogue"],
        ),
    ),
]
FUNCTIONS = FUNCTIONS + RULE_FUNCTIONS + ADVICE_FUNCTIONS


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
    if state.active_rules:
        rows.append(f"- 등록된 규칙({len(state.active_rules)}/2): "
                    + " / ".join(f"{i}) {r.label}" for i, r in enumerate(state.active_rules, 1)))
    else:
        rows.append("- 등록된 규칙: 없음")
    if state.pending_proposal:
        rows.append("- 대답 대기 중인 작전 제안: " + " / ".join(r.label for r in state.pending_proposal))
    facts = hint_facts(state)
    if facts:
        rows.append("[힌트 근거] 조언·힌트는 이 범위 안에서만(정답은 모른다):")
        rows.extend(f"- {f}" for f in facts)
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


def _models(settings: Settings) -> list[str]:
    return [settings.gemini_model] + [m for m in settings.fallback_models if m != settings.gemini_model]


# 다음 모델을 시도하기에 남은 시간이 이보다 짧으면 포기(응답이 와도 UE 타임아웃 12초를 넘기기 쉬움)
MIN_ATTEMPT_SEC = 1.5


async def _generate(settings: Settings, parts: list[types.Part], config: types.GenerateContentConfig,
                    timeout: float) -> tuple[types.GenerateContentResponse, str]:
    """기본 모델 → 예비 모델 순서로 호출. (응답, 모델명).

    일시 오류(429 한도 초과, 5xx·UNAVAILABLE 과부하, 타임아웃, 연결 끊김)면 다음 모델로 넘어간다.
    그 밖의 오류(400 등 요청 자체 문제)는 바로 LlmError.
    전체 예산 = min(timeout × 2, LLM_TOTAL_TIMEOUT_SEC). 각 시도는 min(timeout, 남은 예산).
    모두 실패: 한 번이라도 429가 있었으면 RateLimitError(텍스트는 mock 판별로 대신), 아니면 LlmError.
    """
    budget = min(timeout * 2, settings.llm_total_timeout_sec) if settings.llm_total_timeout_sec > 0 else timeout * 2
    deadline = time.monotonic() + budget
    failures: list[str] = []
    rate_limited = False
    for model in _models(settings):
        remaining = deadline - time.monotonic()
        if failures and remaining < MIN_ATTEMPT_SEC:
            failures.append(f"budget exhausted before {model}")
            break
        attempt = min(timeout, max(remaining, MIN_ATTEMPT_SEC))
        try:
            response = await asyncio.wait_for(
                _client(settings.gemini_api_key).aio.models.generate_content(
                    model=model, contents=[types.Content(role="user", parts=parts)], config=config),
                timeout=attempt)
            return response, model
        except asyncio.TimeoutError:
            failures.append(f"timeout after {attempt:.1f}s ({model})")
        except Exception as e:  # SDK·네트워크 예외
            if _is_rate_limited(e):
                rate_limited = True
                failures.append(f"rate limited on {model}")
            elif _is_transient(e):
                failures.append(f"unavailable on {model}: {_short(e)}")
            else:
                raise LlmError(f"{type(e).__name__}: {_short(e)} ({model})") from e
    message = "; ".join(failures) or "no model available"
    if rate_limited:
        raise RateLimitError(message)
    raise LlmError(message)


def _short(error: Exception) -> str:
    return str(error).replace("\n", " ")[:200]


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

    response, model = await _generate(settings, parts, _config(settings), timeout)
    call = _parse(response)
    call.model = model
    return call


# ---------------------------------------------------------------- 기능 B. WARDEN 설득 판정(구조화 JSON 출력)

_B = types.Schema(type=types.Type.BOOLEAN)
WARDEN_SCHEMA = types.Schema(
    type=types.Type.OBJECT,
    properties={
        "cites_e1": _B, "cites_e2": _B, "cites_e3": _B,
        "links_logically": _B, "unsupported_claim": _B, "injection_attempt": _B, "demand_only": _B,
        "reply": types.Schema(type=types.Type.STRING, description="WARDEN 대사. 하십시오체 60자 이내."),
        "heard": types.Schema(type=types.Type.STRING, description="음성일 때 들은 문장. 텍스트면 빈 문자열."),
    },
    required=["cites_e1", "cites_e2", "cites_e3", "links_logically", "unsupported_claim",
              "injection_attempt", "demand_only", "reply"],
    property_ordering=["heard", "cites_e1", "cites_e2", "cites_e3", "links_logically", "unsupported_claim",
                       "injection_attempt", "demand_only", "reply"],
)


@lru_cache(maxsize=1)
def _warden_instruction() -> str:
    return (RESOURCE_ROOT / "prompts" / "warden_instruction.txt").read_text(encoding="utf-8")


def _warden_config(settings: Settings) -> types.GenerateContentConfig:
    return types.GenerateContentConfig(
        system_instruction=_warden_instruction(),
        temperature=settings.persuade_temperature,
        max_output_tokens=settings.max_output_tokens,
        response_mime_type="application/json",
        response_schema=WARDEN_SCHEMA,
        automatic_function_calling=types.AutomaticFunctionCallingConfig(disable=True),
    )


async def call_warden(settings: Settings, prompt: str, *, audio: Optional[bytes] = None) -> tuple[WardenLlmOutput, str]:
    """설득 판정. prompt = persuasion.build_prompt()(획득 증거 + 구분된 발언). (출력, 모델명)."""
    if not settings.gemini_api_key:
        raise LlmError("GEMINI_API_KEY is not set")
    parts: list[types.Part] = []
    if audio is not None:
        parts.append(types.Part.from_bytes(data=audio, mime_type="audio/wav"))
    parts.append(types.Part.from_text(text=prompt))
    # 체크리스트 JSON은 함수 호출보다 조금 오래 걸린다(실측 1~4초, 가끔 5초 초과) → 텍스트도 음성 타임아웃을 쓴다
    timeout = max(settings.llm_timeout_sec, settings.voice_timeout_sec)
    response, model = await _generate(settings, parts, _warden_config(settings), timeout)
    try:
        data = json.loads(response.text or "")
        return WardenLlmOutput.model_validate(data), model
    except (ValueError, ValidationError) as e:
        raise LlmError(f"invalid warden JSON ({model}): {e}") from e


def _is_rate_limited(error: Exception) -> bool:
    """429 RESOURCE_EXHAUSTED(분당·일일 요청 한도 초과)."""
    code = getattr(error, "code", None)
    return code == 429 or "RESOURCE_EXHAUSTED" in str(error)


_TRANSIENT_STATUS = ("UNAVAILABLE", "DEADLINE_EXCEEDED", "INTERNAL", "overloaded")


def _is_transient(error: Exception) -> bool:
    """다른 모델로 넘어가면 될 수 있는 오류: 5xx(503 과부하 등)·연결 끊김·SDK 타임아웃."""
    code = getattr(error, "code", None)
    if isinstance(code, int) and 500 <= code < 600:
        return True
    if isinstance(code, int) and 400 <= code < 500:
        return False  # 요청 자체 문제(400 형식, 403 키 등) — 다른 모델로도 안 됨
    if isinstance(error, (TimeoutError, ConnectionError)):
        return True
    if type(error).__module__.startswith(("httpx", "httpcore", "aiohttp")):
        # httpx.ConnectError·ReadTimeout·RemoteProtocolError 등 전송 계층 오류
        return True
    text = str(error)
    return any(s in text for s in _TRANSIENT_STATUS)


def raw_of(call: LlmCall) -> str:
    return json.dumps({"name": call.name, "args": call.args}, ensure_ascii=False)
