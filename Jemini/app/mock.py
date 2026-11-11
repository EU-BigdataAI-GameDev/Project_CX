"""모의 모드: Gemini 없이 키워드 규칙으로 함수 호출을 흉내 낸다. 이후 검증·로그 경로는 실제와 같다.

규칙은 위에서부터 먼저 맞는 것(기획 9-1절). 겹치는 단어가 있어 순서가 중요하다
(예: "공격력" → DamageBoost가 "공격해"보다 먼저, "보호막" → Shield가 "막아"보다 먼저).
"""
from __future__ import annotations

import re
from typing import Optional

from . import advice
from .schemas import GameState
from .validator import LlmCall

DAMAGE_BOOST_WORDS = ("공격력", "데미지", "화력", "강화", "세게")
SHIELD_WORDS = ("보호막", "지켜", "보호", "실드")
REBOOT_WORDS = ("살려", "부활", "일으켜", "재부팅")
DECOY_WORDS = ("어그로", "유인", "시선", "관심", "데려가", "미끼")
HEAL_WORDS = ("힐", "체력", "회복", "치료", "아파")
RELOAD_WORDS = ("재장전", "장전", "탄", "총알", "reload", "ammo")
PULSE_WORDS = ("처치", "죽여", "없애", "펄스", "공격해")
ENEMY_WORDS = ("적", "거미", "로봇", "enemy")
ENEMY_HACK_VERBS = ("해킹", "무력화", "멈춰", "막아", "어떻게")
# 적 단어가 없어도 적 해킹으로 보는 동사("막아줘" — 적을 막는 뜻)
ENEMY_ONLY_VERBS = ("무력화", "멈춰", "막아")
HACK_WORDS = ("해킹", "퍼즐", "단말기", "뚫어", "비밀번호", "hack", "puzzle")

MOCK_LINES = {
    "DamageBoost": "레일건 출력 올려 드릴게요!",
    "Shield": "보호막 전개! 5초 버텨요!",
    "QuickReboot": "재부팅 신호 보낼게요, 일어나세요!",
    "Decoy": "제가 시선 끌게요, 지금이에요!",
    "HealSupport": "치료 시작할게요, 버텨요!",
    "ReloadSupport": "재장전 해드릴게요!",
    "PulseAttack": "펄스 발사! 물러서세요!",
    "HackEnemy": "적 회로에 침투할게요!",
    "HackTerminal": "제가 대신 풀게요, 3초만요!",
}


def _has(t: str, words: tuple[str, ...]) -> bool:
    return any(w in t for w in words)


def _shield_who(t: str) -> Optional[str]:
    if "동생" in t:
        return "sister"
    if "오빠" in t:
        return "brother"
    return None


def _pick(t: str, state: Optional[GameState]) -> Optional[str]:
    if _has(t, DAMAGE_BOOST_WORDS):
        return "DamageBoost"
    if _has(t, SHIELD_WORDS):
        return "Shield"
    if _has(t, REBOOT_WORDS):
        return "QuickReboot"
    if _has(t, DECOY_WORDS):
        return "Decoy"
    if _has(t, HEAL_WORDS):
        return "HealSupport"
    if _has(t, RELOAD_WORDS):
        return "ReloadSupport"
    if _has(t, PULSE_WORDS):
        return "PulseAttack"
    if (_has(t, ENEMY_WORDS) and _has(t, ENEMY_HACK_VERBS)) or _has(t, ENEMY_ONLY_VERBS):
        return "HackEnemy"
    if _has(t, HACK_WORDS):
        # "해킹해줘"만 있으면 상태로: 화살표 퍼즐 → 적, 단말기 퍼즐 → 단말기,
        # 퍼즐 없음 → 해킹 가능한 적이 있으면 적, 없으면 다음 잠긴 단말기(기획 3절)
        if state is not None and state.sister_puzzle is not None:
            return "HackEnemy" if state.sister_puzzle.type == "enemy" else "HackTerminal"
        if state is not None and state.enemy_hackable_count > 0 and not _has(t, ("퍼즐", "단말기", "비밀번호")):
            return "HackEnemy"
        return "HackTerminal"
    return None


# ---- 기능 A. 규칙(조건부 지시) 키워드 판별 — 단순한 문장만. 복합·부정 조건은 LLM의 몫(비교 실험 베이스라인).
RULE_WORDS = ("규칙", "조건")
RULE_CANCEL_WORDS = ("취소", "지워", "삭제", "없애", "그만", "해제")
RULE_LIST_WORDS = ("목록", "뭐", "알려", "보여", "확인", "몇")
# 조건 표지: "~면 ", "~동안", "~할 때" 등. 마지막 표지 뒤가 행동 부분.
CONDITIONAL = re.compile(r"(면(?=[\s,.]|$)|동안|때마다|할\s?때|일\s?때|하는\s?중에?|되면)")
# "~하면 좋겠어/돼/고마워"는 조건이 아니다
NOT_CONDITIONAL = re.compile(r"면\s*(좋겠|돼|되겠|고마|감사)")
ENEMY_NEAR_WORDS = ("가까", "다가", "오면", "근처", "안으로", "붙으면", "접근")
DOWN_WORDS = ("쓰러지", "쓰러져", "죽으면", "죽으", "다운")
UNDER_WORDS = ("아래", "이하", "밑", "미만", "떨어지", "안 되")
REPEAT_WORDS = ("매번", "그때마다", "때마다", "계속", "항상")


def _who_in(t: str, default: Optional[str]) -> Optional[str]:
    if "동생" in t:
        return "sister"
    if "오빠" in t:
        return "brother"
    if "누구" in t or "둘 중" in t:
        return "any"
    if re.search(r"(^|\s)(내가|나|제가)\s", t):
        return "self"
    return default


def _rule_conditions(cond: str) -> list[dict]:
    """조건 부분에서 화이트리스트 조건을 뽑는다."""
    out: list[dict] = []
    if "해킹" in cond and _has(cond, ("동안", "중", "하는")):
        out.append({"type": "sister_hacking"})
    if _has(cond, ENEMY_WORDS) and (_has(cond, ENEMY_NEAR_WORDS) or re.search(r"\d+\s*(m|미터|cm)", cond)):
        cm = 500
        if m := re.search(r"(\d+(?:\.\d+)?)\s*(m|미터)(?!m)", cond):
            cm = int(float(m.group(1)) * 100)
        elif m := re.search(r"(\d+)\s*(cm|센티)", cond):
            cm = int(m.group(1))
        out.append({"type": "enemy_within", "cm": cm})
    if "체력" in cond and (m := re.search(r"체력\D{0,8}?(\d+)", cond)) and _has(cond, UNDER_WORDS):
        out.append({"type": "hp_below", "pct": int(m.group(1)), "who": _who_in(cond, None)})
    if _has(cond, ("탄", "총알")):
        if m := re.search(r"(탄약|탄|총알)\D{0,8}?(\d+)", cond):
            out.append({"type": "ammo_below", "n": int(m.group(2))})
        elif _has(cond, ("없으면", "떨어지면", "바닥")):
            out.append({"type": "ammo_below", "n": 1})
    if _has(cond, DOWN_WORDS):
        out.append({"type": "player_downed", "who": _who_in(cond, "any")})
    if m := re.search(r"배터리\D{0,6}?(\d+)\D{0,4}?(넘|이상|초과|위|남아)", cond):
        out.append({"type": "battery_above", "n": int(m.group(1))})
    return out


def _mock_rule(t: str, state: Optional[GameState], heard: Optional[str]) -> Optional[LlmCall]:
    """규칙 관련 문장이면 RegisterRule / ManageRules, 아니면 None(기존 판별로)."""
    if "규칙" in t and _has(t, RULE_CANCEL_WORDS):
        index = 1 if re.search(r"(1\s*번|첫)", t) else 2 if re.search(r"(2\s*번|두\s*번째|둘째)", t) else 0
        return LlmCall("ManageRules", {"dialogue": "", "heard": heard, "op": "cancel", "index": index})
    if "규칙" in t and _has(t, RULE_LIST_WORDS):
        return LlmCall("ManageRules", {"dialogue": "", "heard": heard, "op": "list"})
    if NOT_CONDITIONAL.search(t):
        return None
    markers = list(CONDITIONAL.finditer(t))
    if not markers:
        return None
    split = markers[-1].end()
    cond, rest = t[:split], t[split:]
    conditions = _rule_conditions(cond)
    if not conditions:
        if not _has(t, RULE_WORDS):
            return None
        # 규칙을 말했는데 아는 조건이 없다 → 서버가 UNSUPPORTED_CONDITION으로 거절
        conditions = [{"type": "unsupported"}]
    speaker = state.speaker if state is not None else "brother"
    for c in conditions:
        if c.get("who") == "self":
            c["who"] = speaker
    action = _pick(rest, state) or _pick(t.replace("체력", "").replace("해킹", ""), state)
    if action is None:
        return LlmCall("Chat", {"dialogue": "(모의 모드) 그때 뭘 하면 될까요?", "heard": heard})
    args = {"dialogue": "", "heard": heard, "conditions": conditions, "action": action,
            "repeat": _has(t, REPEAT_WORDS)}
    if m := re.search(r"배터리\D{0,6}?(\d+)\D{0,6}?(남겨|남기|아껴|남도록|남게)", t):
        args["reserve_battery"] = int(m.group(1))
    if action == "Shield" and (who := _shield_who(rest)):
        args["who"] = who
    return LlmCall("RegisterRule", args)


def mock_call(text: Optional[str], state: Optional[GameState] = None, *, voice: bool = False) -> LlmCall:
    heard = text if voice else None
    t = (text or "").lower()
    if not t:
        return LlmCall("Chat", {"dialogue": "잘 안 들렸어요. 다시 말해 줄래요?", "heard": heard})
    # 기능 C. 제안 대기 중 짧은 대답 → 수락·거절, "어떡하지?"류 → 상태로 고른 고정 조언 + 제안
    if state is not None and (answer := advice.quick_answer(t, state)):
        answer.args["heard"] = heard
        answer.model = ""
        return answer
    if state is not None and advice.is_advice_request(t):
        return advice.offline_call(state, t, heard)
    if rule := _mock_rule(t, state, heard):
        return rule
    name = _pick(t, state)
    if name is None:
        return LlmCall("Chat", {"dialogue": "(모의 모드) 무슨 일이에요? 도울 게 있으면 말해요.", "heard": heard})
    args = {"dialogue": MOCK_LINES[name], "heard": heard}
    if name == "Shield" and (who := _shield_who(t)):
        args["who"] = who
    return LlmCall(name, args)
