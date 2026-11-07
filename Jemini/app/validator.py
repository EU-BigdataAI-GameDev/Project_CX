"""LLM이 고른 함수 호출을 서버가 최종 판단한다. LLM 출력을 그대로 믿지 않는다."""
from __future__ import annotations

import random
from dataclasses import dataclass
from typing import Any, Optional

from .config import Settings
from .schemas import Command, CommandResponse, GameState

# 실행 함수 9개 + Chat. HackAssist는 HackTerminal / HackEnemy로 분리되어 삭제(오면 Chat 처리).
ACTIONS = ("ReloadSupport", "HackTerminal", "HackEnemy", "PulseAttack", "DamageBoost",
           "HealSupport", "Decoy", "QuickReboot", "Shield")
ALLOWED = ACTIONS + ("Chat",)

PLAYERS = ("brother", "sister")

# 사유별 기본 대사. (함수, 사유) 전용 대사가 있으면 그것을 먼저 쓴다.
DEFAULT_REJECT_LINES = {
    "LOW_BATTERY": [
        "배터리가 부족해요... 복도에서 충전하면 바로 도울게요!",
        "으, 전력이 모자라요. 충전부터 하고 올게요.",
    ],
    "AMMO_FULL": ["탄창은 이미 가득 찼어요! 마음껏 쏘세요."],
    "ALREADY_RELOADING": ["지금 장전 중이에요, 조금만 기다려요!"],
    "NO_TERMINAL": ["이 구역 단말기는 전부 열려 있어요!"],
    "NO_ENEMY": ["근처에 적이 안 보여요."],
    "ENEMY_NOT_HACKABLE": [
        "이미 기절해 있어요! 지금 공격하세요.",
        "이미 기절해 있어요! 확실히 처치하려면 공격하라고 말해 주세요.",
    ],
    "BROTHER_DOWN": ["오빠가 쓰러져 있어서 강화할 수 없어요. 재부팅부터 할까요?"],
    "HP_FULL": ["두 분 다 체력이 가득해요! 아껴 둘게요."],
    "NO_DOWNED_PLAYER": ["두 분 다 멀쩡해요!"],
    "TARGET_DOWN": ["지금은 쓰러져 있어요. 재부팅부터 할까요?"],
    "BUSY": ["지금 다른 일 하는 중이에요, 잠깐만요!"],
}

FUNCTION_REJECT_LINES = {
    ("HackEnemy", "NO_ENEMY"): ["근처에 해킹할 적이 없어요."],
    ("PulseAttack", "NO_ENEMY"): ["공격할 적이 안 보여요."],
    ("PulseAttack", "LOW_BATTERY"): ["펄스를 쏠 전력이 부족해요..."],
    ("Decoy", "NO_ENEMY"): ["근처에 유인할 적이 없어요."],
}
# 미끼: 적은 있는데 전부 그로기일 때(사유는 같은 NO_ENEMY)
DECOY_ALL_STUNNED_LINE = "다들 멈춰 있어요! 지금 지나가세요."

DEFAULT_OK_LINES = {
    "ReloadSupport": ["재장전 해드릴게요!", "탄창 바로 채워 드릴게요!", "총알 배달 왔어요!"],
    "HackTerminal": ["제가 대신 풀게요, 3초만요!", "단말기 접속 중... 금방 뚫어요!"],
    "HackEnemy": ["적 회로에 침투할게요!", "저 녀석 잠깐 재워 둘게요!"],
    "PulseAttack": ["펄스 발사! 물러서세요!", "전력 최대, 날려 버릴게요!"],
    "DamageBoost": ["레일건 출력 올려 드릴게요!", "30초 동안 화력 두 배예요!"],
    "HealSupport": ["치료 시작할게요, 버텨요!", "두 분 다 회복시켜 드릴게요!"],
    "Decoy": ["저 여기 있어요~ 따라와 보세요!", "제가 시선 끌게요, 지금이에요!"],
    "QuickReboot": ["재부팅 신호 보낼게요, 일어나세요!", "강제 재부팅 시작!"],
    "Shield": ["보호막 전개! 5초 버텨요!", "제가 지켜 드릴게요!"],
}
DAMAGE_BOOST_EXTEND_LINE = "30초 더 늘려 드릴게요!"

DEFAULT_CHAT_LINE = "음... 잘 모르겠어요. 다시 말해 줄래요?"

# 실행 후 배터리가 이 값 이하면 대사에 배터리 부족 언급(LLM이 빠뜨렸을 때의 보험)
LOW_BATTERY_WARN = 20
LOW_BATTERY_TAIL = "전력이 얼마 안 남았어요."
# "전력 최대" 같은 ok 대사는 배터리 부족 언급이 아니므로 "전력"만으로는 보지 않는다
BATTERY_WORDS = ("배터리", "전력이", "전력 부족", "전력도", "충전")
MAX_DIALOGUE_CHARS = 60


@dataclass
class LlmCall:
    """모델 원응답(또는 모의 응답)을 정규화한 것."""
    name: str
    args: dict[str, Any]
    # 실제로 응답한 모델(예비 모델로 넘어갔는지 로그용). 모의 모드는 "mock".
    model: str = ""


def _clean(text: Optional[str]) -> str:
    return (text if isinstance(text, str) else "").strip()


def costs(settings: Settings) -> dict[str, int]:
    return {
        "ReloadSupport": settings.cost_reload,
        "HackTerminal": settings.cost_hack_terminal,
        "HackEnemy": settings.cost_hack_enemy,
        "PulseAttack": settings.cost_pulse_attack,
        "DamageBoost": settings.cost_damage_boost,
        "HealSupport": settings.cost_heal,
        "Decoy": settings.cost_decoy,
        "QuickReboot": settings.cost_quick_reboot,
        "Shield": settings.cost_shield,
    }


def _alive(state: GameState, who: str) -> bool:
    return state.brother_alive if who == "brother" else state.sister_alive


def _check(name: str, state: GameState, args: dict[str, Any]) -> tuple[Optional[str], str]:
    """함수별 조건 검사. (거절 사유 또는 None, 서버가 정한 target)."""
    if name == "ReloadSupport":
        if state.brother_reloading:
            return "ALREADY_RELOADING", "brother"
        if state.brother_ammo >= state.brother_ammo_max:
            return "AMMO_FULL", "brother"
        return None, "brother"

    if name == "HackTerminal":
        on_terminal = state.sister_puzzle is not None and state.sister_puzzle.type == "terminal"
        if not on_terminal and state.terminal_locked_count == 0:
            return "NO_TERMINAL", "sister"
        return None, "sister"

    if name == "HackEnemy":
        if state.sister_puzzle is not None and state.sister_puzzle.type == "enemy":
            return None, "enemy"
        if state.enemy_alive_count == 0:
            return "NO_ENEMY", "enemy"
        if state.enemy_hackable_count == 0:
            # 더 비싼 PulseAttack으로 바꾸지 않는다
            return "ENEMY_NOT_HACKABLE", "enemy"
        return None, "enemy"

    if name == "PulseAttack":
        return ("NO_ENEMY" if state.enemy_alive_count == 0 else None), "enemy"

    if name == "DamageBoost":
        # 강화 중이면 거절 없이 연장
        return (None if state.brother_alive else "BROTHER_DOWN"), "brother"

    if name == "HealSupport":
        hurt = ((state.brother_alive and state.brother_hp < state.brother_hp_max)
                or (state.sister_alive and state.sister_hp < state.sister_hp_max))
        return (None if hurt else "HP_FULL"), "both"

    if name == "Decoy":
        return ("NO_ENEMY" if state.enemy_active_count == 0 else None), "enemies"

    if name == "QuickReboot":
        for who in PLAYERS:
            if not _alive(state, who):
                return None, who
        return "NO_DOWNED_PLAYER", "brother"

    # Shield: 지목한 사람(who), 없으면 말한 사람
    who = _clean(args.get("who")).lower()
    target = who if who in PLAYERS else state.speaker
    return (None if _alive(state, target) else "TARGET_DOWN"), target


def _reject_line(name: str, reason: str, state: GameState) -> str:
    if name == "Decoy" and reason == "NO_ENEMY" and state.enemy_alive_count > 0:
        return DECOY_ALL_STUNNED_LINE
    return random.choice(FUNCTION_REJECT_LINES.get((name, reason)) or DEFAULT_REJECT_LINES[reason])


def with_low_battery_tail(dialogue: str, battery_after: int) -> str:
    """실행 후 배터리가 20 이하인데 대사에 배터리 언급이 없으면 꼬리말을 붙인다.
    합쳐서 60자를 넘으면 줄바꿈(\\n)으로 붙인다 — UE는 줄마다 다음 말풍선으로 보여 준다."""
    if battery_after > LOW_BATTERY_WARN or any(w in dialogue for w in BATTERY_WORDS):
        return dialogue
    joined = f"{dialogue} {LOW_BATTERY_TAIL}"
    return joined if len(joined) <= MAX_DIALOGUE_CHARS else f"{dialogue}\n{LOW_BATTERY_TAIL}"


def validate(call: LlmCall, state: GameState, settings: Settings) -> CommandResponse:
    dialogue = _clean(call.args.get("dialogue"))
    heard = _clean(call.args.get("heard")) or None

    def chat(line: str) -> CommandResponse:
        return CommandResponse(status="chat", dialogue=line or DEFAULT_CHAT_LINE,
                               command=None, battery_after=state.battery, heard=heard)

    def reject(reason: str) -> CommandResponse:
        # LLM은 실행될 줄 알고 "바로 갈게요" 같은 대사를 했을 수 있다 → 거절은 항상 사유별 대사
        return CommandResponse(status="rejected", dialogue=_reject_line(call.name, reason, state),
                               command=None, battery_after=state.battery, reason=reason, heard=heard)

    # 1. 허용 목록 밖(HackAssist 포함)이면 실행하지 않고 대사만 쓴다
    if call.name not in ALLOWED:
        return chat(dialogue)

    # 2. Chat은 배터리 차감 없음, 검사 제외(드론 작업 중·쓰러진 사람도 대화 가능)
    if call.name == "Chat":
        return chat(dialogue)

    # 3. 드론 작업 중이면 어떤 기능도 실행하지 않는다
    if state.drone_busy:
        return reject("BUSY")

    # 4. 함수별 조건 → 5. 공통 마지막 배터리
    reason, target = _check(call.name, state, call.args)
    if reason:
        return reject(reason)
    cost = costs(settings)[call.name]
    if state.battery < cost:
        return reject("LOW_BATTERY")

    if not dialogue:
        dialogue = (DAMAGE_BOOST_EXTEND_LINE
                    if call.name == "DamageBoost" and state.damage_boost_remaining > 0
                    else random.choice(DEFAULT_OK_LINES[call.name]))
    # battery_after = "실행되면 남을 값". UE는 효과가 실제로 실행된 순간에 반영한다.
    battery_after = state.battery - cost
    return CommandResponse(status="ok", dialogue=with_low_battery_tail(dialogue, battery_after),
                           command=Command(name=call.name, target=target),
                           battery_after=battery_after, heard=heard)
