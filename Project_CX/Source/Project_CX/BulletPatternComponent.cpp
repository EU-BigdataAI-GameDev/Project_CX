#include "BulletPatternComponent.h"
#include "BulletBase.h"
#include "BulletPoolSubsystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

UBulletPatternComponent::UBulletPatternComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBulletPatternComponent::BeginPlay()
{
	Super::BeginPlay();

	CurrentAngle = BaseAngle;

	if (UBulletPoolSubsystem* Pool = GetWorld()->GetSubsystem<UBulletPoolSubsystem>())
	{
		Pool->Prewarm(BulletClass, PrewarmCount);
	}

	if (bAutoStart)
	{
		StartFiring();
	}
}

void UBulletPatternComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopFiring();
	Super::EndPlay(EndPlayReason);
}

void UBulletPatternComponent::StartFiring()
{
	StopFiring();
	CurrentAngle = BaseAngle;
	FireOnce();
	GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &UBulletPatternComponent::FireOnce, FireInterval, true);
}

void UBulletPatternComponent::StopFiring()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FireTimer);
	}
}

void UBulletPatternComponent::FireOnce()
{
	AActor* Owner = GetOwner();
	if (!Owner || !BulletClass)
	{
		return;
	}

	const FVector Origin = Owner->GetActorLocation() + SpawnOffset;

	switch (Pattern)
	{
	case EBulletPattern::Ring:
		FireRing(Origin, BaseAngle);
		break;

	case EBulletPattern::Spiral:
		FireRing(Origin, CurrentAngle);
		CurrentAngle = FMath::Fmod(CurrentAngle + RotatePerShot, 360.f);
		break;

	case EBulletPattern::Fan:
		FireFan(Origin, BaseAngle);
		break;

	case EBulletPattern::Aimed:
		FireFan(Origin, GetAngleToPlayer(Origin));
		break;
	}
}

void UBulletPatternComponent::FireRing(const FVector& Origin, float StartAngle)
{
	const float Step = 360.f / BulletCount;
	for (int32 i = 0; i < BulletCount; ++i)
	{
		SpawnBullet(Origin, StartAngle + Step * i);
	}
}

void UBulletPatternComponent::FireFan(const FVector& Origin, float CenterAngle)
{
	if (BulletCount == 1)
	{
		SpawnBullet(Origin, CenterAngle);
		return;
	}

	const float Start = CenterAngle - SpreadAngle * 0.5f;
	const float Step = SpreadAngle / (BulletCount - 1);
	for (int32 i = 0; i < BulletCount; ++i)
	{
		SpawnBullet(Origin, Start + Step * i);
	}
}

void UBulletPatternComponent::SpawnBullet(const FVector& Origin, float AngleDeg)
{
	UBulletPoolSubsystem* Pool = GetWorld()->GetSubsystem<UBulletPoolSubsystem>();
	if (!Pool)
	{
		return;
	}

	if (ABulletBase* Bullet = Pool->GetBullet(BulletClass))
	{
		Bullet->Fire(Origin, AngleDeg, BulletSpeed);
	}
}

float UBulletPatternComponent::GetAngleToPlayer(const FVector& Origin) const
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return BaseAngle;
	}

	const FVector ToPlayer = Player->GetActorLocation() - Origin;
	return FMath::RadiansToDegrees(FMath::Atan2(ToPlayer.Z, ToPlayer.X));
}
