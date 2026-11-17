"""기능 A. 자연어 조건부 지시(규칙). LLM이 고른 RegisterRule / ManageRules 인자를 서버가 화이트리스트로 검증한다.

- 규칙 보관·감시·발동은 UE(서버 권위). 서버는 상태를 저장하지 않고, 개수·목록은 GameState.active_rules로 안다.
- 등록·취소·목록은 배터리를 쓰지 않는다(드론 작업 중에도 가능).
- 발동 직전 UE가 POST /validate로 기존 검증기를 다시 통과시킨다(check_action).
"""
from __future__ import annotations

import random
from typing import Any, Optional

from .config import Settings
from .schemas import (CommandResponse, GameState, Rule, RuleAction, RuleCondition, ValidateResponse)

MAX_RULES = 2
MAX_CONDITIONS = 3
CONDITION_TYPES = ("enemy_within", "sister_hacking", "hp_below", "ammo_below", "player_downed", "battery_above")

# 값이 없을 때 기본값(거절하지 않고 채운다)
DEFAULT_ENEMY_CM = 500
DEFAULT_HP_PCT = 30
DEFAULT_AMMO_N = 5
DEFAULT_BATTERY_N = 50
# 적 탐색 범위(UE EnemySearchRange 15m)
ENEMY_CM_MIN, ENEMY_CM_MAX = 100, 1500

RULE_REJECT_LINES = {
    "UNSUPPORTED_CONDITION": [
        "그 조건은 제가 아직 감시할 수 없어요.\n적 거리·해킹 중·체력·탄약·쓰러짐·배터리만 돼요.",
        "음, 그건 제 센서로 못 봐요.\n적 거리·해킹 중·체력·탄약·쓰러짐·배터리 조건만 돼요.",
    ],
    "TOO_MANY_CONDITIONS": ["조건은 3개까지만 묶을 수 있어요. 조금 줄여 줄래요?"],
    "INVALID_RULE": ["언제 무엇을 할지 다시 한 번 말해 줄래요?"],
    "RULE_LIMIT": ["규칙은 2개까지만 기억할 수 있어요. 하나 취소할까요?"],
    "NO_RULE": ["지금 등록된 규칙이 없어요."],
    # 기능 C. 대답할 작전 제안이 없음(시간이 지났거나 제안한 적 없음)
    "NO_PROPOSAL": ["지금 기다리는 작전 제안이 없어요. 다시 물어봐 줄래요?"],
}

ACTION_KO = {
    "ReloadSupport": "재장전", "HackTerminal": "단말기 해킹", "HackEnemy": "적 해킹",
    "PulseAttack": "펄스", "DamageBoost": "공격력 강화", "HealSupport": "회복",
    "Decoy": "미끼", "QuickReboot": "재부팅", "Shield": "보호막",
}
WHO_KO = {"brother": "오빠", "sister": "동생", "any": "누구든"}

# 규칙 발동 대사(/validate ok)
RULE_FIRE_LINES = {
    "ReloadSupport": "약속대로 재장전할게요!",
    "HackTerminal": "약속대로 단말기 뚫을게요!",
    "HackEnemy": "약속대로 적 회로에 침투할게요!",
    "PulseAttack": "약속대로 펄스 발사!",
    "DamageBoost": "약속대로 화력 올려 드릴게요!",
    "HealSupport": "약속대로 치료 시작할게요!",
    "Decoy": "약속대로 미끼 출동이에요!",
    "QuickReboot": "약속대로 바로 재부팅할게요!",
    "Shield": "약속대로 보호막 전개!",
}


def _int(value: Any) -> Optional[int]:
    try:
        return None if value is None or isinstance(value, bool) else int(round(float(value)))
    except (TypeError, ValueError):
        return None


def _str(value: Any) -> str:
    return value.strip().lower() if isinstance(value, str) else ""


def _reject(reason: str, state: GameState, heard: Optional[str]) -> CommandResponse:
    return CommandResponse(status="rejected", dialogue=random.choice(RULE_REJECT_LINES[reason]),
                           command=None, battery_after=state.battery, reason=reason, heard=heard)


def _normalize_condition(raw: Any, state: GameState) -> Optional[RuleCondition]:
    """화이트리스트 조건이면 정규화해서 돌려준다. 모르는 타입(unsupported 포함)이면 None."""
    if not isinstance(raw, dict):
        return None
    ctype = _str(raw.get("type"))
    if ctype not in CONDITION_TYPES:
        return None
    if ctype == "enemy_within":
        cm = _int(raw.get("cm"))
        if cm is None or cm <= 0:
            cm = DEFAULT_ENEMY_CM
        elif cm < 50:
            # 모델이 m 단위로 준 경우(5 → 500cm)
            cm *= 100
        return RuleCondition(type=ctype, cm=max(ENEMY_CM_MIN, min(ENEMY_CM_MAX, cm)))
    if ctype == "sister_hacking":
        return RuleCondition(type=ctype)
    if ctype == "hp_below":
        pct = _int(raw.get("pct"))
        pct = DEFAULT_HP_PCT if pct is None or pct <= 0 else min(pct, 99)
        who = _str(raw.get("who"))
        # 대상이 없으면 말한 사람 체력
        return RuleCondition(type=ctype, pct=pct, who=who if who in WHO_KO else state.speaker)
    if ctype == "ammo_below":
        n = _int(raw.get("n"))
        n = DEFAULT_AMMO_N if n is None or n <= 0 else min(n, state.brother_ammo_max)
        return RuleCondition(type=ctype, n=n)
    if ctype == "player_downed":
        who = _str(raw.get("who"))
        return RuleCondition(type=ctype, who=who if who in WHO_KO else "any")
    # battery_above
    n = _int(raw.get("n"))
    n = DEFAULT_BATTERY_N if n is None or n < 0 else min(n, 99)
    return RuleCondition(type=ctype, n=n)


def _condition_label(c: RuleCondition) -> str:
    if c.type == "enemy_within":
        m = (c.cm or 0) / 100
        return f"적 {m:g}m 안"
    if c.type == "sister_hacking":
        return "해킹 중"
    if c.type == "hp_below":
        return f"{WHO_KO[c.who or 'any']} 체력<{c.pct}%"
    if c.type == "ammo_below":
        return f"탄약<{c.n}"
    if c.type == "player_downed":
        return f"{WHO_KO[c.who or 'any']} 다운"
    return f"배터리>{c.n}"


def rule_label(rule: Rule) -> str:
    """HUD 한 줄: '해킹 중 + 적 5m 안 → 미끼 (예비 20)'."""
    action = ACTION_KO[rule.do.action]
    if rule.do.action == "Shield" and rule.do.who:
        action = f"{WHO_KO[rule.do.who]} {action}"
    text = " + ".join(_condition_label(c) for c in rule.when) + f" → {action}"
    if rule.reserve_battery > 0:
        text += f" (예비 {rule.reserve_battery})"
    if rule.repeat:
        text += " [반복]"
    return text


def build_rule(args: dict[str, Any], state: GameState) -> tuple[Optional[Rule], Optional[str]]:
    """RegisterRule 인자(또는 Advise 제안 1개) → 검증·정규화한 Rule. 실패면 (None, 사유). 개수 제한은 보지 않는다."""
    # 조건: 목록이 비면 형식 오류, 화이트리스트 밖이 하나라도 있으면 UNSUPPORTED_CONDITION
    raw_conditions = args.get("conditions")
    if not isinstance(raw_conditions, list) or not raw_conditions:
        return None, "INVALID_RULE"
    conditions: list[RuleCondition] = []
    for raw in raw_conditions:
        cond = _normalize_condition(raw, state)
        if cond is None:
            return None, "UNSUPPORTED_CONDITION"
        if cond not in conditions:
            conditions.append(cond)
    if len(conditions) > MAX_CONDITIONS:
        return None, "TOO_MANY_CONDITIONS"

    # 행동: 실행 함수 9개 중 하나(Chat·규칙 함수 제외)
    action = args.get("action")
    if action not in ACTION_KO:
        return None, "INVALID_RULE"
    who = _str(args.get("who"))
    action_who: Optional[str] = who if who in ("brother", "sister") else None
    if action == "Shield" and action_who is None:
        action_who = state.speaker
    if action == "QuickReboot" and action_who is None:
        downed = [c.who for c in conditions if c.type == "player_downed" and c.who in ("brother", "sister")]
        action_who = downed[0] if downed else None
    if action not in ("Shield", "QuickReboot"):
        action_who = None

    reserve = _int(args.get("reserve_battery")) or 0
    rule = Rule(when=conditions, do=RuleAction(action=action, who=action_who),
                reserve_battery=max(0, min(90, reserve)), repeat=bool(args.get("repeat") is True))
    rule.label = rule_label(rule)
    return rule, None


def register_rule(args: dict[str, Any], state: GameState, dialogue: str,
                  heard: Optional[str]) -> CommandResponse:
    # 1. 개수 제한(UE도 막지만 서버가 먼저 말해 준다)
    if len(state.active_rules) >= MAX_RULES:
        return _reject("RULE_LIMIT", state, heard)
    # 2. 조건·행동 검증
    rule, reason = build_rule(args, state)
    if rule is None:
        return _reject(reason or "INVALID_RULE", state, heard)
    return CommandResponse(status="rule", rule_op="register", rule=rule,
                           dialogue=dialogue or f"알겠어요! {rule.label}, 기억할게요.",
                           command=None, battery_after=state.battery, heard=heard)


def manage_rules(args: dict[str, Any], state: GameState, dialogue: str,
                 heard: Optional[str]) -> CommandResponse:
    op = _str(args.get("op")) or "list"
    rules = state.active_rules
    if op in ("accept", "decline"):
        return answer_proposal(op == "accept", state, dialogue, heard)
    if op == "cancel":
        if not rules:
            return _reject("NO_RULE", state, heard)
        index = _int(args.get("index")) or 0
        if index < 0 or index > len(rules):
            return _reject("NO_RULE", state, heard)
        if index == 0:
            rule_id, line = 0, "규칙을 전부 지웠어요."
        else:
            rule_id, line = rules[index - 1].id, f"{index}번 규칙을 지웠어요."
        return CommandResponse(status="rule", rule_op="cancel", rule_id=rule_id,
                               dialogue=dialogue or line, command=None,
                               battery_after=state.battery, heard=heard)
    # 목록: LLM 대사 대신 서버가 정확히 만든다(줄마다 다음 말풍선)
    if not rules:
        line = "지금 등록된 규칙이 없어요."
    else:
        rows = [f"{i}) {r.label}" + (" — 보류 중" if r.held else "") for i, r in enumerate(rules, 1)]
        line = "지금 규칙은 이거예요.\n" + "\n".join(rows)
    return CommandResponse(status="rule", rule_op="list", dialogue=line, command=None,
                           battery_after=state.battery, heard=heard)


def answer_proposal(accept: bool, state: GameState, dialogue: str, heard: Optional[str]) -> CommandResponse:
    """기능 C. 제안한 작전(state.pending_proposal) 수락·거절. 실제 등록은 UE가 보관 중인 제안 규칙으로 한다
    (UE가 빈 자리만큼 AddRule — 최종 권위는 UE). 서버는 제안 유무·자리만 확인하고 대사를 정한다."""
    pending = state.pending_proposal
    if not pending:
        return _reject("NO_PROPOSAL", state, heard)
    if not accept:
        return CommandResponse(status="rule", rule_op="decline", dialogue=dialogue or "알겠어요, 작전은 접어 둘게요.",
                               command=None, battery_after=state.battery, heard=heard)
    free = MAX_RULES - len(state.active_rules)
    if free <= 0:
        return _reject("RULE_LIMIT", state, heard)
    n = min(free, len(pending))
    line = (f"작전 개시! 규칙 {n}개 걸어 둘게요." if n == len(pending)
            else f"규칙 자리가 {n}개뿐이라 첫 작전만 걸게요.")
    # 수락 대사는 서버가 만든다(LLM이 등록 개수를 틀리게 말하지 않도록)
    return CommandResponse(status="rule", rule_op="accept", dialogue=line, command=None,
                           battery_after=state.battery, heard=heard)


def check_action(action: str, who: Optional[str], reserve_battery: int, state: GameState,
                 settings: Settings) -> ValidateResponse:
    """POST /validate: 규칙 발동 직전 기존 검증기(BUSY·조건·LOW_BATTERY) + 예비 배터리."""
    from .validator import LlmCall, costs, validate  # 순환 import 방지
    cost = costs(settings)[action]
    args: dict[str, Any] = {"dialogue": RULE_FIRE_LINES[action]}
    if who:
        args["who"] = who
    result = validate(LlmCall(action, args), state, settings)
    if result.status == "ok" and result.battery_after < reserve_battery:
        return ValidateResponse(status="rejected", reason="RESERVE_BATTERY", battery_after=state.battery,
                                cost=cost, dialogue="배터리를 남겨 두기로 했으니 아껴 둘게요.")
    return ValidateResponse(status="ok" if result.status == "ok" else "rejected",
                            dialogue=result.dialogue, command=result.command,
                            battery_after=result.battery_after, reason=result.reason, cost=cost)
