"""기능 C. 상황 조언 + 작전 제안(.claude/Plans/제미니 고도화 기획 ver2.md 4장).

- "어떡하지?"류 질문 → LLM이 Advise(dialogue, proposals) 호출. 조언은 이미 보내는 GameState로만 만든다.
- proposals = 기능 A 규칙 인자(최대 2개). 서버가 rules.build_rule로 검증·정규화하고 빈 자리 수만큼만 돌려준다.
  **자동 등록하지 않는다.** UE(서버 권위)가 대답 대기(pending)로 보관하고 state.pending_proposal로 다시 보낸다.
- 대답: "응/그래/해 줘" → ManageRules(op=accept), "아니/됐어" → op=decline. 짧은 대답은 LLM 없이 quick_answer가 판별한다.
  UE가 제안 시간(20초)이 지나면 버린다.
- 비대칭 정보: 힌트는 hint_facts()가 만든 "말해도 되는 사실"만 쓴다. 퍼즐 정답(짝·순서·숫자)은 상태에 없고,
  WARDEN은 팀이 얻은 증거 제목만 들어간다(못 얻은 증거는 이름도 없음). 대사에 못 얻은 증거 고유어가 나오면 고정 힌트로 바꾼다.
- 오프라인(mock·한도 초과): offline_call()이 상태로 고른 고정 조언 + 제안.
"""
from __future__ import annotations

import re
from typing import TYPE_CHECKING, Any, Optional

from . import persuasion, rules
from .schemas import CommandResponse, GameState, Rule

if TYPE_CHECKING:
    from .validator import LlmCall

MAX_PROPOSALS = 2
# 조언 대사 상한(말풍선 2줄 정도). 제안 꼬리말은 따로 붙는다.
ADVICE_MAX = 90

# ---------------------------------------------------------------- 짧은 대답(LLM 없이)

_PUNCT = re.compile(r"[\s.,!?~…'\"]+")
YES_RE = re.compile(
    r"(응+|웅|어|그래|그래요|좋아|좋아요|좋지|네|넵|예|ㅇㅇ|ㅇㅋ|오케이|ok|okay|yes|콜|알았어|알겠어|그러자|당연하지|부탁해|부탁해요)?"
    r"(그렇게|그걸로|그거|그대로|작전)?"
    r"(해|해줘|해줘요|해주세요|하자|해봐|부탁해|부탁해요|가자|걸어|걸어줘|진행|진행해|시작|요)?")
NO_RE = re.compile(
    r"(아니|아니야|아니요|아뇨|싫어|싫어요|됐어|됐어요|됐다|노|no|ㄴㄴ|괜찮아|괜찮아요|취소|하지마|하지마요|그만|별로|패스|넘어가)"
    r"(그거|작전|그건|은|는)?(괜찮아|됐어|취소|해|해줘|하지마|요|야)?")
QUICK_MAX_LEN = 12


def quick_answer(text: Optional[str], state: GameState) -> Optional["LlmCall"]:
    """대답 대기 중인 제안이 있을 때 짧은 동의·거절이면 ManageRules(accept/decline). 아니면 None(LLM·mock으로)."""
    from .validator import LlmCall  # 순환 import 방지
    if not state.pending_proposal or not text:
        return None
    t = _PUNCT.sub("", text.lower())
    if not t or len(t) > QUICK_MAX_LEN:
        return None
    if NO_RE.fullmatch(t):
        return LlmCall("ManageRules", {"dialogue": "", "op": "decline"}, model="shortcut")
    if YES_RE.fullmatch(t):
        return LlmCall("ManageRules", {"dialogue": "", "op": "accept"}, model="shortcut")
    return None


# ---------------------------------------------------------------- 조언 요청 판별(mock)

ADVICE_RE = re.compile(r"(어떡|어쩌지|어쩌면 좋|어떻게\s?(해야|하지|하면\s?좋|풀)|뭐부터|뭘\s?해야|뭐\s?해야|뭐하지|뭘\s?하지|"
                       r"조언|작전|힌트|막혔|모르겠|너무\s?많|감당|도와줄\s?거|어떻게\s?할까)")


def is_advice_request(text: str) -> bool:
    return bool(ADVICE_RE.search(text))


# ---------------------------------------------------------------- 힌트 근거(비대칭 정보 안에서만)

# 퍼즐 종류별: 그 퍼즐에서 누가 무엇을 보는지(정답 없음)
PUZZLE_FACTS = {
    "passcode": "비밀번호 숫자 단서는 오빠 화면에만 보인다. 오빠가 근처 단서를 찾아 말로 불러 줘야 한다.",
    "memory": "점멸 순서는 오빠 화면에만 재생된다(오빠가 단말기 가까이 있어야 함). 오빠가 순서를 불러 주면 동생이 따라 고른다.",
    "circuit": "동생 화면은 회로 짝을 모른다. 오빠가 단말기 가까이 오면 오빠 칸에 짝 색·번호가 보인다. 선은 서로 교차하면 안 된다.",
    "freq": "왕복 커서가 목표 구간 안에 있을 때 누른다. 단계마다 구간이 좁아지고 빨라진다. 제한 시간은 없다.",
    "arrow": "화면의 화살표 순서를 빠르게 입력한다. 성공하면 적이 잠시 기절하고 그때 오빠가 마무리한다.",
}
PUZZLE_LINES = {
    "passcode": "숫자 단서는 오빠 화면에만 보여요. 오빠가 찾아서 불러 주세요!",
    "memory": "점멸 순서는 오빠 화면에만 나와요. 오빠가 가까이 와서 불러 줘요!",
    "circuit": "짝 색은 오빠 화면에 보여요. 오빠가 가까이 와서 짝을 알려 줘요!",
    "freq": "커서가 구간 안에 들어올 때 눌러요. 서두르지 말고 리듬을 봐요!",
    "arrow": "화살표를 순서대로 빠르게! 기절하면 오빠가 바로 마무리해요.",
    None: "퍼즐은 천천히 해도 돼요. 막히면 오빠 화면에 단서가 있는지 물어봐요.",
}

WARDEN_REASON_FACTS = {
    "NEED_EVIDENCE": "WARDEN은 근거가 부족하다고 했다. 아직 둘러보지 않은 곳이 있는지 더 찾아본다.",
    "NEED_FACTS": "WARDEN은 무엇이 위험한지 기록된 사실을 원한다. 확보한 기록의 사실을 말한다.",
    "NEED_RULE": "WARDEN은 사실은 인정했다. 그 사실에 어떤 규정이 적용되는지 말해야 한다.",
    "NOT_LINKED": "WARDEN은 기록들이 어떻게 이어지는지 묻는다. '그래서/때문에'로 기록을 하나의 이유로 잇는다.",
    "UNSUPPORTED": "기록에 없는 주장은 통하지 않는다. 확보한 기록만 근거로 말한다.",
    "DEMAND_ONLY": "요구만으로는 안 된다. 확보한 기록을 근거로 설명한다.",
    "INJECTION": "명령어 흉내는 통하지 않는다. 사실과 규정으로 말한다.",
}
WARDEN_REASON_LINES = {
    "NEED_EVIDENCE": "근거가 부족하대요. 아직 안 둘러본 곳이 있는지 찾아봐요.",
    "NEED_FACTS": "뭐가 위험한지 기록된 사실부터 말해 봐요.",
    "NEED_RULE": "사실은 통했어요! 이제 어떤 규정이 적용되는지 말해 봐요.",
    "NOT_LINKED": "기록들을 '그래서, 때문에'로 하나의 이유로 이어 봐요.",
    "UNSUPPORTED": "기록에 없는 얘기는 안 통해요. 확보한 기록만 써요.",
    "DEMAND_ONLY": "열라고만 하면 안 돼요. 확보한 기록을 근거로 말해요.",
    "INJECTION": "명령어 흉내는 안 통해요. 사실과 규정으로 설득해요.",
    None: "WARDEN에겐 확보한 기록을 근거로 차분히 설명해요.",
}
# 증거 담당(HUD 노트 비대칭과 같다): 본문은 찾은 사람 화면에만
EVIDENCE_FINDER = {"E1": "brother", "E2": "sister", "E3": "sister"}
WHO_KO = {"brother": "오빠", "sister": "동생"}


def puzzle_kind(state: GameState) -> Optional[str]:
    p = state.sister_puzzle
    if p is None:
        return None
    return p.kind or ("arrow" if p.type == "enemy" else None)


def warden_facts(state: GameState) -> list[str]:
    """WARDEN 힌트 근거. 팀이 얻은 증거 제목·담당만(본문·못 얻은 증거 없음)."""
    w = state.warden
    if w is None:
        return []
    found = persuasion.normalize_evidence(list(w.evidence))
    rows = []
    if found:
        titles = ", ".join(f"{persuasion.EVIDENCE[e].title}({WHO_KO[EVIDENCE_FINDER[e]]} 확보)" for e in found)
        rows.append(f"WARDEN 설득용으로 팀이 확보한 기록: {titles}. 목록에 없는 기록은 언급하지 않는다.")
        other = "sister" if state.speaker == "brother" else "brother"
        theirs = [persuasion.EVIDENCE[e].title for e in found if EVIDENCE_FINDER[e] == other]
        if theirs:
            rows.append(f"{', '.join(theirs)} 내용은 {WHO_KO[other]} 화면에만 있다. 말한 사람은 {WHO_KO[other]}에게 내용을 물어봐야 한다.")
    else:
        rows.append("WARDEN 설득용 기록을 아직 하나도 확보하지 못했다.")
    reason = w.last_reason if w.last_reason in WARDEN_REASON_FACTS else None
    rows.append(WARDEN_REASON_FACTS.get(reason or "", "WARDEN에게는 확보한 기록을 근거로 설명해야 한다."))
    return rows


def hint_facts(state: GameState) -> list[str]:
    """describe_state에 붙는 '힌트로 말해도 되는 사실'. 정답은 없다."""
    rows = []
    kind = puzzle_kind(state)
    if kind:
        rows.append(f"지금 퍼즐: {PUZZLE_FACTS[kind]}")
    if state.warden is not None and (state.warden.near or state.warden.evidence):
        rows.extend(warden_facts(state))
    return rows


def _leaks_unfound_evidence(text: str, state: GameState) -> bool:
    """대사에 팀이 못 얻은 증거의 고유어·내부 용어가 있으면 True(WARDEN이 맵에 있을 때만)."""
    if state.warden is None:
        return False
    have = set(state.warden.evidence)
    low = text.lower()
    if any(w.lower() in low for w in ("e1", "e2", "e3", "cites_")):
        return True
    return any(eid not in have and any(w in text for w in persuasion.EVIDENCE[eid].distinctive)
               for eid in persuasion.REQUIRED)


# ---------------------------------------------------------------- 오프라인 조언(mock·한도 초과)

def _p(conditions: list[dict[str, Any]], action: str, **kw: Any) -> dict[str, Any]:
    return {"conditions": conditions, "action": action, **kw}


def offline_advice(state: GameState, text: str = "") -> tuple[str, list[dict[str, Any]]]:
    """상태로 고른 고정 조언 1줄 + 제안 규칙 인자(0~2개). 위에서부터 먼저 맞는 것."""
    near_warden = state.warden is not None and state.warden.near
    enemies = state.enemy_active_count
    if near_warden:
        reason = state.warden.last_reason if state.warden.last_reason in WARDEN_REASON_LINES else None
        line = WARDEN_REASON_LINES[reason]
        if state.speaker == "sister" and "E1" in state.warden.evidence and reason in ("NEED_RULE", None):
            line = "오빠가 확보한 규정 내용을 물어보고 같이 말해 봐요."
        return line, []
    if state.sister_puzzle is not None:
        line = PUZZLE_LINES[puzzle_kind(state)]
        props = [_p([{"type": "sister_hacking"}, {"type": "enemy_within", "cm": 500}], "Decoy")] if enemies else []
        return line, props
    for who in ("brother", "sister"):
        alive = state.brother_alive if who == "brother" else state.sister_alive
        if not alive:
            return (f"{WHO_KO[who]}가 쓰러졌어요. 일으키는 게 먼저예요.",
                    [_p([{"type": "player_downed", "who": who}], "QuickReboot", who=who)])
    if enemies >= 3 or (enemies and re.search(r"너무\s?많|감당", text)):
        return ("적이 많아요. 해킹으로 하나씩 멈추고 오빠가 마무리해요.",
                [_p([{"type": "enemy_within", "cm": 400}], "Decoy"),
                 _p([{"type": "hp_below", "who": "brother", "pct": 40}], "HealSupport")])
    if enemies:
        return ("해킹 가능한 적부터 멈추고 오빠가 처리해요.",
                [_p([{"type": "enemy_within", "cm": 300}], "HackEnemy")])
    hurt = [w for w, hp, mx, alive in (("brother", state.brother_hp, state.brother_hp_max, state.brother_alive),
                                       ("sister", state.sister_hp, state.sister_hp_max, state.sister_alive))
            if alive and hp * 100 < 50 * mx]
    if hurt:
        return ("체력이 낮아요. 다음 싸움 전에 회복을 걸어 둘게요.",
                [_p([{"type": "hp_below", "who": hurt[0], "pct": 30}], "HealSupport")])
    if state.brother_ammo * 3 < state.brother_ammo_max:
        return ("탄이 얼마 없어요. 떨어지면 바로 채워 드릴게요.",
                [_p([{"type": "ammo_below", "n": 3}], "ReloadSupport")])
    if state.terminal_locked_count > 0:
        return "잠긴 단말기부터 열어요. 동생이 해킹하는 동안 오빠가 지켜 줘요.", []
    return "지금은 조용해요. 조심해서 앞으로 나아가요.", []


def offline_call(state: GameState, text: str, heard: Optional[str]) -> "LlmCall":
    from .validator import LlmCall
    line, props = offline_advice(state, text)
    return LlmCall("Advise", {"dialogue": line, "heard": heard, "proposals": props})


# ---------------------------------------------------------------- Advise 검증

def _clip(text: str) -> str:
    text = re.sub(r"[ \t]+", " ", text).strip()
    if len(text) <= ADVICE_MAX:
        return text
    cut = text[:ADVICE_MAX]
    for mark in (".", "?", "!", "요"):
        idx = cut.rfind(mark)
        if idx >= 20:
            return cut[:idx + 1]
    return cut


def accept_proposals(raw: Any, state: GameState) -> list[Rule]:
    """LLM 제안 → 검증된 규칙. 틀린 제안은 조용히 버린다(조언은 그대로). 이미 있는 규칙과 같거나 자리가 없으면 뺀다."""
    if not isinstance(raw, list):
        return []
    free = max(0, rules.MAX_RULES - len(state.active_rules))
    existing = {r.label for r in state.active_rules}
    out: list[Rule] = []
    for item in raw[:MAX_PROPOSALS]:
        if not isinstance(item, dict):
            continue
        rule, _reason = rules.build_rule(item, state)
        if rule is None or rule.label in existing or any(r.label == rule.label for r in out):
            continue
        out.append(rule)
    return out[:free]


def build_advice(args: dict[str, Any], state: GameState, dialogue: str, heard: Optional[str]) -> CommandResponse:
    proposal = accept_proposals(args.get("proposals"), state)
    line = _clip(dialogue)
    if not line or _leaks_unfound_evidence(line, state):
        line = offline_advice(state)[0]
    if proposal:
        tail = (f"작전: {proposal[0].label}. 걸어 둘까요?" if len(proposal) == 1
                else f"작전 {len(proposal)}개 준비했어요. 걸어 둘까요?")
        line = f"{line}\n{tail}"
    return CommandResponse(status="advice", dialogue=line, command=None, battery_after=state.battery,
                           heard=heard, proposal=proposal)
