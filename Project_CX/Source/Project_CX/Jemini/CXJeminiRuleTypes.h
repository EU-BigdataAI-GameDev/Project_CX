// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CXJeminiRuleTypes.generated.h"

/**
 *  제미니 조건부 지시(규칙) 데이터 — .claude/Plans/제미니 고도화 기획 ver2.md 2장(기능 A).
 *  서버 RegisterRule 응답의 rule JSON(Docs/Jemini/02_요구사항_명세서.md 4-1절)과 1:1.
 */

/** 조건 1개. Type별로 쓰는 값만 의미가 있다. */
USTRUCT()
struct FCXJeminiRuleCondition
{
	GENERATED_BODY()

	/** enemy_within / sister_hacking / hp_below / ammo_below / player_downed / battery_above */
	UPROPERTY()
	FString Type;

	/** enemy_within = cm, hp_below = %, ammo_below = 발, battery_above = %. sister_hacking·player_downed는 0. */
	UPROPERTY()
	int32 Value = 0;

	/** hp_below·player_downed 대상: brother / sister / any. */
	UPROPERTY()
	FString Who;
};

/** 규칙 1개(서버 권위로 보관·감시·발동, 복제는 HUD용). */
USTRUCT()
struct FCXJeminiRule
{
	GENERATED_BODY()

	/** 1부터 증가(취소·목록 대상 식별). */
	UPROPERTY()
	int32 Id = 0;

	/** 모두 만족(AND), 1~3개. */
	UPROPERTY()
	TArray<FCXJeminiRuleCondition> When;

	/** 실행 함수 9개 중 하나(ReloadSupport ~ Shield). */
	UPROPERTY()
	FString Action;

	/** Shield 대상 / QuickReboot 우선 대상(brother / sister), 그 외 빈 값. */
	UPROPERTY()
	FString ActionWho;

	/** 발동 후에도 남길 배터리. */
	UPROPERTY()
	int32 ReserveBattery = 0;

	/** false = 1회 발동 후 삭제. */
	UPROPERTY()
	bool bRepeat = false;

	/** HUD 한 줄 요약(서버가 만든 한국어). */
	UPROPERTY()
	FString Label;

	/** 조건은 맞는데 거절이 이어져 보류 중(HUD "규칙 보류"). */
	UPROPERTY()
	bool bHeld = false;

	/** 보류 사유(LOW_BATTERY / RESERVE_BATTERY / BUSY / NO_CONNECTION / 검증기 사유). */
	UPROPERTY()
	FString HeldReason;

	// ---- 서버 전용 실행 상태(복제 안 함)

	/** 조건이 맞은 채 거절이 시작된 시각(음수 = 실패 중 아님). */
	UPROPERTY(NotReplicated)
	float FailSince = -1.0f;

	/** 이 시각 전에는 다시 시도하지 않는다(재시도 쿨다운·반복 간격). */
	UPROPERTY(NotReplicated)
	float NextTryTime = 0.0f;
};
