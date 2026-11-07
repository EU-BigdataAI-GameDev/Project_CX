"""모의 모드: Gemini 없이 키워드 규칙으로 함수 호출을 흉내 낸다. 이후 검증·로그 경로는 실제와 같다.

규칙은 위에서부터 먼저 맞는 것(기획 9-1절). 겹치는 단어가 있어 순서가 중요하다
(예: "공격력" → DamageBoost가 "공격해"보다 먼저, "보호막" → Shield가 "막아"보다 먼저).
"""
from __future__ import annotations

from typing import Optional

from .schemas import GameState
from .validator import LlmCall

DAMAGE_BOOST_WORDS = ("공격력", "데미지", "화력", "강화", "세게")
SHIELD_WORDS = ("보호막", "지켜", "보호", "실드")
REBOOT_WORDS = ("살려", "부활", "일으켜", "재부팅")
DECOY_WORDS = ("어그로", "유인", "시선", "관심", "데려가")
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


def mock_call(text: Optional[str], state: Optional[GameState] = None, *, voice: bool = False) -> LlmCall:
    heard = text if voice else None
    t = (text or "").lower()
    if not t:
        return LlmCall("Chat", {"dialogue": "잘 안 들렸어요. 다시 말해 줄래요?", "heard": heard})
    name = _pick(t, state)
    if name is None:
        return LlmCall("Chat", {"dialogue": "(모의 모드) 무슨 일이에요? 도울 게 있으면 말해요.", "heard": heard})
    args = {"dialogue": MOCK_LINES[name], "heard": heard}
    if name == "Shield" and (who := _shield_who(t)):
        args["who"] = who
    return LlmCall(name, args)
