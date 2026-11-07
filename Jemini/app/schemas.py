"""요청/응답 스키마. UE C++ 쪽 JSON과 1:1로 맞춘다(Docs/Jemini/02_요구사항_명세서.md 4절).

신규 필드는 모두 기본값이 있어 예전 UE 요청(필드 누락)도 422 없이 받는다.
"""
from __future__ import annotations

from typing import Literal, Optional

from pydantic import BaseModel, Field

Player = Literal["brother", "sister"]


class SisterPuzzle(BaseModel):
    type: Literal["terminal", "enemy"]
    progress: float = Field(default=0.0, ge=0.0, le=1.0)


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


class CommandRequest(BaseModel):
    text: str = Field(min_length=1, max_length=300)
    state: GameState


Status = Literal["ok", "rejected", "chat", "fallback"]
CommandName = Literal["ReloadSupport", "HackTerminal", "HackEnemy", "PulseAttack", "DamageBoost",
                      "HealSupport", "Decoy", "QuickReboot", "Shield"]
Target = Literal["brother", "sister", "enemy", "enemies", "both"]


class Command(BaseModel):
    name: CommandName
    # 서버가 함수 이름(+ 상태, Shield는 who)으로 채운다
    target: Target


class CommandResponse(BaseModel):
    status: Status
    dialogue: str
    command: Optional[Command] = None
    battery_after: int
    reason: Optional[str] = None
    heard: Optional[str] = None
    # LLM 대신 mock 키워드 판별로 처리했으면 true(mock 모드 또는 모든 모델 한도 초과). UE HUD에 작게 표시.
    mock: bool = False
