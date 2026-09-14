// Copyright Epic Games, Inc. All Rights Reserved.

#include "BrotherCombatComponent.h"
#include "BrotherBullet.h"
#include "CombatDamageable.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"

UBrotherCombatComponent::UBrotherCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UBrotherCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentAmmo = MaxAmmo;
}

void UBrotherCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bInputBound)
	{
		TryBindInput();
	}

	if (!bIsAiming)
	{
		return;
	}

	// 엣지 케이스(공격 시스템.md 2.3): 타겟이 죽거나 사거리를 벗어나면 다음으로 가까운 적으로 재탐색.
	if (!IsValid(CurrentTarget))
	{
		CurrentTarget = FindNearestTarget();
		if (!CurrentTarget)
		{
			bIsAiming = false;
			return;
		}
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
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

	bInputBound = true;
}

AActor* UBrotherCombatComponent::FindNearestTarget() const
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
	float BestDistSq = FMath::Square(AimRange);

	for (AActor* Candidate : Candidates)
	{
		if (!IsValid(Candidate) || Candidate == GetOwner())
		{
			continue;
		}

		const FVector ToCandidate = Candidate->GetActorLocation() - OwnerLocation;
		const float DistSq = ToCandidate.SizeSquared();
		if (DistSq > BestDistSq)
		{
			continue;
		}

		const FVector ToCandidateDir = ToCandidate.GetSafeNormal();
		if (FVector::DotProduct(OwnerForward, ToCandidateDir) < ConeCos)
		{
			continue;
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
	CurrentTarget = FindNearestTarget();
	bIsAiming = (CurrentTarget != nullptr);
}

void UBrotherCombatComponent::StopAim()
{
	bIsAiming = false;
	CurrentTarget = nullptr;
}

void UBrotherCombatComponent::Fire()
{
	if (bIsReloading)
	{
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return;
	}

	const FVector SpawnLocation = OwnerCharacter->GetActorLocation() + OwnerCharacter->GetActorRotation().RotateVector(MuzzleOffset);

	FVector AimDirection;
	if (bIsAiming && IsValid(CurrentTarget))
	{
		AimDirection = (CurrentTarget->GetActorLocation() - SpawnLocation).GetSafeNormal();
	}
	else
	{
		// 공격 시스템.md 3.2: 조준 중이 아니면 이번 한 발만 즉시 재탐색한 방향으로 나간다
		// (bIsAiming을 true로 바꾸지는 않는다 — 캐릭터가 계속 그 타겟을 보게 만들지 않기 위함).
		AActor* OneShotTarget = FindNearestTarget();
		AimDirection = OneShotTarget
			? (OneShotTarget->GetActorLocation() - SpawnLocation).GetSafeNormal()
			: OwnerCharacter->GetActorForwardVector();
	}

	CurrentAmmo = FMath::Max(CurrentAmmo - 1, 0);
	if (CurrentAmmo <= 0)
	{
		StartReload();
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
	SpawnBullet(AimDirection);
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
		Bullet->SetDamage(BulletDamage);
	}
}

void UBrotherCombatComponent::StartReload()
{
	bIsReloading = true;
	GetWorld()->GetTimerManager().SetTimer(ReloadTimerHandle, this, &UBrotherCombatComponent::FinishReload, ReloadTime, false);
}

void UBrotherCombatComponent::FinishReload()
{
	CurrentAmmo = MaxAmmo;
	bIsReloading = false;
}
