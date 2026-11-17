// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXHealthComponent.h"
#include "CXPlayerLifeComponent.h"
#include "CXSfx.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"

UCXHealthComponent::UCXHealthComponent()
{
	// 움찔 연출 중에만 틱(StartFlinch에서 켠다)
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// 런타임에 붙는 컴포넌트라 원격 머신에도 생기도록 복제(UCXPlayerLifeComponent와 같은 방식)
	SetIsReplicatedByDefault(true);

	// 피격음: 별도 에셋 없이 레일건 금속 충돌음을 낮은 피치로 재사용(전투 시스템 기획 9.3 — 신규 임포트 없음)
	for (const TCHAR* Path : {
		TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Impact_Wall_01.SW_Brother_Impact_Wall_01"),
		TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Impact_Wall_02.SW_Brother_Impact_Wall_02"),
		TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Impact_Wall_03.SW_Brother_Impact_Wall_03") })
	{
		if (USoundBase* Sound = CXSfx::Find<USoundBase>(Path))
		{
			HurtSounds.Add(Sound);
		}
	}
}

UCXHealthComponent* UCXHealthComponent::EnsureOn(APawn* Pawn)
{
	if (!Pawn)
	{
		return nullptr;
	}
	if (UCXHealthComponent* Existing = Pawn->FindComponentByClass<UCXHealthComponent>())
	{
		return Existing;
	}
	// BP_Brother처럼 C++ 부모가 없는 폰도 에셋을 고치지 않고 런타임에 붙인다
	UCXHealthComponent* Component = NewObject<UCXHealthComponent>(Pawn, TEXT("CXHealth"));
	Pawn->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

UCXHealthComponent* UCXHealthComponent::FindOn(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UCXHealthComponent>() : nullptr;
}

bool UCXHealthComponent::IsActorDead(const AActor* Actor)
{
	const UCXHealthComponent* Health = FindOn(Actor);
	return Health && Health->IsDead();
}

void UCXHealthComponent::InitMaxHealth(float NewMaxHealth)
{
	MaxHealth = FMath::Max(1.0f, NewMaxHealth);
	CurrentHealth = MaxHealth;
	bDepleted = false;
}

void UCXHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCXHealthComponent, CurrentHealth);
	DOREPLIFETIME(UCXHealthComponent, MaxHealth);
	DOREPLIFETIME(UCXHealthComponent, ShieldEndTime);
}

void UCXHealthComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		// 서버만 피해를 받는다(클라이언트는 복제값만). 시작 체력 = 최대치.
		CurrentHealth = MaxHealth;
		bDepleted = false;
		if (!bDamageBound)
		{
			Owner->OnTakeAnyDamage.AddDynamic(this, &UCXHealthComponent::HandleTakeAnyDamage);
			bDamageBound = true;
		}
	}
}

// ---------------------------------------------------------------- 피해(서버)

void UCXHealthComponent::HandleTakeAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Damage <= 0.0f || bDepleted)
	{
		return;
	}
	// 사망·부활 대기 중이면 무시(엔진 CanBeDamaged 검사와 이중 안전장치 — 기획 6.2 서버 처리 순서)
	UCXPlayerLifeComponent* Life = Owner->FindComponentByClass<UCXPlayerLifeComponent>();
	if (Life && !Life->IsAlive())
	{
		return;
	}
	// 제미니 보호막 중이면 무시
	if (GetShieldRemaining() > 0.0f)
	{
		return;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth - Damage, 0.0f, MaxHealth);
	AActor* Instigator = DamageCauser ? DamageCauser : (InstigatedBy ? InstigatedBy->GetPawn() : nullptr);
	UE_LOG(LogTemp, Log, TEXT("CXHealth: %s took %.0f -> %.0f/%.0f"), *Owner->GetName(), Damage, CurrentHealth, MaxHealth);

	NotifyHealthChanged(OldHealth, Instigator);

	if (CurrentHealth <= 0.0f)
	{
		// 0이면 1회만
		bDepleted = true;
		OnDepleted.Broadcast();
		if (bKillOwnerOnDepleted && Life)
		{
			Life->Kill(ECXDeathCause::Health);
		}
	}
	else if (Life && HitInvulnerableTime > 0.0f)
	{
		// 피격 무적 1초(거미 2마리 동시 접촉 시 순간 60 피해 방지 — 기획 8장). 점멸은 LifeComponent가 처리
		Life->StartInvulnerability(HitInvulnerableTime);
	}
}

void UCXHealthComponent::ResetToMax()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	const float OldHealth = CurrentHealth;
	CurrentHealth = MaxHealth;
	bDepleted = false;
	if (OldHealth != CurrentHealth)
	{
		NotifyHealthChanged(OldHealth, nullptr);
	}
}

// ---------------------------------------------------------------- 제미니 회복 / 보호막(서버)

double UCXHealthComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

float UCXHealthComponent::Heal(float Amount)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Amount <= 0.0f || bDepleted || CurrentHealth <= 0.0f)
	{
		return 0.0f;
	}
	const UCXPlayerLifeComponent* Life = Owner->FindComponentByClass<UCXPlayerLifeComponent>();
	if (Life && !Life->IsAlive())
	{
		return 0.0f;
	}
	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Clamp(CurrentHealth + Amount, 0.0f, MaxHealth);
	if (CurrentHealth != OldHealth)
	{
		UE_LOG(LogTemp, Log, TEXT("CXHealth: %s healed %.0f -> %.0f/%.0f"), *Owner->GetName(), CurrentHealth - OldHealth, CurrentHealth, MaxHealth);
		NotifyHealthChanged(OldHealth, nullptr);
	}
	return CurrentHealth - OldHealth;
}

bool UCXHealthComponent::GrantShield(float Duration)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Duration <= 0.0f || CurrentHealth <= 0.0f)
	{
		return false;
	}
	const UCXPlayerLifeComponent* Life = Owner->FindComponentByClass<UCXPlayerLifeComponent>();
	if (Life && !Life->IsAlive())
	{
		return false;
	}
	ShieldEndTime = GetServerTime() + Duration;
	UE_LOG(LogTemp, Log, TEXT("CXHealth: %s shield %.1fs"), *Owner->GetName(), Duration);
	return true;
}

void UCXHealthComponent::ClearShield()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ShieldEndTime = 0.0;
	}
}

float UCXHealthComponent::GetShieldRemaining() const
{
	if (ShieldEndTime <= 0.0)
	{
		return 0.0f;
	}
	return FMath::Max(0.0f, static_cast<float>(ShieldEndTime - GetServerTime()));
}

// ---------------------------------------------------------------- 연출(모든 머신)

void UCXHealthComponent::OnRep_Health(float OldHealth)
{
	NotifyHealthChanged(OldHealth, nullptr);
}

void UCXHealthComponent::NotifyHealthChanged(float OldHealth, AActor* Instigator)
{
	const float Delta = CurrentHealth - OldHealth;
	if (Delta < 0.0f)
	{
		LastDamageTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

		if (bFlinchOnHit)
		{
			StartFlinch();
		}
		if (USoundBase* Sound = CXSfx::PickRandom(HurtSounds))
		{
			UGameplayStatics::PlaySoundAtLocation(this, Sound, GetOwner()->GetActorLocation(), 1.0f, HurtPitch);
		}
	}
	OnHealthChanged.Broadcast(CurrentHealth, Delta, Instigator);
}

void UCXHealthComponent::StartFlinch()
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh)
	{
		return;
	}
	if (!bFlinching)
	{
		// 이미 움찔 중이면 기준 위치는 그대로(누적 방지)
		FlinchBaseLocation = Mesh->GetRelativeLocation();
	}
	bFlinching = true;
	FlinchElapsed = 0.0f;
	SetComponentTickEnabled(true);
}

void UCXHealthComponent::StopFlinch()
{
	if (!bFlinching)
	{
		return;
	}
	bFlinching = false;
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			Mesh->SetRelativeLocation(FlinchBaseLocation);
		}
	}
	SetComponentTickEnabled(false);
}

void UCXHealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bFlinching)
	{
		SetComponentTickEnabled(false);
		return;
	}
	FlinchElapsed += DeltaTime;
	const float Duration = FMath::Max(0.01f, FlinchDuration);
	if (FlinchElapsed >= Duration)
	{
		StopFlinch();
		return;
	}
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh)
	{
		StopFlinch();
		return;
	}
	// 캡슐 기준 뒤(-X)로 밀렸다 돌아오는 곡선 + 짧은 좌우 떨림. 캡슐(=액터) 위치는 바꾸지 않는다.
	const float T = FlinchElapsed / Duration;
	const float Back = FMath::Sin(T * PI) * FlinchDistance;
	const float Shake = FMath::Sin(FlinchElapsed * 90.0f) * (1.0f - T) * FlinchDistance * 0.4f;
	Mesh->SetRelativeLocation(FlinchBaseLocation + FVector(-Back, Shake, 0.0f));
}

// ---------------------------------------------------------------- 조회

float UCXHealthComponent::GetTimeSinceLastDamage() const
{
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds() - LastDamageTime) : 1000.0f;
}

FString UCXHealthComponent::GetDebugState() const
{
	return FString::Printf(TEXT("hp=%.0f/%.0f dead=%d sinceHit=%.2f shield=%.1f"), CurrentHealth, MaxHealth, IsDead() ? 1 : 0, GetTimeSinceLastDamage(), GetShieldRemaining());
}
