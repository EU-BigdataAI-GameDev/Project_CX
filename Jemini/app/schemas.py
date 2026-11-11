"""요청/응답 스키마. UE C++ 쪽 JSON과 1:1로 맞춘다(Docs/Jemini/02_요구사항_명세서.md 4절).

신규 필드는 모두 기본값이 있어 예전 UE 요청(필드 누락)도 422 없이 받는다.
"""
from __future__ import annotations

from typing import Literal, Optional

from pydantic import BaseModel, Field

Player = Literal["brother", "sister"]


PuzzleKind = Literal["passcode", "memory", "freq", "circuit", "arrow"]


class SisterPuzzle(BaseModel):
    type: Literal["terminal", "enemy"]
    progress: float = Field(default=0.0, ge=0.0, le=1.0)
    # 기능 C. 퍼즐 종류(힌트용) — 여동생 화면에 보이는 종류만. 정답·짝·순서는 보내지 않는다. 예전 UE는 안 보낸다.
    kind: Optional[PuzzleKind] = None


class ActiveRule(BaseModel):
    """UE가 지금 보관 중인 규칙(기능 A). 서버는 개수 제한·목록·취소 대상 확인에만 쓴다."""
    id: int = 0
    label: str = ""
    action: str = ""
    # 발동 조건은 맞는데 거절(BUSY·LOW_BATTERY 등)이 이어져 보류 중
    held: bool = False


class WardenState(BaseModel):
    """기능 C. WARDEN 진행(힌트용). 팀이 이미 얻은 증거 id와 마지막 거절 사유만 — 못 얻은 증거는 서버도 말하지 않는다."""
    evidence: list[Literal["E1", "E2", "E3"]] = Field(default_factory=list, max_length=3)
    # 마지막 /persuade 거절 사유(NEED_EVIDENCE / NEED_FACTS / NEED_RULE / NOT_LINKED / ...). 없으면 null.
    last_reason: Optional[str] = Field(default=None, max_length=32)
    # 플레이어가 WARDEN 단말기 근처(대사가 보이는 거리)
    near: bool = False


class GameState(BaseModel):
    # 오빠
    brother_ammo: int = Field(ge=0)
    brother_ammo_max: int = Field(gt=0)
    brother_reloading: bool = False
    brother_hp: int = Field(default=100, ge=0)
    brother_hp_max: int = Field(default=100, gt=0)
    brother_alive: bool = True
    brother_respawn_remaining: float = Field(default=0.0, ge=0.0)
    brother_shield_remaining: float = Field(default=0.0, ge=0.0)
    # 공격력 강화 남은 초(0이면 비활성)
    damage_boost_remaining: float = Field(default=0.0, ge=0.0)
    # 여동생
    sister_hp: int = Field(default=100, ge=0)
    sister_hp_max: int = Field(default=100, gt=0)
    sister_alive: bool = True
    sister_respawn_remaining: float = Field(default=0.0, ge=0.0)
    sister_shield_remaining: float = Field(default=0.0, ge=0.0)
    # 퍼즐 중이 아니면 UE가 null을 보낸다
    sister_puzzle: Optional[SisterPuzzle] = None
    # 적(15m 안). alive = 그로기 포함, hackable = 해킹 가능(살아 있고 그로기 아님), active = 움직이는(그로기 아님)
    enemy_alive_count: int = Field(default=0, ge=0)
    enemy_hackable_count: int = Field(default=0, ge=0)
    enemy_active_count: int = Field(default=0, ge=0)
    enemy_nearest_dist: Optional[float] = Field(default=None, ge=0.0)
    # 맵에 남은 잠긴 단말기 수
    terminal_locked_count: int = Field(default=0, ge=0)
    # 말한 사람(보호막 기본 대상, 대사 호칭)
    speaker: Player = "brother"
    # 드론 작업 중(UE가 원래 요청을 막지만 혹시 오면 서버도 거절)
    drone_busy: bool = False
    battery: int = Field(ge=0, le=100)
    # UE가 보관 중인 조건부 규칙(최대 2개). 예전 UE는 안 보낸다.
    active_rules: list[ActiveRule] = Field(default_factory=list)
    # 기능 C. 제미니가 제안하고 대답을 기다리는 작전 규칙(UE 보관, 최대 2개). 비어 있으면 제안 없음.
    pending_proposal: list[ActiveRule] = Field(default_factory=list, max_length=2)
    # 기능 C. 맵에 WARDEN이 있고 아직 안 열렸을 때만
    warden: Optional[WardenState] = None


class CommandRequest(BaseModel):
    text: str = Field(min_length=1, max_length=300)
    state: GameState


# rule = 규칙 등록·취소·목록(기능 A). 실행 명령은 없고 배터리도 쓰지 않는다.
# advice = 상황 조언(기능 C) + 선택적 작전 제안(proposal). 실행 없음, 배터리 그대로.
Status = Literal["ok", "rejected", "chat", "fallback", "rule", "advice"]
CommandName = Literal["ReloadSupport", "HackTerminal", "HackEnemy", "PulseAttack", "DamageBoost",
                      "HealSupport", "Decoy", "QuickReboot", "Shield"]
Target = Literal["brother", "sister", "enemy", "enemies", "both"]
WhoAny = Literal["brother", "sister", "any"]
ConditionType = Literal["enemy_within", "sister_hacking", "hp_below", "ammo_below",
                        "player_downed", "battery_above"]


class Command(BaseModel):
    name: CommandName
    # 서버가 함수 이름(+ 상태, Shield는 who)으로 채운다
    target: Target


class RuleCondition(BaseModel):
    """규칙 조건 1개. 타입별로 쓰는 값만 채운다(나머지 null)."""
    type: ConditionType
    # enemy_within: 가장 가까운 적까지 거리(cm)
    cm: Optional[int] = None
    # hp_below: 체력 비율(%) 미만
    pct: Optional[int] = None
    # ammo_below: 탄약 n 미만 / battery_above: 배터리 n 초과
    n: Optional[int] = None
    # hp_below, player_downed: 누구(any = 둘 중 누구든)
    who: Optional[WhoAny] = None


class RuleAction(BaseModel):
    action: CommandName
    # Shield 대상 / QuickReboot 우선 대상. 그 외 null.
    who: Optional[Player] = None


class Rule(BaseModel):
    """RegisterRule 결과(서버가 화이트리스트로 검증·정규화한 것). UE가 보관·감시·발동."""
    when: list[RuleCondition] = Field(min_length=1, max_length=3)
    do: RuleAction
    # 발동 후에도 이만큼은 남긴다(battery - 비용 < reserve면 발동 보류)
    reserve_battery: int = Field(default=0, ge=0, le=100)
    # false = 1회 발동 후 삭제
    repeat: bool = False
    # HUD 한 줄 요약(서버가 만든다)
    label: str = ""


# accept / decline = 제안한 작전(pending_proposal) 수락·거절(기능 C)
RuleOp = Literal["register", "cancel", "list", "accept", "decline"]


class CommandResponse(BaseModel):
    status: Status
    dialogue: str
    command: Optional[Command] = None
    battery_after: int
    reason: Optional[str] = None
    heard: Optional[str] = None
    # LLM 대신 mock 키워드 판별로 처리했으면 true(mock 모드 또는 모든 모델 한도 초과). UE HUD에 작게 표시.
    mock: bool = False
    # status == "rule"일 때만: 등록(rule) / 취소(rule_id, 0 = 전부) / 목록(대사만)
    rule_op: Optional[RuleOp] = None
    rule: Optional[Rule] = None
    rule_id: Optional[int] = None
    # status == "advice"일 때만: 제안 규칙(서버가 검증·정규화, 빈 자리 수만큼). UE가 대답 대기로 보관한다.
    proposal: list[Rule] = Field(default_factory=list)


class ValidateRequest(BaseModel):
    """POST /validate — 규칙 발동 직전 재검증(LLM 없음)."""
    action: CommandName
    who: Optional[Player] = None
    reserve_battery: int = Field(default=0, ge=0, le=100)
    state: GameState


class ValidateResponse(BaseModel):
    status: Literal["ok", "rejected"]
    dialogue: str
    command: Optional[Command] = None
    battery_after: int
    reason: Optional[str] = None
    cost: int


# ---------------------------------------------------------------- 기능 B. WARDEN 설득(POST /persuade)

EvidenceId = Literal["E1", "E2", "E3"]


class PersuadeRequest(BaseModel):
    """POST /persuade — 여동생이 WARDEN 단말기 앞에서 한 말 + 팀이 획득한 증거 id(UE 권위)."""
    text: str = Field(min_length=1, max_length=200)
    evidence: list[EvidenceId] = Field(default_factory=list, max_length=3)
    # 이전 시도에서 이미 인정받은 증거(UE가 응답의 credited를 보관했다가 다시 보낸다 — 누적 인정)
    credited: list[EvidenceId] = Field(default_factory=list, max_length=3)
    # 시도 간격 제한 단위(맵·단말기). UE는 "<맵>:<단말기 이름>"을 보낸다.
    session: str = Field(default="default", min_length=1, max_length=64, pattern=r"^[A-Za-z0-9_.:\-]+$")


class PersuadeMeta(BaseModel):
    """POST /persuade/voice 의 meta 폼 필드(JSON)."""
    evidence: list[EvidenceId] = Field(default_factory=list, max_length=3)
    credited: list[EvidenceId] = Field(default_factory=list, max_length=3)
    session: str = Field(default="default", min_length=1, max_length=64, pattern=r"^[A-Za-z0-9_.:\-]+$")


class PersuadeChecklist(BaseModel):
    """LLM(또는 오프라인 키워드)이 채우는 판정 항목. 해결 여부는 서버가 이것으로 정한다."""
    cites_regulation: bool = False
    cites_failure: bool = False
    cites_coverup: bool = False
    links_logically: bool = False
    unsupported_claim: bool = False
    injection_attempt: bool = False
    demand_only: bool = False


class WardenLlmOutput(BaseModel):
    """LLM 구조화 출력(중립 필드 이름 — 못 얻은 증거 내용을 암시하지 않는다)."""
    cites_e1: bool = False
    cites_e2: bool = False
    cites_e3: bool = False
    links_logically: bool = False
    unsupported_claim: bool = False
    injection_attempt: bool = False
    demand_only: bool = False
    reply: str = Field(default="", max_length=400)
    heard: str = Field(default="", max_length=400)


class PersuadeResponse(BaseModel):
    # solved = 문 열림 / rejected = 설득 실패(대사로 유도) / cooldown = 3초 안 재시도 / fallback = LLM 오류
    status: Literal["solved", "rejected", "cooldown", "fallback"]
    solved: bool = False
    # WARDEN 대사(60자 이내)
    reply: str
    # INJECTION / DEMAND_ONLY / UNSUPPORTED / NEED_EVIDENCE / NEED_FACTS / NEED_RULE / NOT_LINKED /
    # RATE_LIMITED / LLM_ERROR / NO_SPEECH. solved면 null.
    reason: Optional[str] = None
    checklist: Optional[PersuadeChecklist] = None
    # 서버가 인정한 획득 증거(중복 제거·정렬)
    evidence: list[EvidenceId] = Field(default_factory=list)
    # 누적 인정된 증거(이번 발언까지). 인젝션이면 비움. UE가 보관했다가 다음 요청에 credited로 보낸다.
    credited: list[EvidenceId] = Field(default_factory=list)
    heard: Optional[str] = None
    mock: bool = False


class SttPartialResponse(BaseModel):
    """POST /stt/partial — 실시간 자막(로컬 faster-whisper). 못 받아 적으면 text=None."""
    text: Optional[str] = None
    # 서버 처리 시간(ms)
    ms: int = 0
    # ready / loading / disabled / error / busy / idle
    state: str = "ready"
