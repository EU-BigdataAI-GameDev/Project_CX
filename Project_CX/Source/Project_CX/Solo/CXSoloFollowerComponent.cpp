// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXSoloFollowerComponent.h"
#include "LocalCoopGameMode.h"
#include "CXPlayerLifeComponent.h"
#include "BrotherCombatComponent.h"
#include "SisterHackComponent.h"
#include "CoopInteractComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXSolo, Log, All);

namespace CXSoloFollow
{
	/** 목표가 이만큼(cm) 높고 가까우면 점프. */
	constexpr float JumpHeightGap = 70.0f;
	constexpr float JumpNearRange = 500.0f;
	/** 점프 사이 최소 간격(초). */
	constexpr float JumpCooldown = 0.7f;
	/** 막힌 채 이 시간(초)이 지나면 점프로 넘어 본다. */
	constexpr float StuckJumpTime = 0.4f;
	/** 이 거리(cm)보다 가까우면 겹침 방지로 살짝 비켜 준다. */
	constexpr float TooCloseDistance = 55.0f;
	/** 순간이동 후 이 시간(초) 동안은 다시 이동하지 않는다. */
	constexpr float TeleportCooldown = 1.5f;
	/** 낭떠러지 판정: 앞쪽 발 아래 이 거리(cm) 안에 바닥이 없으면 구멍. */
	constexpr float GroundProbeDepth = 160.0f;
	constexpr float GroundProbeAhead = 90.0f;
	/** 자동 해킹/탐지 확인 주기(초) — 단말기 전체 순회를 매 프레임 하지 않게. */
	constexpr float HackCheckInterval = 0.25f;
}

UCXSoloFollowerComponent::UCXSoloFollowerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UCXSoloFollowerComponent* UCXSoloFollowerComponent::EnsureOn(APawn* Pawn)
{
	if (!Pawn)
	{
		return nullptr;
	}
	if (UCXSoloFollowerComponent* Existing = Pawn->FindComponentByClass<UCXSoloFollowerComponent>())
	{
		return Existing;
	}
	UCXSoloFollowerComponent* Component = NewObject<UCXSoloFollowerComponent>(Pawn, TEXT("CXSoloFollower"));
	Pawn->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

void UCXSoloFollowerComponent::Configure(bool bInAI, APawn* InTarget)
{
	if (bAI && !bInAI)
	{
		// 조작으로 넘어감: 자동 동작 정리
		ResetAutomation();
	}
	bAI = bInAI;
	Target = InTarget;
}

void UCXSoloFollowerComponent::ResetAutomation()
{
	bMoving = false;
	StuckTime = 0.0f;
	HackElapsed = 0.0f;
	bHackCompleted = false;

	APawn* Self = Cast<APawn>(GetOwner());
	if (!Self)
	{
		return;
	}
	if (ACharacter* Character = Cast<ACharacter>(Self))
	{
		Character->StopJumping();
	}
	// AI가 연 해킹 세션은 닫는다(조작으로 넘어가면 플레이어가 새로 시작)
	if (USisterHackComponent* Hack = Self->FindComponentByClass<USisterHackComponent>(); Hack && Hack->IsHacking())
	{
		Hack->CancelHack();
	}
	if (UBrotherCombatComponent* Combat = Self->FindComponentByClass<UBrotherCombatComponent>(); Combat && Combat->bIsAiming)
	{
		Combat->StopAim();
	}
}

void UCXSoloFollowerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Self = Cast<ACharacter>(GetOwner());
	const APawn* Leader = Target.Get();
	if (!bAI || !Self || !Leader || !Self->HasAuthority())
	{
		return;
	}

	// 점프 키를 뗀 것처럼 한 번 놓아 준다(Jump()는 StopJumping 전까지 눌림 상태)
	const double Now = GetWorld()->GetTimeSeconds();
	if (JumpReleaseTime > 0.0 && Now >= JumpReleaseTime)
	{
		Self->StopJumping();
		JumpReleaseTime = 0.0;
	}

	const ALocalCoopGameMode* GameMode = GetWorld()->GetAuthGameMode<ALocalCoopGameMode>();
	if (!GameMode || GameMode->IsGameOver() || GameMode->IsGameCleared() || GameMode->IsTravelling())
	{
		return;
	}
	if (!UCXPlayerLifeComponent::IsPawnAlive(Self) || !UCXPlayerLifeComponent::IsPawnAlive(Leader))
	{
		bMoving = false;
		StuckTime = 0.0f;
		return;
	}

	// 역할별 자동 행동
	bool bBusy = false;
	if (USisterHackComponent* Hack = Self->FindComponentByClass<USisterHackComponent>())
	{
		TickAutoHack(DeltaTime, Hack);
		bBusy = Hack->IsHacking();	// 해킹 중엔 제자리 고정(퍼즐 규칙과 같음)
	}
	else if (UBrotherCombatComponent* Combat = Self->FindComponentByClass<UBrotherCombatComponent>())
	{
		TickAutoFire(Combat);
	}

	if (!bBusy)
	{
		TickFollow(DeltaTime, Self, Leader);
	}
}

// ---------------------------------------------------------------- 따라다니기

void UCXSoloFollowerComponent::TickFollow(float DeltaTime, ACharacter* Self, const APawn* Leader)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastTeleportTime < CXSoloFollow::TeleportCooldown)
	{
		return;
	}

	const FVector To = Leader->GetActorLocation() - Self->GetActorLocation();
	const FVector To2D(To.X, To.Y, 0.0f);
	const float Dist2D = To2D.Size();

	// 너무 멀어졌다 → 옆으로 순간이동
	if (Dist2D > TeleportDistance)
	{
		UE_LOG(LogCXSolo, Log, TEXT("Solo: %s too far (%.0f cm) -> teleport"), *Self->GetName(), Dist2D);
		TeleportNextToLeader(Self, Leader);
		return;
	}

	// 이동 시작/정지 히스테리시스
	if (!bMoving && Dist2D > FollowDistance + StartMoveMargin)
	{
		bMoving = true;
	}
	else if (bMoving && Dist2D <= FollowDistance)
	{
		bMoving = false;
	}

	UCharacterMovementComponent* Movement = Self->GetCharacterMovement();
	const bool bOnGround = Movement && Movement->IsMovingOnGround();
	const FVector Dir = Dist2D > KINDA_SMALL_NUMBER ? To2D / Dist2D : FVector::ZeroVector;

	if (bMoving)
	{
		// 가는 중에는 풀 스피드(목표가 달리면 따라붙는다). 가까워지면 부드럽게.
		const float Scale = FMath::Clamp((Dist2D - FollowDistance) / 60.0f, 0.35f, 1.0f);
		Self->AddMovementInput(Dir, Scale);

		// 막힘 감지: 가려는데 거의 못 움직인다
		const float HorizontalSpeed = Self->GetVelocity().Size2D();
		if (HorizontalSpeed < 20.0f && !Self->GetMovementComponent()->IsFalling())
		{
			StuckTime += DeltaTime;
		}
		else
		{
			StuckTime = 0.0f;
		}

		// 점프 판단: 1) 목표가 위에 있다 2) 앞이 낭떠러지(목표가 아래가 아닐 때) 3) 막혔다
		if (bOnGround)
		{
			const bool bTargetAbove = To.Z > CXSoloFollow::JumpHeightGap && Dist2D < CXSoloFollow::JumpNearRange;
			const bool bGapAhead = !HasGroundAhead(Self, Dir) && To.Z > -120.0f;
			const bool bBlocked = StuckTime > CXSoloFollow::StuckJumpTime;
			if (bTargetAbove || bGapAhead || bBlocked)
			{
				TryJump(Self);
			}
		}

		if (StuckTime > StuckTeleportTime)
		{
			UE_LOG(LogCXSolo, Log, TEXT("Solo: %s stuck %.1fs -> teleport"), *Self->GetName(), StuckTime);
			TeleportNextToLeader(Self, Leader);
		}
	}
	else
	{
		StuckTime = 0.0f;
		// 너무 붙어 있으면(캡슐이 겹쳐 밀어내는 일 방지) 살짝 비켜 준다
		if (Dist2D < CXSoloFollow::TooCloseDistance && Leader->GetVelocity().Size2D() < 30.0f)
		{
			Self->AddMovementInput(-Dir, 0.4f);
		}
		// 목표가 위에 있고 가까우면(발판 위) 제자리에서도 점프
		else if (bOnGround && To.Z > CXSoloFollow::JumpHeightGap && Dist2D < FollowDistance + StartMoveMargin)
		{
			TryJump(Self);
		}
	}
}

void UCXSoloFollowerComponent::TryJump(ACharacter* Self)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastJumpTime < CXSoloFollow::JumpCooldown || !Self->CanJump())
	{
		return;
	}
	LastJumpTime = Now;
	Self->Jump();
	JumpReleaseTime = Now + 0.2;
}

bool UCXSoloFollowerComponent::HasGroundAhead(const ACharacter* Self, const FVector& Dir) const
{
	if (Dir.IsNearlyZero())
	{
		return true;
	}
	const UCapsuleComponent* Capsule = Self->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0f;
	const FVector Feet = Self->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
	const FVector Start = Feet + Dir * CXSoloFollow::GroundProbeAhead + FVector(0.0f, 0.0f, 40.0f);
	const FVector End = Start - FVector(0.0f, 0.0f, 40.0f + CXSoloFollow::GroundProbeDepth);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXSoloGroundProbe), false, Self);
	FHitResult Hit;
	return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
}

void UCXSoloFollowerComponent::TeleportNextToLeader(ACharacter* Self, const APawn* Leader)
{
	const FVector Base = Leader->GetActorLocation();
	// 목표 옆 1m(진행 축 뒤쪽 → 앞쪽 → 레인 앞뒤 순). 겹침이 있으면 엔진이 비어 있는 자리로 밀어낸다
	const FVector Offsets[] =
	{
		FVector(0.0f, -FollowDistance, 20.0f),
		FVector(0.0f, FollowDistance, 20.0f),
		FVector(-FollowDistance, 0.0f, 20.0f),
		FVector(FollowDistance, 0.0f, 20.0f),
		FVector(0.0f, 0.0f, 120.0f),
	};
	bool bMoved = false;
	for (const FVector& Offset : Offsets)
	{
		if (Self->TeleportTo(Base + Offset, Self->GetActorRotation(), false, false))
		{
			bMoved = true;
			break;
		}
	}
	if (!bMoved)
	{
		Self->TeleportTo(Base + Offsets[0], Self->GetActorRotation(), false, true);
	}
	if (UCharacterMovementComponent* Movement = Self->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	bMoving = false;
	StuckTime = 0.0f;
	LastTeleportTime = GetWorld()->GetTimeSeconds();
}

// ---------------------------------------------------------------- 여동생 AI: 자동 해킹

void UCXSoloFollowerComponent::TickAutoHack(float DeltaTime, USisterHackComponent* Hack)
{
	if (Hack->IsHacking())
	{
		// 세션이 열려 있다 → 잠깐 뒤 성공 경로로 마무리(성공 표시가 뜬 뒤에는 한 번만)
		HackElapsed += DeltaTime;
		const float Delay = Hack->IsEnemyHacking() ? EnemyHackDelay : TerminalHackDelay;
		if (!bHackCompleted && HackElapsed >= Delay)
		{
			bHackCompleted = true;
			Hack->SoloCompleteSession();
		}
		return;
	}
	HackElapsed = 0.0f;
	bHackCompleted = false;

	HackCheckTimer -= DeltaTime;
	if (HackCheckTimer > 0.0f)
	{
		return;
	}
	HackCheckTimer = CXSoloFollow::HackCheckInterval;

	// 단말기 우선(F 상호작용과 같은 순서), 없으면 범위 안 적 — 둘 다 여동생 본인의 탐지/세션 경로
	if (Hack->WantsInteract())
	{
		Hack->Interact();
	}
	else if (Hack->GetEnemyHackCooldownRemaining() <= 0.0f)
	{
		Hack->TryHackEnemy();
	}
}

// ---------------------------------------------------------------- 오빠 AI: 자동 사격

void UCXSoloFollowerComponent::TickAutoFire(UBrotherCombatComponent* Combat)
{
	Combat->SoloAutoFire();
}
