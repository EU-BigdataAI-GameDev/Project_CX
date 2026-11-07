// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpiderBot.h"
#include "CXHealthComponent.h"
#include "CXPlayerLifeComponent.h"
#include "CXSfx.h"
#include "AIController.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "TimerManager.h"

namespace SpiderBotDefaults
{
	// 에셋: Content/Project_CX/Characters/Enemy/SpiderBot/ (모델·Idle 애님·피직스 에셋 — 기존 커밋)
	const TCHAR* MeshPath = TEXT("/Game/Project_CX/Characters/Enemy/SpiderBot/SpiderBot_Skeletal.SpiderBot_Skeletal");
	const TCHAR* IdlePath = TEXT("/Game/Project_CX/Characters/Enemy/SpiderBot/SpiderBot_Skeletal_Anim.SpiderBot_Skeletal_Anim");
	// 공격 루프(1.0초, 24fps): Idle 0프레임 포즈 기반, saw_base/saw_elbow 와인드업(0~0.54초) → 내려찍기(0.63~0.71초) → 복귀 + saw_blade 회전.
	// 에디터 Python(AnimationDataController::SetBoneTrackKeys)으로 생성한 에셋 — 전투 시스템 기획 14장.
	const TCHAR* AttackPath = TEXT("/Game/Project_CX/Characters/Enemy/SpiderBot/AS_SpiderBot_Attack.AS_SpiderBot_Attack");
	const TCHAR* FlashPath = TEXT("/Game/Project_CX/Characters/Enemy/SpiderBot/Mat_EyeGlow.Mat_EyeGlow");
	// 그로기 정전 깜빡임 — 거미 자체 머티리얼(스켈레탈 사용 플래그 있음)만 사용
	const TCHAR* DropoutPath = TEXT("/Game/Project_CX/Characters/Enemy/SpiderBot/Mat_Black.Mat_Black");
	const TCHAR* SparksPath = TEXT("/Game/SoulCity/EffectsMobileOpt/Sparks/ParticleSystems/P_WeldSparks.P_WeldSparks");

	// 원본 모델은 높이 약 390cm — 예전 Map1 배치(c8e3901)와 같은 0.25배(약 1m). 모델 정면(톱날·집게)은 -Y라 +90도 돌려 액터 정면(+X)에 맞춘다.
	constexpr float MeshScale = 0.25f;
	constexpr float MeshYaw = 90.0f;
	constexpr float CapsuleRadius = 48.0f;
	constexpr float CapsuleHalfHeight = 50.0f;
}

ASpiderBot::ASpiderBot()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// 판단은 폰(서버 Tick)에 있고 컨트롤러는 이동 입력 처리용 기본 AAIController — 배치/스폰 모두 자동 빙의
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize(SpiderBotDefaults::CapsuleRadius, SpiderBotDefaults::CapsuleHalfHeight);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = MoveSpeed;
	Move->bOrientRotationToMovement = true;
	Move->bUseControllerDesiredRotation = false;
	Move->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	Move->BrakingDecelerationWalking = 1500.0f;
	// NavMesh 없이 직진 추적 → 발판 끝에서 떨어지지 않게(자기 구역을 지킨다)
	Move->bCanWalkOffLedges = false;
	// 레인의 50cm 데크(Map1 S1_Deck 등)를 따라 올라오도록 기본 45cm보다 조금 높게(계단은 원래 가능)
	Move->MaxStepHeight = 55.0f;

	USkeletalMeshComponent* MeshComp = GetMesh();
	MeshComp->SetSkeletalMeshAsset(CXSfx::Find<USkeletalMesh>(SpiderBotDefaults::MeshPath));
	MeshComp->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -SpiderBotDefaults::CapsuleHalfHeight), FRotator(0.0f, SpiderBotDefaults::MeshYaw, 0.0f));
	MeshComp->SetRelativeScale3D(FVector(SpiderBotDefaults::MeshScale));
	MeshComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	IdleAnim = CXSfx::Find<UAnimSequence>(SpiderBotDefaults::IdlePath);
	MeshComp->AnimationData.AnimToPlay = IdleAnim;
	MeshComp->AnimationData.bSavedLooping = true;
	MeshComp->AnimationData.bSavedPlaying = true;
	AttackAnim = CXSfx::Find<UAnimSequence>(SpiderBotDefaults::AttackPath);
	HitFlashMaterial = CXSfx::Find<UMaterialInterface>(SpiderBotDefaults::FlashPath);

	// 체력 50(총알 10 × 5발). 플레이어용 연출(움찔·피격음·피격 무적·사망 연결)은 끄고 자기 반응을 쓴다.
	Health = CreateDefaultSubobject<UCXHealthComponent>(TEXT("Health"));
	Health->InitMaxHealth(MaxHealth);
	Health->HitInvulnerableTime = 0.0f;
	Health->bKillOwnerOnDepleted = false;
	Health->bFlinchOnHit = false;
	Health->HurtSounds.Reset();

	MoveAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("MoveAudio"));
	MoveAudio->SetupAttachment(RootComponent);
	MoveAudio->bAutoActivate = false;

	// 그로기 전기 불빛(평소 꺼짐) — 몸통 위쪽
	StunLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StunLight"));
	StunLight->SetupAttachment(RootComponent);
	StunLight->SetRelativeLocation(FVector(0.0f, 0.0f, 45.0f));
	StunLight->SetIntensity(0.0f);
	StunLight->SetAttenuationRadius(320.0f);
	StunLight->SetCastShadows(false);
	StunLight->SetVisibility(false);
	StunDropoutMaterial = CXSfx::Find<UMaterialInterface>(SpiderBotDefaults::DropoutPath);
	StunSurgeMaterial = HitFlashMaterial;
	StunSparksTemplate = CXSfx::Find<UParticleSystem>(SpiderBotDefaults::SparksPath);

	// 사운드(효과음 적용 기획 2.4 — 새 임포트 없이 기존 에셋 재사용)
	MoveLoopSound = CXSfx::Find<USoundBase>(TEXT("/Game/SoulCity/Sound/Wav/AmbientLooping/Shake_Metal01.Shake_Metal01"));
	StepSound = CXSfx::Find<USoundBase>(TEXT("/Game/SoulCity/Sound/Cue/Footstep_Metal_Cue.Footstep_Metal_Cue"));
	AttackSawSound = CXSfx::Find<USoundBase>(TEXT("/Game/SoulCity/Sound/Wav/AmbientLooping/SparkLoop02.SparkLoop02"));
	AlertSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/UI/SW_UI_Denied.SW_UI_Denied"));
	DestroySound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/World/SW_Gate_Off.SW_Gate_Off"));
	StunStartSound = DestroySound;
	StunLoopSound = CXSfx::Find<USoundBase>(TEXT("/Game/SoulCity/Sound/Wav/AmbientLooping/SparkLoop01.SparkLoop01"));
	RebootSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/UI/SW_UI_NeonBuzz.SW_UI_NeonBuzz"));
	for (int32 i = 1; i <= 6; ++i)
	{
		const FString Path = FString::Printf(TEXT("/Game/SoulCity/Sound/Wav/AmbientNonLoop/MetalPanel_ThinRattle_0%d.MetalPanel_ThinRattle_0%d"), i, i);
		if (USoundBase* Sound = CXSfx::Find<USoundBase>(*Path))
		{
			HitSounds.Add(Sound);
		}
	}
	SfxClass = CXSfx::Find<USoundClass>(TEXT("/Game/Project_CX/Audio/Classes/SCl_SFX.SCl_SFX"));
	SmallAttenuation = CXSfx::Find<USoundAttenuation>(TEXT("/Game/Project_CX/Audio/Attenuation/ATT_Small.ATT_Small"));
	MediumAttenuation = CXSfx::Find<USoundAttenuation>(TEXT("/Game/Project_CX/Audio/Attenuation/ATT_Medium.ATT_Medium"));
}

void ASpiderBot::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASpiderBot, State);
	DOREPLIFETIME(ASpiderBot, StunEndTime);
}

void ASpiderBot::BeginPlay()
{
	// BP/레벨에서 바꾼 최대 체력을 컴포넌트 BeginPlay(시작 체력 = 최대치) 전에 넘긴다
	if (HasAuthority())
	{
		Health->InitMaxHealth(MaxHealth);
	}
	Super::BeginPlay();

	GetCharacterMovement()->MaxWalkSpeed = MoveSpeed;
	MeshBaseLocation = GetMesh()->GetRelativeLocation();
	if (IdleAnim)
	{
		GetMesh()->PlayAnimation(IdleAnim, true);
		// 여러 마리가 같은 박자로 움직이지 않게 시작 위치를 섞는다
		GetMesh()->SetPosition(FMath::FRand() * IdleAnim->GetPlayLength(), false);
	}

	Health->OnHealthChanged.AddUObject(this, &ASpiderBot::HandleHealthChanged);
	Health->OnDepleted.AddUObject(this, &ASpiderBot::HandleDepleted);

	if (MoveAudio)
	{
		MoveAudio->SetSound(MoveLoopSound);
		MoveAudio->AttenuationSettings = SmallAttenuation;
		MoveAudio->SoundClassOverride = SfxClass;
	}
	SenseTimer = FMath::FRand() * IdleSenseInterval;
}

void ASpiderBot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (State == ESpiderBotState::Dead)
	{
		return;
	}
	if (HasAuthority())
	{
		ServerThink(DeltaSeconds);
	}
	UpdateLocomotionVisuals(DeltaSeconds);
}

// ---------------------------------------------------------------- 서버 판단(상태 기계 — 기획 7.2)

void ASpiderBot::ServerThink(float DeltaSeconds)
{
	StateTime += DeltaSeconds;

	// 제미니 미끼: 그로기·사망이 아니면 일반 판단 대신 미끼 쪽으로(공격 없음)
	if (State != ESpiderBotState::Stunned && State != ESpiderBotState::Dead && TickDecoy(DeltaSeconds))
	{
		return;
	}

	switch (State)
	{
	case ESpiderBotState::Idle:
	{
		SenseTimer -= DeltaSeconds;
		if (SenseTimer <= 0.0f)
		{
			SenseTimer = IdleSenseInterval;
			if (APawn* Found = FindDetectTarget())
			{
				Target = Found;
				RetargetTimer = RetargetInterval;
				SetState(ESpiderBotState::Chase);
			}
		}
		break;
	}

	case ESpiderBotState::Attack:
	{
		// 연속 공격 루프(쿨타임 없음): 대상이 살아 있고 접촉 범위(+해제 여유) 안이면 계속 내려찍는다.
		APawn* Victim = Target.Get();
		if (!IsValidTarget(Victim) || !IsTouching(Victim, AttackReleaseSlack))
		{
			// 범위를 벗어났거나 죽었다 → 다시 추적(즉시 재평가 — 죽었으면 다른 생존자, 없으면 Idle)
			RetargetTimer = 0.0f;
			SetState(ESpiderBotState::Chase);
			break;
		}
		FaceTowards(Victim->GetActorLocation(), DeltaSeconds, false);

		// 타격 = 각 주기의 내려찍기 끝 프레임(애님과 같은 시간축 — 클라이언트도 상태 진입 시 애님을 0부터 재생)
		const float Period = FMath::Max(0.2f, ChopPeriod);
		const float ImpactTime = (ChopsDone + FMath::Clamp(ChopImpactFraction, 0.0f, 1.0f)) * Period;
		if (StateTime >= ImpactTime)
		{
			++ChopsDone;
			PerformChop(Victim);
		}
		break;
	}

	case ESpiderBotState::Stunned:
	{
		// 그로기: 이동·공격 없음(피해는 그대로 받는다 — 오빠가 마무리). 끝나면 즉시 재평가해서 추적 재개.
		if (GetServerTime() >= StunEndTime)
		{
			RetargetTimer = 0.0f;
			UE_LOG(LogTemp, Log, TEXT("SpiderBot: %s reboot (stun over) at t=%.2f"), *GetName(), GetWorld()->GetTimeSeconds());
			SetState(ESpiderBotState::Chase);
		}
		break;
	}

	case ESpiderBotState::Chase:
	{
		APawn* Current = Target.Get();
		const bool bLost = !IsValidTarget(Current) || FVector::Dist(Current->GetActorLocation(), GetActorLocation()) > LoseRadius;
		RetargetTimer -= DeltaSeconds;
		if (bLost || RetargetTimer <= 0.0f)
		{
			// 0.5초마다 가장 가까운 생존자로 재평가. 대상이 죽었거나 멀어졌으면 다른 생존자, 없으면 Idle.
			RetargetTimer = RetargetInterval;
			Current = FindNearestLivingPlayer(LoseRadius);
			Target = Current;
			if (!Current)
			{
				SetState(ESpiderBotState::Idle);
				break;
			}
		}

		if (IsTouching(Current))
		{
			// 붙으면 바로 공격 루프 시작(쿨타임 없음). 무적 중인 대상도 루프는 돌고 피해만 엔진이 거른다.
			StartAttack(Current);
		}
		else
		{
			FVector Dir = Current->GetActorLocation() - GetActorLocation();
			Dir.Z = 0.0f;
			AddMovementInput(Dir.GetSafeNormal());
		}
		break;
	}

	default:
		break;
	}
}

void ASpiderBot::StartAttack(APawn* Victim)
{
	if (!Victim)
	{
		return;
	}
	Target = Victim;
	ChopsDone = 0;
	FaceTowards(Victim->GetActorLocation(), 0.0f, true);
	GetCharacterMovement()->StopMovementImmediately();
	SetState(ESpiderBotState::Attack);
}

void ASpiderBot::PerformChop(APawn* Victim)
{
	// 피해 전달은 엔진 경로(서버 권위 + CanBeDamaged) → 대상의 UCXHealthComponent.
	// 피격 무적(1초)·부활 무적(2초) 중이면 TakeDamage가 0을 돌려준다 → 이번 타격은 헛친다.
	const float Dealt = UGameplayStatics::ApplyDamage(Victim, AttackDamage, GetController(), this, UDamageType::StaticClass());
	PlaySfx(AttackHitSound, SmallAttenuation);
	UE_LOG(LogTemp, Log, TEXT("SpiderBot: %s chop #%d on %s at t=%.2f -> %.0f"), *GetName(), ChopsDone, *Victim->GetName(),
		GetWorld()->GetTimeSeconds(), Dealt);
}

void ASpiderBot::SetState(ESpiderBotState NewState)
{
	if (State == NewState)
	{
		return;
	}
	const ESpiderBotState Previous = State;
	State = NewState;
	StateTime = 0.0f;
	ForceNetUpdate();
	// 서버(로컬 협동 = 화면이 있는 머신)도 같은 연출 — 클라이언트는 OnRep_State
	ApplyStateVisuals(Previous);
}

APawn* ASpiderBot::FindDetectTarget() const
{
	APawn* Best = nullptr;
	float BestDistSq = FMath::Square(DetectRadius);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!IsValidTarget(Pawn))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation());
		if (DistSq < BestDistSq && HasLineOfSight(Pawn))
		{
			Best = Pawn;
			BestDistSq = DistSq;
		}
	}
	return Best;
}

APawn* ASpiderBot::FindNearestLivingPlayer(float Radius) const
{
	APawn* Best = nullptr;
	float BestDistSq = FMath::Square(Radius);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!IsValidTarget(Pawn))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation());
		if (DistSq < BestDistSq)
		{
			Best = Pawn;
			BestDistSq = DistSq;
		}
	}
	return Best;
}

bool ASpiderBot::HasLineOfSight(const APawn* Pawn) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SpiderBotLOS), false, this);
	Params.AddIgnoredActor(Pawn);
	const FVector Eye = GetActorLocation() + FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.5f);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Pawn->GetActorLocation(), ECC_Visibility, Params);
}

bool ASpiderBot::IsTouching(const APawn* Pawn, float ExtraReach) const
{
	if (!Pawn)
	{
		return false;
	}
	float OtherRadius = 34.0f;
	float OtherHalfHeight = 88.0f;
	if (const ACharacter* Other = Cast<ACharacter>(Pawn))
	{
		Other->GetCapsuleComponent()->GetScaledCapsuleSize(OtherRadius, OtherHalfHeight);
	}
	float MyRadius = 0.0f;
	float MyHalfHeight = 0.0f;
	GetCapsuleComponent()->GetScaledCapsuleSize(MyRadius, MyHalfHeight);

	// 서버 판정만(클라이언트 판정 신뢰 안 함 — 기획 10장). 두 캡슐 표면 사이 ContactReach 이하 + 높이 겹침.
	const float Reach = ContactReach + ExtraReach;
	const FVector Delta = Pawn->GetActorLocation() - GetActorLocation();
	return Delta.Size2D() <= MyRadius + OtherRadius + Reach
		&& FMath::Abs(Delta.Z) <= MyHalfHeight + OtherHalfHeight + Reach;
}

bool ASpiderBot::IsValidTarget(const APawn* Pawn)
{
	// 사망·부활 대기 중(숨김) 플레이어는 쫓지 않는다
	return IsValid(Pawn) && !Pawn->IsHidden() && UCXPlayerLifeComponent::IsPawnAlive(Pawn) && !UCXHealthComponent::IsActorDead(Pawn);
}

void ASpiderBot::FaceTowards(const FVector& Location, float DeltaSeconds, bool bInstant)
{
	FVector To = Location - GetActorLocation();
	To.Z = 0.0f;
	if (To.IsNearlyZero())
	{
		return;
	}
	const FRotator Desired(0.0f, To.Rotation().Yaw, 0.0f);
	SetActorRotation(bInstant ? Desired : FMath::RInterpTo(GetActorRotation(), Desired, DeltaSeconds, 10.0f));
}

float ASpiderBot::GetTimeToNextChop() const
{
	if (State != ESpiderBotState::Attack)
	{
		return -1.0f;
	}
	const float Period = FMath::Max(0.2f, ChopPeriod);
	return FMath::Max(0.0f, (ChopsDone + FMath::Clamp(ChopImpactFraction, 0.0f, 1.0f)) * Period - StateTime);
}

// ---------------------------------------------------------------- 피해 / 사망

void ASpiderBot::ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse)
{
	// 총알 진입점(서버) → 엔진 TakeDamage → UCXHealthComponent(플레이어와 같은 경로 — 기획 6.2)
	if (!HasAuthority() || IsDead())
	{
		return;
	}
	const APawn* InstigatorPawn = Cast<APawn>(DamageCauser);
	AController* InstigatorController = InstigatorPawn ? InstigatorPawn->GetController() : nullptr;
	UGameplayStatics::ApplyDamage(this, Damage, InstigatorController, DamageCauser, UDamageType::StaticClass());
}

void ASpiderBot::ApplyHealing(float Healing, AActor* Healer)
{
	// 회복 없음(기획 범위 밖)
}

void ASpiderBot::HandleHealthChanged(float NewHealth, float Delta, AActor* DamageInstigator)
{
	if (Delta >= 0.0f || State == ESpiderBotState::Dead)
	{
		return;
	}
	// 피격 반응: 짧은 붉은 점멸 + 금속 덜컹(넉백 없음 — 5발 리듬 유지)
	StartHitFlash();
	PlaySfx(CXSfx::PickRandom(HitSounds), SmallAttenuation, 0.8f, FMath::FRandRange(0.95f, 1.1f));

	// 감지 전에 맞으면 쏜 플레이어를 바로 쫓는다(그로기 중에는 상태 유지 — 끝나면 Chase에서 가장 가까운 생존자)
	if (HasAuthority() && State == ESpiderBotState::Idle && NewHealth > 0.0f)
	{
		APawn* Shooter = Cast<APawn>(DamageInstigator);
		if (!Shooter && DamageInstigator)
		{
			Shooter = DamageInstigator->GetInstigator();
		}
		if (IsValidTarget(Shooter))
		{
			Target = Shooter;
			RetargetTimer = RetargetInterval;
			SetState(ESpiderBotState::Chase);
		}
	}
}

void ASpiderBot::HandleDepleted()
{
	HandleDeath();
}

void ASpiderBot::HandleDeath()
{
	if (!HasAuthority() || State == ESpiderBotState::Dead)
	{
		return;
	}
	// 시체는 조준·총알·해킹 대상에서 즉시 빠진다(캡슐 충돌 끔 + CanBeDamaged false + IsDead)
	SetCanBeDamaged(false);
	Target = nullptr;
	SetState(ESpiderBotState::Dead);
	SetLifeSpan(FMath::Max(0.1f, CorpseLifeSpan));
	DetachFromControllerPendingDestroy();
	UE_LOG(LogTemp, Log, TEXT("SpiderBot: %s destroyed"), *GetName());
}

// ---------------------------------------------------------------- 해킹 대상: 그로기 3초(해킹 시스템 기획 10장)

bool ASpiderBot::IsHackTargetAvailable() const
{
	// 살아 있고 그로기 중이 아닐 때만(그로기 중 재해킹 불가)
	return State != ESpiderBotState::Dead && State != ESpiderBotState::Stunned;
}

bool ASpiderBot::ApplyHackStun(APawn* Hacker)
{
	if (!HasAuthority() || !IsHackTargetAvailable())
	{
		return false;
	}
	// 대상은 유지(그로기가 끝나면 Chase가 즉시 재평가 — 죽었거나 멀면 다른 생존자)
	StunEndTime = GetServerTime() + FMath::Max(0.1f, StunDuration);
	GetCharacterMovement()->StopMovementImmediately();
	SetState(ESpiderBotState::Stunned);
	UE_LOG(LogTemp, Log, TEXT("SpiderBot: %s STUNNED %.1fs by %s at t=%.2f"), *GetName(), StunDuration, *GetNameSafe(Hacker), GetWorld()->GetTimeSeconds());
	return true;
}

bool ASpiderBot::ApplyDecoy(AActor* Lure, float Duration)
{
	if (!HasAuthority() || IsDead() || !IsValid(Lure) || Duration <= 0.0f)
	{
		return false;
	}
	DecoyLure = Lure;
	DecoyEndTime = GetServerTime() + Duration;
	if (State == ESpiderBotState::Attack || State == ESpiderBotState::Idle)
	{
		// 공격 루프를 끊고 걷기 연출(Chase)로
		SetState(ESpiderBotState::Chase);
	}
	UE_LOG(LogTemp, Log, TEXT("SpiderBot: %s DECOY %.1fs -> %s"), *GetName(), Duration, *GetNameSafe(Lure));
	return true;
}

bool ASpiderBot::IsDecoyed() const
{
	return DecoyLure.IsValid() && GetServerTime() < DecoyEndTime;
}

bool ASpiderBot::TickDecoy(float DeltaSeconds)
{
	if (!DecoyLure.IsValid())
	{
		return false;
	}
	const AActor* Lure = DecoyLure.Get();
	if (GetServerTime() >= DecoyEndTime || !IsValid(Lure))
	{
		// 미끼 끝 → 기존 감지 규칙으로 즉시 재평가(가장 가까운 생존자, 없으면 Idle)
		DecoyLure.Reset();
		RetargetTimer = 0.0f;
		SetState(ESpiderBotState::Chase);
		return false;
	}
	if (State != ESpiderBotState::Chase)
	{
		SetState(ESpiderBotState::Chase);
	}
	// 드론 아래 바닥 쪽으로(수평 이동만 — 발판 끝에서 떨어지지 않는 기존 이동 그대로). 가까우면 제자리에서 올려다본다.
	FVector Dir = Lure->GetActorLocation() - GetActorLocation();
	Dir.Z = 0.0f;
	FaceTowards(Lure->GetActorLocation(), DeltaSeconds, false);
	if (Dir.SizeSquared() > FMath::Square(60.0f))
	{
		AddMovementInput(Dir.GetSafeNormal());
	}
	return true;
}

float ASpiderBot::GetHackStunRemaining() const
{
	return State == ESpiderBotState::Stunned ? FMath::Max(0.0f, StunEndTime - GetServerTime()) : 0.0f;
}

float ASpiderBot::GetServerTime() const
{
	// 서버: 월드 시각(일시정지 중 멈춤) / 클라이언트: 서버 시각 추정 — 복제된 StunEndTime과 같은 시간축
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : World->GetTimeSeconds();
}

// ---------------------------------------------------------------- 연출(모든 머신)

void ASpiderBot::OnRep_State(ESpiderBotState PreviousState)
{
	ApplyStateVisuals(PreviousState);
}

void ASpiderBot::ApplyStateVisuals(ESpiderBotState PreviousState)
{
	if (PreviousState == ESpiderBotState::Attack && SawAudio)
	{
		SawAudio->FadeOut(0.15f, 0.0f);
	}
	if (PreviousState == ESpiderBotState::Stunned)
	{
		// 그로기 끝: 정상 복귀면 재부팅 연출, 그로기 중 사망이면 연출만 걷고 래그돌로
		ExitStunVisuals(State != ESpiderBotState::Dead);
	}
	if (State != ESpiderBotState::Dead && (PreviousState == ESpiderBotState::Attack || State == ESpiderBotState::Attack))
	{
		// 공격 루프 <-> Idle(걷기 대용) 전환. 사망은 래그돌이 맡으므로 애님을 건드리지 않는다.
		PlayStateAnimation();
	}

	switch (State)
	{
	case ESpiderBotState::Chase:
		if (PreviousState == ESpiderBotState::Idle)
		{
			// 대상 발견: 짧은 전자 경고음
			PlaySfx(AlertSound, MediumAttenuation, 0.9f, 0.85f);
		}
		break;

	case ESpiderBotState::Attack:
		// 공격 루프 애님(PlayStateAnimation) + 톱날 소리 루프(Attack 상태 동안 — SparkLoop02)
		PlaySfx(AttackWindupSound, SmallAttenuation);
		if (AttackSawSound)
		{
			if (!SawAudio)
			{
				SawAudio = UGameplayStatics::SpawnSoundAttached(AttackSawSound, GetRootComponent(), NAME_None, FVector::ZeroVector,
					EAttachLocation::KeepRelativeOffset, true, 0.7f, 1.2f, 0.0f, SmallAttenuation, nullptr, false);
				if (SawAudio)
				{
					SawAudio->SoundClassOverride = SfxClass;
				}
			}
			if (SawAudio)
			{
				SawAudio->Play();
			}
		}
		break;

	case ESpiderBotState::Stunned:
		EnterStunVisuals();
		break;

	case ESpiderBotState::Dead:
		EnterDeadVisuals();
		break;

	default:
		break;
	}
}

void ASpiderBot::EnterDeadVisuals()
{
	if (bDeadVisualsApplied)
	{
		return;
	}
	bDeadVisualsApplied = true;

	if (bStunVisualsActive)
	{
		ExitStunVisuals(false);
	}
	EndHitFlash();
	if (MoveAudio)
	{
		MoveAudio->Stop();
	}
	if (SawAudio)
	{
		SawAudio->Stop();
	}
	PlaySfx(DestroySound, MediumAttenuation, 1.0f, 0.7f);

	// 이동 정지 + 충돌 끔(총알·플레이어가 시체에 막히지 않음)
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->StopMovementImmediately();
	Move->DisableMovement();
	Move->SetComponentTickEnabled(false);

	// 사망 = 피직스 에셋 래그돌(사망 애님 없음 — 기획 13장 2). 플레이어·카메라와는 부딪히지 않게.
	USkeletalMeshComponent* MeshComp = GetMesh();
	MeshComp->SetRelativeLocation(MeshBaseLocation);
	MeshComp->SetPlayRate(1.0f);
	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	MeshComp->SetAllBodiesSimulatePhysics(true);
	MeshComp->SetSimulatePhysics(true);
	MeshComp->WakeAllRigidBodies();
	MeshComp->bBlendPhysics = true;
}

void ASpiderBot::PlayStateAnimation()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (State == ESpiderBotState::Attack && AttackAnim)
	{
		// 애님 1루프 = ChopPeriod(서버 타격 시점과 같은 시간축)
		MeshComp->PlayAnimation(AttackAnim, true);
		MeshComp->SetPlayRate(AttackAnim->GetPlayLength() / FMath::Max(0.2f, ChopPeriod));
	}
	else if (IdleAnim)
	{
		const UAnimSingleNodeInstance* Single = MeshComp->GetSingleNodeInstance();
		if (!Single || Single->GetAnimationAsset() != IdleAnim)
		{
			MeshComp->PlayAnimation(IdleAnim, true);
		}
	}
}

void ASpiderBot::UpdateLocomotionVisuals(float DeltaSeconds)
{
	// 임시 걷기: 이동 속도에 따라 Idle 재생 속도↑(걷기 애님 없음 — 기획 13장 2). 공격 루프는 ChopPeriod에 맞춘 고정 속도.
	const float Speed = GetVelocity().Size2D();
	const float Alpha = MoveSpeed > 0.0f ? FMath::Clamp(Speed / MoveSpeed, 0.0f, 1.0f) : 0.0f;
	if (State == ESpiderBotState::Stunned)
	{
		// 그로기 자세는 UpdateStunVisuals가 맡는다(재생 속도 0으로 고정)
		UpdateStunVisuals(DeltaSeconds);
	}
	else if (State != ESpiderBotState::Attack)
	{
		GetMesh()->SetPlayRate(FMath::Lerp(1.0f, MovePlayRate, Alpha));
	}

	// 이동 루프(움직일 때만, 속도로 음량·피치)
	if (MoveAudio && MoveLoopSound)
	{
		if (Alpha > 0.1f)
		{
			if (!MoveAudio->IsPlaying())
			{
				MoveAudio->Play(FMath::FRand() * 5.0f);
			}
			MoveAudio->SetVolumeMultiplier(MoveLoopVolume * Alpha);
			MoveAudio->SetPitchMultiplier(0.85f + 0.3f * Alpha);
		}
		else if (MoveAudio->IsPlaying())
		{
			MoveAudio->Stop();
		}
	}

	// 다리 딸깍(이동 거리 기반 — 애님 노티파이 대신, 플레이어 발소리와 같은 방식)
	if (StepSound && Alpha > 0.1f)
	{
		StepAccum += Speed * DeltaSeconds;
		if (StepAccum >= StepLength)
		{
			StepAccum = 0.0f;
			PlaySfx(StepSound, SmallAttenuation, 0.35f, FMath::FRandRange(1.55f, 1.75f));
		}
	}
}

void ASpiderBot::StartHitFlash()
{
	if (!HitFlashMaterial)
	{
		return;
	}
	USkeletalMeshComponent* MeshComp = GetMesh();
	for (int32 i = 0; i < MeshComp->GetNumMaterials(); ++i)
	{
		MeshComp->SetMaterial(i, HitFlashMaterial);
	}
	GetWorldTimerManager().SetTimer(HitFlashTimer, this, &ASpiderBot::EndHitFlash, FMath::Max(0.02f, HitFlashTime), false);
}

void ASpiderBot::EndHitFlash()
{
	GetWorldTimerManager().ClearTimer(HitFlashTimer);
	GetMesh()->EmptyOverrideMaterials();
}

// ---------------------------------------------------------------- 그로기 연출(모든 머신)

void ASpiderBot::SetAllMaterials(UMaterialInterface* Material)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!Material)
	{
		MeshComp->EmptyOverrideMaterials();
		return;
	}
	for (int32 i = 0; i < MeshComp->GetNumMaterials(); ++i)
	{
		MeshComp->SetMaterial(i, Material);
	}
}

void ASpiderBot::EnterStunVisuals()
{
	if (bStunVisualsActive || bDeadVisualsApplied)
	{
		return;
	}
	bStunVisualsActive = true;
	StunFlickerTimer = 0.0f;
	StunTwitchTimer = FMath::FRandRange(StunTwitchInterval.X, StunTwitchInterval.Y);
	StunTwitchLeft = 0.0f;

	if (MoveAudio)
	{
		MoveAudio->Stop();
	}
	if (SawAudio)
	{
		SawAudio->FadeOut(0.1f, 0.0f);
	}

	// 자세: 공격 애님의 "내려찍은 직후"(톱 팔이 바닥, 몸통 앞으로 숙임)에서 멈춤 + 메시만 주저앉기
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (AttackAnim)
	{
		MeshComp->PlayAnimation(AttackAnim, false);
		MeshComp->SetPosition(AttackAnim->GetPlayLength() * FMath::Clamp(StunPoseFraction, 0.0f, 1.0f), false);
	}
	MeshComp->SetPlayRate(0.0f);
	MeshComp->SetRelativeLocation(MeshBaseLocation - FVector(0.0f, 0.0f, StunSlumpDepth));

	// 전원 차단음 + 스파크 루프 + 파티클 + 전기 불빛
	PlaySfx(StunStartSound, MediumAttenuation, 1.0f, 1.15f);
	if (StunLoopSound)
	{
		if (!StunAudio)
		{
			StunAudio = UGameplayStatics::SpawnSoundAttached(StunLoopSound, GetRootComponent(), NAME_None, FVector::ZeroVector,
				EAttachLocation::KeepRelativeOffset, true, 0.55f, 1.25f, 0.0f, SmallAttenuation, nullptr, false);
			if (StunAudio)
			{
				StunAudio->SoundClassOverride = SfxClass;
			}
		}
		if (StunAudio)
		{
			StunAudio->Play();
		}
	}
	if (StunSparksTemplate && !StunSparks)
	{
		StunSparks = UGameplayStatics::SpawnEmitterAttached(StunSparksTemplate, GetRootComponent(), NAME_None, FVector(0.0f, 0.0f, 35.0f),
			FRotator::ZeroRotator, FVector(StunSparksScale), EAttachLocation::KeepRelativeOffset, false);
	}
	if (StunLight)
	{
		StunLight->SetVisibility(true);
		StunLight->SetLightColor(StunLightColorA);
		StunLight->SetIntensity(StunLightIntensity);
	}
}

void ASpiderBot::ExitStunVisuals(bool bReboot)
{
	if (!bStunVisualsActive)
	{
		return;
	}
	bStunVisualsActive = false;

	SetAllMaterials(nullptr);
	USkeletalMeshComponent* MeshComp = GetMesh();
	MeshComp->SetRelativeLocation(MeshBaseLocation);
	if (StunAudio)
	{
		StunAudio->FadeOut(0.2f, 0.0f);
	}
	if (StunSparks)
	{
		StunSparks->DeactivateSystem();
		StunSparks->bAutoDestroy = true;
		StunSparks = nullptr;
	}
	if (StunLight)
	{
		StunLight->SetIntensity(0.0f);
		StunLight->SetVisibility(false);
	}

	if (bReboot)
	{
		// 재부팅: 네온 켜지는 소리 + Idle(걷기 대용)로 복귀
		PlaySfx(RebootSound, MediumAttenuation, 0.9f, 0.9f);
		MeshComp->SetPlayRate(1.0f);
		PlayStateAnimation();
	}
}

void ASpiderBot::UpdateStunVisuals(float DeltaSeconds)
{
	if (!bStunVisualsActive)
	{
		// 클라이언트가 Stunned 상태로 처음 받았을 때 등 — 연출이 아직 없으면 지금 시작
		EnterStunVisuals();
		if (!bStunVisualsActive)
		{
			return;
		}
	}
	USkeletalMeshComponent* MeshComp = GetMesh();
	const float Remaining = GetHackStunRemaining();
	const bool bRebooting = Remaining > 0.0f && Remaining < 0.45f;

	// 머티리얼·불빛 깜빡임(0.04~0.12초마다): 원래 / 정전(검정) / 과부하(발광). 끝나기 직전엔 발광 고정 = 재부팅 신호.
	StunFlickerTimer -= DeltaSeconds;
	if (StunFlickerTimer <= 0.0f)
	{
		StunFlickerTimer = FMath::FRandRange(0.04f, 0.12f);
		const float Roll = FMath::FRand();
		if (bRebooting)
		{
			SetAllMaterials(StunSurgeMaterial ? StunSurgeMaterial.Get() : HitFlashMaterial.Get());
		}
		else if (Roll < 0.3f)
		{
			SetAllMaterials(StunDropoutMaterial);
		}
		else if (Roll < 0.45f)
		{
			SetAllMaterials(StunSurgeMaterial ? StunSurgeMaterial.Get() : HitFlashMaterial.Get());
		}
		else
		{
			SetAllMaterials(nullptr);
		}
		if (StunLight)
		{
			StunLight->SetLightColor(FMath::RandBool() ? StunLightColorA : StunLightColorB);
			StunLight->SetIntensity(StunLightIntensity * (bRebooting ? 0.25f : FMath::FRandRange(0.15f, 1.0f)));
		}
	}

	// 경련: 가끔 0.08초 동안 톱 팔이 살짝 들렸다 떨어지고(애님 위치 점프 — 내려찍은 뒤 복귀 구간 78~90%) 몸이 떨린다
	const float PoseTime = AttackAnim ? AttackAnim->GetPlayLength() * FMath::Clamp(StunPoseFraction, 0.0f, 1.0f) : 0.0f;
	if (StunTwitchLeft > 0.0f)
	{
		StunTwitchLeft -= DeltaSeconds;
		MeshComp->SetRelativeLocation(MeshBaseLocation - FVector(0.0f, 0.0f, StunSlumpDepth) + FMath::VRand() * 2.5f);
		if (StunTwitchLeft <= 0.0f)
		{
			MeshComp->SetRelativeLocation(MeshBaseLocation - FVector(0.0f, 0.0f, StunSlumpDepth));
			if (AttackAnim)
			{
				MeshComp->SetPosition(PoseTime, false);
			}
		}
	}
	else
	{
		StunTwitchTimer -= DeltaSeconds;
		if (StunTwitchTimer <= 0.0f && !bRebooting)
		{
			StunTwitchTimer = FMath::FRandRange(StunTwitchInterval.X, StunTwitchInterval.Y);
			StunTwitchLeft = 0.08f;
			if (AttackAnim)
			{
				MeshComp->SetPosition(AttackAnim->GetPlayLength() * FMath::FRandRange(0.78f, 0.9f), false);
			}
		}
	}
	MeshComp->SetPlayRate(0.0f);
}

void ASpiderBot::PlaySfx(USoundBase* Sound, USoundAttenuation* Attenuation, float Volume, float Pitch) const
{
	// SoulCity·UI 원본 에셋은 사운드 클래스/감쇠가 없거나 UI용이라 게임플레이 클래스(SCl_SFX)·감쇠로 덮어써 재생
	CXSfx::PlayAtLocation(this, Sound, GetActorLocation(), Volume, Pitch, Attenuation, nullptr, SfxClass);
}

FString ASpiderBot::GetDebugState() const
{
	static const TCHAR* StateNames[] = { TEXT("Idle"), TEXT("Chase"), TEXT("Attack"), TEXT("Dead"), TEXT("Stunned") };
	const APawn* Current = Target.Get();
	return FString::Printf(TEXT("%s state=%s hp=%.0f target=%s chops=%d nextChop=%.2f stun=%.2f speed=%.0f loc=%s"), *GetName(),
		StateNames[FMath::Clamp(static_cast<int32>(State), 0, 4)], Health ? Health->GetHealth() : -1.0f,
		Current ? *Current->GetName() : TEXT("none"), ChopsDone, GetTimeToNextChop(), GetHackStunRemaining(), GetVelocity().Size2D(),
		*GetActorLocation().ToCompactString());
}
