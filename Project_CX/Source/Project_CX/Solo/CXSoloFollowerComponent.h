// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXSoloFollowerComponent.generated.h"

class ACharacter;
class UBrotherCombatComponent;
class USisterHackComponent;

/**
 *  솔로 모드(UCXSoloSubsystem)에서 "조작하지 않는" 캐릭터를 움직이는 작은 AI — 폰에 런타임으로 붙는다.
 *
 *  - 따라다니기: 조작 캐릭터와 수평(XY) 거리 FollowDistance(1m)를 유지. 목표가 더 높거나 앞에 낭떠러지/막힘이 있으면 점프,
 *    TeleportDistance(15m)보다 멀어졌거나 StuckTeleportTime초 넘게 막히면 조작 캐릭터 옆으로 순간이동.
 *    이동은 AddMovementInput(월드 절대축 — 플레이어와 같은 경로)만 쓴다. 키보드 입력은 받지 않는다(UCXGameViewportClient가 돌린다).
 *  - 여동생 AI: 해킹 가능한 단말기·적(USisterHackComponent의 같은 탐지/세션 경로)이 범위 안이면 세션을 열고,
 *    잠깐 뒤(단말기 TerminalHackDelay / 적 EnemyHackDelay) 제미니 해킹 대행과 같은 성공 경로로 끝낸다(퍼즐 입력 생략). WARDEN은 대상 아님.
 *  - 오빠 AI: 자동 조준 사거리 안의 가장 가까운 적을 UBrotherCombatComponent의 평소 발사 경로로 쏜다(연사 간격·탄약 그대로, 탄이 0이면 장전).
 *  - 서버(권위) 월드에서만 동작하고, 게임 오버/클리어/맵 이동 중이거나 이 캐릭터가 쓰러져 있으면 쉰다.
 */
UCLASS(ClassGroup = (Custom))
class UCXSoloFollowerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXSoloFollowerComponent();

	/** 폰에 없으면 붙인다. */
	static UCXSoloFollowerComponent* EnsureOn(APawn* Pawn);

	/** bAI = true면 AI로 동작(Target = 따라갈 조작 캐릭터). false면 아무것도 하지 않는다(조작 중). */
	void Configure(bool bInAI, APawn* InTarget);
	bool IsAIControlled() const { return bAI; }

	/** 솔로를 끌 때: 진행 중인 자동 해킹 세션·조준을 정리한다. */
	void ResetAutomation();

	UPROPERTY(EditAnywhere, Category = "Solo")
	float FollowDistance = 100.0f;

	/** FollowDistance + 이 값보다 멀어지면 움직이기 시작, FollowDistance 안이면 멈춘다. */
	UPROPERTY(EditAnywhere, Category = "Solo")
	float StartMoveMargin = 40.0f;

	UPROPERTY(EditAnywhere, Category = "Solo")
	float TeleportDistance = 1500.0f;

	UPROPERTY(EditAnywhere, Category = "Solo")
	float StuckTeleportTime = 4.0f;

	/** 단말기 / 적 해킹이 시작된 뒤 성공 처리까지(초). */
	UPROPERTY(EditAnywhere, Category = "Solo")
	float TerminalHackDelay = 1.5f;

	UPROPERTY(EditAnywhere, Category = "Solo")
	float EnemyHackDelay = 1.0f;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void TickFollow(float DeltaTime, ACharacter* Self, const APawn* Leader);
	void TickAutoHack(float DeltaTime, USisterHackComponent* Hack);
	void TickAutoFire(UBrotherCombatComponent* Combat);
	void TryJump(ACharacter* Self);
	bool HasGroundAhead(const ACharacter* Self, const FVector& Dir) const;
	void TeleportNextToLeader(ACharacter* Self, const APawn* Leader);

	bool bAI = false;
	TWeakObjectPtr<APawn> Target;

	bool bMoving = false;
	float StuckTime = 0.0f;
	double LastJumpTime = -10.0;
	double JumpReleaseTime = 0.0;
	double LastTeleportTime = -10.0;

	// 자동 해킹
	float HackCheckTimer = 0.0f;
	float HackElapsed = 0.0f;
	bool bHackCompleted = false;
};
