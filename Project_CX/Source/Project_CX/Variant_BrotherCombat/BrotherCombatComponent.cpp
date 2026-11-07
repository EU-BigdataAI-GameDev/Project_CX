// Copyright Epic Games, Inc. All Rights Reserved.

#include "BrotherCombatComponent.h"
#include "BrotherBullet.h"
#include "CombatDamageable.h"
#include "CXHealthComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "CXSfx.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "CXGameInstance.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

UBrotherCombatComponent::UBrotherCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);

	ReloadChargeSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Reload_Charge.SW_Brother_Reload_Charge"));
	ReloadDoneSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Reload_Done.SW_Brother_Reload_Done"));
	DryFireSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_DryFire.SW_Brother_DryFire"));
	LowAmmoSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_LowAmmo.SW_Brother_LowAmmo"));
	AimLockSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_AimLock.SW_Brother_AimLock"));
}

void UBrotherCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentAmmo = MaxAmmo;

	// 맵 이동으로 넘어온 제미니 공격력 강화(남은 시간을 게임 인스턴스에 맡겨 둔다 — 한 번 쓰고 비운다)
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UCXGameInstance* GI = Cast<UCXGameInstance>(GetOwner()->GetGameInstance()); GI && GI->JeminiDamageBoostRemaining > 0.0f)
		{
			ApplyDamageBoost(GI->JeminiDamageBoostRemaining, FMath::Max(1.0f, GI->JeminiDamageBoostMultiplier));
			GI->JeminiDamageBoostRemaining = 0.0f;
		}
	}
}

void UBrotherCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBrotherCombatComponent, DamageBoostEndTime);
	DOREPLIFETIME(UBrotherCombatComponent, DamageBoostMultiplier);
}

double UBrotherCombatComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void UBrotherCombatComponent::ApplyDamageBoost(float Duration, float Multiplier)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Duration <= 0.0f)
	{
		return;
	}
	const double Now = GetServerTime();
	const float Remaining = GetDamageBoostRemaining();
	DamageBoostMultiplier = FMath::Max(1.0f, Multiplier);
	DamageBoostEndTime = Now + Remaining + Duration;
	UE_LOG(LogTemp, Log, TEXT("BrotherCombat: damage boost x%.1f %.0fs (+%.0fs)"), DamageBoostMultiplier, Remaining + Duration, Duration);
}

void UBrotherCombatComponent::ClearDamageBoost()
{
	if (GetOwner() && GetOwner()->HasAuthority() && DamageBoostEndTime > 0.0)
	{
		DamageBoostEndTime = 0.0;
		DamageBoostMultiplier = 1.0f;
		UE_LOG(LogTemp, Log, TEXT("BrotherCombat: damage boost cleared"));
	}
}

float UBrotherCombatComponent::GetDamageBoostRemaining() const
{
	if (DamageBoostEndTime <= 0.0)
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, static_cast<float>(DamageBoostEndTime - GetServerTime()));
}

void UBrotherCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ReloadAudio)
	{
		ReloadAudio->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

bool UBrotherCombatComponent::IsLocallyControlledOwner() const
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	return PawnOwner && PawnOwner->IsLocallyControlled();
}

void UBrotherCombatComponent::PlayLocal2D(USoundBase* Sound) const
{
	// HUD 성격의 소리(탄약 부족·조준 고정)는 오빠를 조작하는 로컬 머신에서만(효과음 적용 기획 6.5)
	if (Sound && IsLocallyControlledOwner())
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
}

void UBrotherCombatComponent::UpdateReloadSound()
{
	if (bIsReloading && ReloadAudio && ReloadAudio->IsPlaying())
	{
		// 장전 시간이 바뀌어도 소리를 다시 만들 필요 없이 진행도로 상승감을 낸다(기획 2.2)
		ReloadAudio->SetPitchMultiplier(FMath::Lerp(ReloadPitchStart, ReloadPitchEnd, GetReloadProgress()));
	}
}

void UBrotherCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bInputBound)
	{
		TryBindInput();
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return;
	}

	if (bIsAiming)
	{
		// 엣지 케이스(자동 조준 사격 구현.md 2.3): 타겟이 죽거나 사거리를 벗어나면 다음으로 가까운 적으로 재탐색.
		if (!IsTargetAlive(CurrentTarget))
		{
			CurrentTarget = FindNearestTarget(AimRange, true);
			if (!CurrentTarget)
			{
				bIsAiming = false;
			}
		}
	}
	else
	{
		// 조준 중이 아니어도, 근접 범위(기본 5m) 안에 적이 있으면 항상 그쪽을 자동으로 바라본다.
		// 방향 제한(원뿔) 없이 순수 거리 기준 — 등 뒤에 붙어도 반응해야 자연스럽다.
		CurrentTarget = FindNearestTarget(ProximityAutoFaceRange, false);
	}

	UpdateFacing(OwnerCharacter, DeltaTime);

	// 조준 고정(LOCK) 타겟을 새로 잡은 순간 짧은 삑
	AActor* LockTarget = bIsAiming ? CurrentTarget.Get() : nullptr;
	if (LockTarget && LockTarget != LastLockTarget.Get())
	{
		PlayLocal2D(AimLockSound);
	}
	LastLockTarget = LockTarget;

	UpdateReloadSound();
}

void UBrotherCombatComponent::UpdateFacing(ACharacter* OwnerCharacter, float DeltaTime)
{
	UCharacterMovementComponent* MovementComp = OwnerCharacter->GetCharacterMovement();
	if (MovementComp && !bCachedDefaultOrientRotation)
	{
		bDefaultOrientRotationToMovement = MovementComp->bOrientRotationToMovement;
		bCachedDefaultOrientRotation = true;
	}

	const bool bHasFacingTarget = IsValid(CurrentTarget);

	if (MovementComp)
	{
		// 타겟을 바라보는 동안은 "이동 방향으로 자동 회전"을 꺼서 캐릭터 회전과 WASD 이동(스트레이프)을 분리한다.
		// 타겟이 없어지면 원래 값으로 복원 — 그 순간부터 다시 이동 방향을 자연스럽게 바라보게 된다.
		MovementComp->bOrientRotationToMovement = bHasFacingTarget ? false : bDefaultOrientRotationToMovement;
	}

	if (!bHasFacingTarget)
	{
		return;
	}

	const FVector ToTarget = CurrentTarget->GetActorLocation() - OwnerCharacter->GetActorLocation();
	const FRotator DesiredRotation(0.0f, ToTarget.Rotation().Yaw, 0.0f);
	const FRotator NewRotation = FMath::RInterpTo(OwnerCharacter->GetActorRotation(), DesiredRotation, DeltaTime, AimRotationInterpSpeed);
	OwnerCharacter->SetActorRotation(NewRotation);
}

void UBrotherCombatComponent::TryBindInput()
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner || !PawnOwner->IsLocallyControlled())
	{
		return;
	}

	UEnhancedInputComponent* EnhancedInputComp = Cast<UEnhancedInputComponent>(PawnOwner->InputComponent);
	if (!EnhancedInputComp)
	{
		// 최초 플레이어 Pawn은 BeginPlay 시점엔 아직 Possess()가 안 끝나 InputComponent가 없을 수 있다.
		// 다음 틱에 다시 시도한다(TickComponent 참고).
		return;
	}

	if (FireAction)
	{
		EnhancedInputComp->BindAction(FireAction, ETriggerEvent::Started, this, &UBrotherCombatComponent::Fire);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BrotherCombatComponent: FireAction이 지정되지 않아 발사 입력이 바인딩되지 않았습니다."));
	}

	if (AimAction)
	{
		EnhancedInputComp->BindAction(AimAction, ETriggerEvent::Started, this, &UBrotherCombatComponent::StartAim);
		EnhancedInputComp->BindAction(AimAction, ETriggerEvent::Completed, this, &UBrotherCombatComponent::StopAim);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("BrotherCombatComponent: AimAction이 지정되지 않아 조준 입력이 바인딩되지 않았습니다."));
	}

	// 게임패드 RT = 사격, LT = 조준(누르고 있기). 같은 Input Action을 쓰므로 위 바인딩이 그대로 받는다.
	const APlayerController* PC = Cast<APlayerController>(PawnOwner->GetController());
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr)
	{
		if (!GamepadMappingContext)
		{
			GamepadMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_BrotherGamepad"));
			if (FireAction)
			{
				GamepadMappingContext->MapKey(FireAction, EKeys::Gamepad_RightTrigger);
			}
			if (AimAction)
			{
				GamepadMappingContext->MapKey(AimAction, EKeys::Gamepad_LeftTrigger);
			}
		}
		Subsystem->AddMappingContext(GamepadMappingContext, 1);
	}

	bInputBound = true;
}

bool UBrotherCombatComponent::IsTargetAlive(const AActor* Candidate)
{
	// 사망한 거미로봇은 3초 동안 시체로 남는다 — 그동안 조준되면 안 된다(전투 시스템 기획 5장)
	return IsValid(Candidate) && Candidate->CanBeDamaged() && !UCXHealthComponent::IsActorDead(Candidate);
}

AActor* UBrotherCombatComponent::FindNearestTarget(float Range, bool bApplyForwardCone) const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return nullptr;
	}

	TArray<AActor*> Candidates;
	UGameplayStatics::GetAllActorsWithInterface(this, UCombatDamageable::StaticClass(), Candidates);

	const FVector OwnerLocation = OwnerCharacter->GetActorLocation();
	const FVector OwnerForward = OwnerCharacter->GetActorForwardVector();
	const float ConeCos = FMath::Cos(FMath::DegreesToRadians(AimConeHalfAngleDeg));

	AActor* BestTarget = nullptr;
	float BestDistSq = FMath::Square(Range);

	for (AActor* Candidate : Candidates)
	{
		if (Candidate == GetOwner() || !IsTargetAlive(Candidate))
		{
			continue;
		}

		const FVector ToCandidate = Candidate->GetActorLocation() - OwnerLocation;
		const float DistSq = ToCandidate.SizeSquared();
		if (DistSq > BestDistSq)
		{
			continue;
		}

		if (bApplyForwardCone)
		{
			const FVector ToCandidateDir = ToCandidate.GetSafeNormal();
			if (FVector::DotProduct(OwnerForward, ToCandidateDir) < ConeCos)
			{
				continue;
			}
		}

		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(GetOwner());
		QueryParams.AddIgnoredActor(Candidate);
		const bool bBlocked = GetWorld()->LineTraceSingleByChannel(HitResult, OwnerLocation, Candidate->GetActorLocation(), ECC_Visibility, QueryParams);
		if (bBlocked)
		{
			continue;
		}

		BestTarget = Candidate;
		BestDistSq = DistSq;
	}

	return BestTarget;
}

void UBrotherCombatComponent::StartAim()
{
	CurrentTarget = FindNearestTarget(AimRange, true);
	bIsAiming = (CurrentTarget != nullptr);
}

void UBrotherCombatComponent::StopAim()
{
	bIsAiming = false;
	CurrentTarget = nullptr;
}

void UBrotherCombatComponent::Fire()
{
	if (bIsReloading || CurrentAmmo <= 0)
	{
		// 탄약 0(제미니 재장전 전/장전 중)에 쏘면 헛방 — 자동 재장전은 없다(제미니 AI봇 기획 B3)
		if (CurrentAmmo <= 0 && DryFireSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, DryFireSound, GetOwner()->GetActorLocation());
		}
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return;
	}

	// 연사 제한: FireInterval 안의 클릭은 무시(탄약도 소모하지 않는다)
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastFireTime < FireInterval)
	{
		return;
	}
	LastFireTime = Now;

	const FVector SpawnLocation = OwnerCharacter->GetActorLocation() + OwnerCharacter->GetActorRotation().RotateVector(MuzzleOffset);

	FVector AimDirection;
	if (IsValid(CurrentTarget))
	{
		// CurrentTarget은 우클릭 조준 중이거나, 근접 자동 조준 범위(기본 5m) 안에 적이 있을 때 채워져 있다.
		// 두 경우 모두 "무조건 그 방향으로 발사"되어야 하므로 여기서 통합 처리한다.
		AimDirection = (CurrentTarget->GetActorLocation() - SpawnLocation).GetSafeNormal();
	}
	else
	{
		// 자동 조준 사격 구현.md 3.2: 조준 중이 아니고 근접 타겟도 없으면 이번 한 발만 즉시 재탐색한 방향으로 나간다
		// (CurrentTarget/bIsAiming을 바꾸지는 않는다 — 캐릭터가 계속 그 타겟을 보게 만들지 않기 위함).
		AActor* OneShotTarget = FindNearestTarget(AimRange, true);
		AimDirection = OneShotTarget
			? (OneShotTarget->GetActorLocation() - SpawnLocation).GetSafeNormal()
			: OwnerCharacter->GetActorForwardVector();
	}

	const int32 PrevAmmo = CurrentAmmo;
	CurrentAmmo = FMath::Max(CurrentAmmo - 1, 0);

	// 탄약 부족 경계(HUD LOW CHARGE와 같은 기준)를 넘는 순간 1회
	const int32 LowThreshold = FMath::CeilToInt(MaxAmmo * LowAmmoRatio);
	if (PrevAmmo > LowThreshold && CurrentAmmo <= LowThreshold && CurrentAmmo > 0)
	{
		PlayLocal2D(LowAmmoSound);
	}

	if (OwnerCharacter->HasAuthority())
	{
		SpawnBullet(AimDirection);
	}
	else
	{
		Server_Fire(AimDirection);
	}
}

void UBrotherCombatComponent::Server_Fire_Implementation(FVector_NetQuantizeNormal AimDirection)
{
	// 원격 오빠: 서버 쪽 탄약 사본도 줄여 둔다(제미니 요청 상태 수집용)
	if (!IsLocallyControlledOwner())
	{
		CurrentAmmo = FMath::Max(CurrentAmmo - 1, 0);
	}
	SpawnBullet(AimDirection);
}

bool UBrotherCombatComponent::RequestJeminiReload()
{
	if (bIsReloading || CurrentAmmo >= MaxAmmo)
	{
		return false;
	}
	if (IsLocallyControlledOwner())
	{
		StartReload();
	}
	else
	{
		Client_JeminiReload();
		// 서버 사본: 장전이 끝난 것으로 본다
		CurrentAmmo = MaxAmmo;
	}
	return true;
}

void UBrotherCombatComponent::Client_JeminiReload_Implementation()
{
	if (!bIsReloading && CurrentAmmo < MaxAmmo)
	{
		StartReload();
	}
}

void UBrotherCombatComponent::SpawnBullet(const FVector& AimDirection)
{
	if (!BulletClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("BrotherCombatComponent: BulletClass가 지정되지 않아 발사되지 않았습니다."));
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return;
	}

	const FVector SpawnLocation = OwnerCharacter->GetActorLocation() + OwnerCharacter->GetActorRotation().RotateVector(MuzzleOffset);
	const FRotator SpawnRotation = AimDirection.Rotation();

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = OwnerCharacter;
	SpawnParams.Instigator = OwnerCharacter;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ABrotherBullet* Bullet = GetWorld()->SpawnActor<ABrotherBullet>(BulletClass, SpawnLocation, SpawnRotation, SpawnParams))
	{
		// 제미니 공격력 강화(서버에서만 — SpawnBullet은 서버 경로)
		Bullet->SetDamage(BulletDamage * GetDamageMultiplier());
	}
}

float UBrotherCombatComponent::GetReloadProgress() const
{
	if (!bIsReloading || ReloadTime <= 0.0f)
	{
		return 0.0f;
	}
	const float Remaining = GetWorld()->GetTimerManager().GetTimerRemaining(ReloadTimerHandle);
	return FMath::Clamp(1.0f - Remaining / ReloadTime, 0.0f, 1.0f);
}

void UBrotherCombatComponent::StartReload()
{
	bIsReloading = true;
	GetWorld()->GetTimerManager().SetTimer(ReloadTimerHandle, this, &UBrotherCombatComponent::FinishReload, ReloadTime, false);

	// 충전 루프: 캐릭터에 붙여 재생, 피치는 TickComponent에서 진행도로 올린다
	if (ReloadChargeSound)
	{
		if (!ReloadAudio && GetOwner())
		{
			ReloadAudio = UGameplayStatics::SpawnSoundAttached(ReloadChargeSound, GetOwner()->GetRootComponent(), NAME_None, FVector::ZeroVector,
				EAttachLocation::KeepRelativeOffset, true, 1.0f, ReloadPitchStart, 0.0f, nullptr, nullptr, false);
		}
		else if (ReloadAudio)
		{
			ReloadAudio->SetPitchMultiplier(ReloadPitchStart);
			ReloadAudio->Play();
		}
	}
}

void UBrotherCombatComponent::FinishReload()
{
	CurrentAmmo = MaxAmmo;
	bIsReloading = false;

	if (ReloadAudio)
	{
		ReloadAudio->FadeOut(0.05f, 0.0f);
	}
	if (ReloadDoneSound && GetOwner())
	{
		UGameplayStatics::PlaySoundAtLocation(this, ReloadDoneSound, GetOwner()->GetActorLocation());
	}
}
