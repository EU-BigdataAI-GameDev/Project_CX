// Copyright Epic Games, Inc. All Rights Reserved.

#include "BrotherBullet.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "CombatDamageable.h"

ABrotherBullet::ABrotherBullet()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	CollisionComp = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComp"));
	CollisionComp->InitSphereRadius(5.0f);
	CollisionComp->SetMobility(EComponentMobility::Movable);
	CollisionComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionComp->SetGenerateOverlapEvents(true);
	SetRootComponent(CollisionComp);

	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	MeshComp->SetupAttachment(RootComponent);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionComp;
	ProjectileMovement->InitialSpeed = BulletSpeed;
	ProjectileMovement->MaxSpeed = BulletSpeed;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bInitialVelocityInLocalSpace = true;
	ProjectileMovement->ProjectileGravityScale = 0.0f;

	InitialLifeSpan = BulletLifeSpan;
}

void ABrotherBullet::BeginPlay()
{
	Super::BeginPlay();
	CollisionComp->OnComponentBeginOverlap.AddDynamic(this, &ABrotherBullet::OnBulletOverlap);
}

void ABrotherBullet::OnBulletOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!IsValid(OtherActor) || OtherActor == this || OtherActor == GetOwner() || OtherActor == GetInstigator())
	{
		return;
	}

	// 공격 시스템.md 7장: 데미지 확정은 서버 권위로만 처리한다. 클라이언트에서 로컬 시뮬레이션된
	// 총알도 이 함수는 타지만, 실제 ApplyDamage는 서버(HasAuthority)에서만 호출한다.
	if (HasAuthority())
	{
		if (ICombatDamageable* Damageable = Cast<ICombatDamageable>(OtherActor))
		{
			const FVector HitLocation = bFromSweep ? FVector(SweepResult.ImpactPoint) : GetActorLocation();
			Damageable->ApplyDamage(Damage, GetOwner(), HitLocation, GetVelocity().GetSafeNormal());
		}
	}

	Destroy();
}
