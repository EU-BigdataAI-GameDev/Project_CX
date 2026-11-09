// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXPlayerLifeComponent.h"
#include "LocalCoopGameMode.h"
#include "CXHealthComponent.h"
#include "SisterHackComponent.h"
#include "BrotherCombatComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"

namespace CXLife
{
	/** 부활 지점 바닥 탐색: 후보 위 / 아래 거리(cm). */
	constexpr float TraceUp = 150.0f;
	constexpr float TraceDown = 400.0f;
	/** 걸을 수 있는 바닥 기울기(법선 Z). */
	constexpr float MinFloorNormalZ = 0.7f;
	/** 같은 자리 중복 기록 방지 거리(cm). */
	constexpr float SafeGroundMinStep = 50.0f;
}

UCXPlayerLifeComponent::UCXPlayerLifeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 이동 컴포넌트보다 먼저(BeginPlay에서 선행 조건 추가) — 엔진 KillZ 처리보다 앞서 낙사를 잡는다
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	// 런타임에 붙는 컴포넌트라 원격 머신에도 생기도록 복제
	SetIsReplicatedByDefault(true);
}

UCXPlayerLifeComponent* UCXPlayerLifeComponent::EnsureOn(APawn* Pawn)
{
	if (!Pawn)
	{
		return nullptr;
	}
	if (UCXPlayerLifeComponent* Existing = Pawn->FindComponentByClass<UCXPlayerLifeComponent>())
	{
		return Existing;
	}
	// BP_Brother처럼 C++ 부모가 없는 폰도 에셋을 고치지 않고 런타임에 붙인다
	UCXPlayerLifeComponent* Component = NewObject<UCXPlayerLifeComponent>(Pawn, TEXT("CXPlayerLife"));
	Pawn->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

bool UCXPlayerLifeComponent::IsPawnAlive(const APawn* Pawn)
{
	if (!IsValid(Pawn))
	{
		return false;
	}
	const UCXPlayerLifeComponent* Life = Pawn->FindComponentByClass<UCXPlayerLifeComponent>();
	return !Life || Life->IsAlive();
}

void UCXPlayerLifeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCXPlayerLifeComponent, LifeState);
	DOREPLIFETIME(UCXPlayerLifeComponent, RespawnEndTime);
	DOREPLIFETIME(UCXPlayerLifeComponent, RespawnDuration);
	DOREPLIFETIME(UCXPlayerLifeComponent, bInvulnerable);
	DOREPLIFETIME(UCXPlayerLifeComponent, LastDeathCause);
}

bool UCXPlayerLifeComponent::IsServer() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

AController* UCXPlayerLifeComponent::ResolveController() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn && Pawn->GetController())
	{
		return Pawn->GetController();
	}
	return CachedController.Get();
}

// ---------------------------------------------------------------- 수명

void UCXPlayerLifeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		// 이 컴포넌트가 이동보다 먼저 틱 → 이전 프레임 위치로 낙사 판정
		Character->GetCharacterMovement()->AddTickPrerequisiteComponent(this);
	}
}

void UCXPlayerLifeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 안전망: 엔진 KillZ 처리 등으로 폰이 살아 있는 채 파괴되면 사망으로 넘긴다(게임모드가 새 폰으로 부활시킨다)
	if (EndPlayReason == EEndPlayReason::Destroyed && IsServer() && LifeState == ECXLifeState::Alive && !bReportedDeath)
	{
		UWorld* World = GetWorld();
		AController* Controller = CachedController.Get();
		if (World && !World->bIsTearingDown && Controller)
		{
			bReportedDeath = true;
			if (ALocalCoopGameMode* GameMode = World->GetAuthGameMode<ALocalCoopGameMode>())
			{
				GameMode->NotifyPlayerDied(Controller, ECXDeathCause::Fall);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UCXPlayerLifeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (Pawn->GetController())
		{
			CachedController = Pawn->GetController();
		}
	}

	if (IsServer())
	{
		if (LifeState == ECXLifeState::Alive)
		{
			CheckFallDeath();
		}
		if (LifeState == ECXLifeState::Alive)
		{
			SafeGroundAccum += DeltaTime;
			if (SafeGroundAccum >= SafeGroundInterval)
			{
				SafeGroundAccum = 0.0f;
				RecordSafeGround();
			}
		}
		// 부활 무적 종료
		if (bInvulnerable && GetWorld()->GetTimeSeconds() >= InvulnerableEndTime)
		{
			bInvulnerable = false;
			if (AActor* Owner = GetOwner())
			{
				Owner->SetCanBeDamaged(bSavedCanBeDamaged);
			}
			OnRep_Invulnerable();
		}
	}

	// 무적 점멸(모든 머신 — 복제된 bInvulnerable 기준)
	if (bInvulnerable && LifeState == ECXLifeState::Alive)
	{
		const bool bHide = FMath::Fmod(GetWorld()->GetTimeSeconds() * BlinkRate, 2.0) >= 1.0;
		SetBodyBlinkHidden(bHide);
	}
}

// ---------------------------------------------------------------- 낙사 / 안전 지면

void UCXPlayerLifeComponent::CheckFallDeath()
{
	const AActor* Owner = GetOwner();
	const AWorldSettings* WorldSettings = GetWorld()->GetWorldSettings(true);
	if (!Owner || !WorldSettings || !WorldSettings->AreWorldBoundsChecksEnabled())
	{
		return;
	}
	if (Owner->GetActorLocation().Z < WorldSettings->KillZ + FallDeathMargin)
	{
		// 낙사는 킬존 도달 시점에 사망 확정(기획 6장)
		Kill(ECXDeathCause::Fall);
	}
}

void UCXPlayerLifeComponent::RecordSafeGround()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || Character->IsHidden())
	{
		return;
	}
	const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	if (!Move || !Move->IsMovingOnGround() || !Move->CurrentFloor.IsWalkableFloor())
	{
		return;
	}
	// 이동 발판(Movable) 위는 제외 — 정적 지오메트리만
	const UPrimitiveComponent* Floor = Move->CurrentFloor.HitResult.GetComponent();
	if (!Floor || Floor->Mobility == EComponentMobility::Movable)
	{
		return;
	}
	const AWorldSettings* WorldSettings = GetWorld()->GetWorldSettings(true);
	const FVector Location = Character->GetActorLocation();
	if (WorldSettings && Location.Z < WorldSettings->KillZ + SafeGroundKillZClearance)
	{
		return;
	}

	const FVector Forward = ComputeProgressDirection(Character);

	if (SafeGround.Num() > 0 && FVector::DistSquared(SafeGround.Last().Location, Location) < FMath::Square(CXLife::SafeGroundMinStep))
	{
		SafeGround.Last().Forward = Forward;
		return;
	}
	SafeGround.Add({ Location, Forward });
	while (SafeGround.Num() > FMath::Max(1, SafeGroundCount))
	{
		SafeGround.RemoveAt(0);
	}
}

FVector UCXPlayerLifeComponent::ComputeProgressDirection(const ACharacter* Character)
{
	FVector Facing = Character->GetActorForwardVector();
	Facing.Z = 0.0f;
	Facing = Facing.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

	// 사이드뷰 스크롤 축 = 이 캐릭터 카메라의 오른쪽(수평). 정지 시 캐릭터가 카메라 쪽/안쪽을 볼 수 있어
	// 바라보는 방향만 쓰면 "뒤쪽"이 화면 깊이 방향이 된다 → 스크롤 축으로 투영한다(카메라는 읽기만).
	const APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (!PC || !PC->PlayerCameraManager)
	{
		return Facing;
	}
	FVector Axis = FRotationMatrix(PC->PlayerCameraManager->GetCameraRotation()).GetUnitAxis(EAxis::Y);
	Axis.Z = 0.0f;
	if (!Axis.Normalize())
	{
		return Facing;
	}
	float Side = FVector::DotProduct(Facing, Axis);
	if (FMath::Abs(Side) < 0.3f)
	{
		// 화면 깊이 쪽을 보고 서 있으면 이동 방향으로 판단(그것도 없으면 화면 오른쪽 = 진행 방향으로 가정)
		const FVector Velocity = Character->GetVelocity().GetSafeNormal2D();
		Side = Velocity.IsNearlyZero() ? 1.0f : FVector::DotProduct(Velocity, Axis);
	}
	return Side >= 0.0f ? Axis : -Axis;
}

bool UCXPlayerLifeComponent::FindRespawnSpotFor(const APawn* Respawner, FVector& OutLocation, FRotator& OutRotation) const
{
	const APawn* Partner = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!Partner || !World)
	{
		return false;
	}

	float Radius = 34.0f;
	float HalfHeight = 88.0f;
	const ACharacter* RespawnChar = Cast<ACharacter>(Respawner);
	const UCharacterMovementComponent* RespawnMove = RespawnChar ? RespawnChar->GetCharacterMovement() : nullptr;
	if (RespawnChar)
	{
		RespawnChar->GetCapsuleComponent()->GetScaledCapsuleSize(Radius, HalfHeight);
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXRespawnSpot), false);
	Params.AddIgnoredActor(Partner);
	if (Respawner)
	{
		Params.AddIgnoredActor(Respawner);
	}

	// 기록 없음(스폰 직후 등)이면 파트너가 땅 위에 있을 때만 현재 위치를 쓴다
	TArray<FSafeGroundEntry> Entries = SafeGround;
	if (Entries.Num() == 0)
	{
		const ACharacter* PartnerChar = Cast<ACharacter>(Partner);
		if (PartnerChar && PartnerChar->GetCharacterMovement()->IsMovingOnGround())
		{
			Entries.Add({ Partner->GetActorLocation(), ComputeProgressDirection(PartnerChar) });
		}
	}

	// 최신 기록부터, 막히면 이전 기록으로 후퇴
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		const FSafeGroundEntry& Entry = Entries[i];
		FVector Dir = Entry.Forward;
		if (RespawnMove && RespawnMove->bConstrainToPlane)
		{
			// 사이드뷰 평면 안에서만 뒤/앞으로
			Dir = RespawnMove->ConstrainDirectionToPlane(Dir).GetSafeNormal(UE_SMALL_NUMBER, Dir);
		}
		for (const float Offset : RespawnOffsets)
		{
			FVector Candidate = Entry.Location + Dir * Offset;
			if (RespawnMove && RespawnMove->bConstrainToPlane)
			{
				Candidate = RespawnMove->ConstrainLocationToPlane(Candidate);
			}

			// 아래로 바닥 확인(구멍 위 금지)
			FHitResult Hit;
			const FVector Start = Candidate + FVector(0.0f, 0.0f, CXLife::TraceUp);
			const FVector End = Candidate - FVector(0.0f, 0.0f, CXLife::TraceDown);
			if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params) || Hit.ImpactNormal.Z < CXLife::MinFloorNormalZ)
			{
				continue;
			}
			const UPrimitiveComponent* FloorComp = Hit.GetComponent();
			if (FloorComp && FloorComp->Mobility == EComponentMobility::Movable)
			{
				continue;
			}
			const FVector Spot = Hit.ImpactPoint + FVector(0.0f, 0.0f, HalfHeight + 5.0f);

			// 캡슐이 벽/천장에 끼지 않는지
			if (World->OverlapBlockingTestByChannel(Spot, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
			{
				continue;
			}
			OutLocation = Spot;
			OutRotation = Entry.Forward.Rotation();
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------- 사망 / 부활

void UCXPlayerLifeComponent::Kill(ECXDeathCause Cause)
{
	if (!IsServer() || LifeState != ECXLifeState::Alive)
	{
		return;
	}
	AController* Controller = ResolveController();
	ALocalCoopGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ALocalCoopGameMode>() : nullptr;
	if (GameMode && Controller)
	{
		GameMode->NotifyPlayerDied(Controller, Cause);
	}
}

void UCXPlayerLifeComponent::EnterDeath(ECXDeathCause Cause, float RespawnDelay)
{
	if (!IsServer())
	{
		return;
	}
	const ECXLifeState Previous = LifeState;
	LastDeathCause = Cause;
	bReportedDeath = true;

	// 여동생 해킹 중 사망 → 세션 취소(실패 아님, 단말기는 다시 시도 가능)
	if (USisterHackComponent* Hack = GetOwner()->FindComponentByClass<USisterHackComponent>())
	{
		if (Hack->IsHacking())
		{
			Hack->CancelHack();
		}
	}

	// 제미니 효과: 보호막 해제, 오빠 공격력 강화 즉시 해제(부활해도 없음 — 제미니 기능 기획 2-5)
	if (UCXHealthComponent* Health = GetOwner()->FindComponentByClass<UCXHealthComponent>())
	{
		Health->ClearShield();
	}
	if (UBrotherCombatComponent* Combat = GetOwner()->FindComponentByClass<UBrotherCombatComponent>())
	{
		Combat->ClearDamageBoost();
	}

	if (RespawnDelay >= 0.0f)
	{
		LifeState = ECXLifeState::AwaitingRespawn;
		RespawnDuration = RespawnDelay;
		const AGameStateBase* GameState = GetWorld()->GetGameState();
		const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		RespawnEndTime = Now + RespawnDelay;
	}
	else
	{
		LifeState = ECXLifeState::Dead;
		RespawnDuration = 0.0f;
		RespawnEndTime = 0.0;
	}

	// 무적 해제 + 사망 중 피해 무시
	bInvulnerable = false;
	AActor* Owner = GetOwner();
	bSavedCanBeDamaged = true;
	Owner->SetCanBeDamaged(false);
	SetBodyBlinkHidden(false);

	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, DeathSound, Owner->GetActorLocation());
	}

	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		UCharacterMovementComponent* Move = Character->GetCharacterMovement();
		Move->StopMovementImmediately();
		Move->DisableMovement();
		// 낙사 위치가 KillZ 아래면 숨긴 채 KillZ 위로 올려 둔다(엔진 파괴 방지)
		const AWorldSettings* WorldSettings = GetWorld()->GetWorldSettings(true);
		if (WorldSettings && Owner->GetActorLocation().Z < WorldSettings->KillZ + 1.0f)
		{
			FVector Loc = Owner->GetActorLocation();
			Loc.Z = WorldSettings->KillZ + FallDeathMargin;
			Owner->SetActorLocation(Loc, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	if (Previous == ECXLifeState::Alive)
	{
		ApplyLifeStateVisuals(true);
	}
}

void UCXPlayerLifeComponent::CancelRespawn()
{
	if (!IsServer() || LifeState != ECXLifeState::AwaitingRespawn)
	{
		return;
	}
	LifeState = ECXLifeState::Dead;
	RespawnEndTime = 0.0;
}

void UCXPlayerLifeComponent::Revive(const FVector& Location, const FRotator& Rotation, float InvulnerableTime)
{
	if (!IsServer() || LifeState == ECXLifeState::Alive)
	{
		return;
	}
	LifeState = ECXLifeState::Alive;
	RespawnEndTime = 0.0;
	bReportedDeath = false;
	SafeGround.Reset();
	SafeGroundAccum = 0.0f;

	AActor* Owner = GetOwner();
	ApplyLifeStateVisuals(false);

	FRotator Facing = Rotation;
	Facing.Pitch = 0.0f;
	Facing.Roll = 0.0f;
	Owner->TeleportTo(Location, Facing, false, true);
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		UCharacterMovementComponent* Move = Character->GetCharacterMovement();
		Move->StopMovementImmediately();
		Move->SetMovementMode(MOVE_Falling);
	}

	// 체력 전체 회복(전투 시스템 기획 9.1 — 부활 체력 100, 세이브포인트 체력 저장 없음)
	if (UCXHealthComponent* Health = Owner->FindComponentByClass<UCXHealthComponent>())
	{
		Health->ResetToMax();
	}
	StartInvulnerability(InvulnerableTime);
	LastReviveLocalTime = GetWorld()->GetTimeSeconds();

	if (ReviveSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ReviveSound, Location);
	}
}

void UCXPlayerLifeComponent::StartInvulnerability(float Seconds)
{
	if (!IsServer() || Seconds <= 0.0f)
	{
		return;
	}
	AActor* Owner = GetOwner();
	if (!bInvulnerable)
	{
		bSavedCanBeDamaged = true;
	}
	bInvulnerable = true;
	InvulnerableEndTime = GetWorld()->GetTimeSeconds() + Seconds;
	Owner->SetCanBeDamaged(false);
}

void UCXPlayerLifeComponent::OnRep_LifeState(ECXLifeState PreviousState)
{
	const bool bWasDead = PreviousState != ECXLifeState::Alive;
	const bool bIsDead = LifeState != ECXLifeState::Alive;
	if (bWasDead != bIsDead)
	{
		ApplyLifeStateVisuals(bIsDead);
		if (!bIsDead)
		{
			LastReviveLocalTime = GetWorld()->GetTimeSeconds();
		}
	}
}

void UCXPlayerLifeComponent::OnRep_Invulnerable()
{
	if (!bInvulnerable)
	{
		SetBodyBlinkHidden(false);
	}
}

void UCXPlayerLifeComponent::ApplyLifeStateVisuals(bool bDead)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return;
	}
	Pawn->SetActorHiddenInGame(bDead);
	Pawn->SetActorEnableCollision(!bDead);
	SetBodyBlinkHidden(false);

	if (ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		// 사망 중에는 이동 틱을 꺼서 엔진 KillZ 검사(이동 틱 안)도 돌지 않게
		Character->GetCharacterMovement()->SetComponentTickEnabled(!bDead);
	}

	// 입력은 조작하는 머신에서 막는다(로컬 협동 = 서버, listen server 원격 클라이언트 = OnRep)
	if (Pawn->IsLocallyControlled())
	{
		APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
		if (bDead)
		{
			if (UBrotherCombatComponent* Combat = Pawn->FindComponentByClass<UBrotherCombatComponent>())
			{
				if (Combat->bIsAiming)
				{
					Combat->StopAim();
				}
			}
			Pawn->DisableInput(PC);
			if (PC)
			{
				PC->FlushPressedKeys();
			}
		}
		else
		{
			Pawn->EnableInput(PC);
		}
	}
}

void UCXPlayerLifeComponent::SetBodyBlinkHidden(bool bHidden)
{
	if (bHidden == bBlinkHidden)
	{
		return;
	}
	bBlinkHidden = bHidden;
	// 몸 메시만 점멸(무기 등 붙은 메시의 표시 상태는 건드리지 않는다)
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			Mesh->SetHiddenInGame(bHidden, false);
		}
	}
}

// ---------------------------------------------------------------- 조회

float UCXPlayerLifeComponent::GetRespawnRemaining() const
{
	if (LifeState != ECXLifeState::AwaitingRespawn)
	{
		return 0.0f;
	}
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0);
	return FMath::Max(0.0f, static_cast<float>(RespawnEndTime - Now));
}

float UCXPlayerLifeComponent::GetTimeSinceRevive() const
{
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds() - LastReviveLocalTime) : 1000.0f;
}

FString UCXPlayerLifeComponent::GetDebugState() const
{
	static const TCHAR* StateNames[] = { TEXT("Alive"), TEXT("AwaitingRespawn"), TEXT("Dead") };
	const AActor* Owner = GetOwner();
	return FString::Printf(TEXT("state=%s remaining=%.2f invuln=%d hidden=%d safe=%d loc=%s cause=%d"),
		StateNames[FMath::Clamp(static_cast<int32>(LifeState), 0, 2)], GetRespawnRemaining(), bInvulnerable ? 1 : 0,
		Owner && Owner->IsHidden() ? 1 : 0, SafeGround.Num(), Owner ? *Owner->GetActorLocation().ToCompactString() : TEXT("-"),
		static_cast<int32>(LastDeathCause));
}
