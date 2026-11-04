// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CXHackTarget.generated.h"

class APawn;

UINTERFACE(MinimalAPI, NotBlueprintable)
class UCXHackTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 *  여동생이 해킹할 수 있는 "단말기가 아닌" 대상(적 등) — 해킹 시스템 기획 10장(적 해킹 → 그로기 3초).
 *
 *  - ACoopHUD가 여동생 칸에서 이 인터페이스를 구현한 액터에 단말기와 같은 마름모 마커 + 거리,
 *    GetHackInteractRange() 안이고 해킹 가능(IsHackTargetAvailable + 여동생 쿨타임 아님)이면 `[X] HACK`을 그린다.
 *    그로기 중(GetHackStunRemaining > 0)이면 두 플레이어 칸 모두에 SYSTEM DOWN 마커 + 남은 시간 바를 그린다.
 *  - 흐름: USisterHackComponent::TryHackEnemy → 여동생 로컬에서 주파수 동조 퍼즐 → 성공 시 서버에서 ApplyHackSuccess.
 *  - F/X 우선순위: 해킹 세션 > 단말기 > 문 버튼 > 이 대상(UCoopInteractComponent).
 */
class ICXHackTarget
{
	GENERATED_BODY()

public:
	/** 지금 해킹을 시작할 수 있는지(살아 있음, 그로기 중 아님). */
	virtual bool IsHackTargetAvailable() const = 0;

	/** 이 거리(cm) 안이면 해킹 가능(`[X] HACK`). 단말기 InteractRange와 같은 300cm가 기본. */
	virtual float GetHackInteractRange() const { return 300.0f; }

	/** 해킹 퍼즐 성공(서버 전용). 그로기 등 효과를 적용했으면 true. */
	virtual bool ApplyHackSuccess(APawn* Hacker) { return false; }

	/** 그로기 남은 시간(초, 모든 머신 — 복제값 기준). 그로기가 아니면 0. */
	virtual float GetHackStunRemaining() const { return 0.0f; }

	/** 그로기 전체 시간(초) — HUD 카운트다운 바 비율용. */
	virtual float GetHackStunDuration() const { return 0.0f; }

	/** 해킹 패널 헤더에 쓰는 대상 이름(영문 — Canvas 폰트에 한글 없음). */
	virtual FString GetHackTargetName() const { return TEXT("HOSTILE_UNIT"); }
};
