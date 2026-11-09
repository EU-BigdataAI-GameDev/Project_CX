// Copyright Epic Games, Inc. All Rights Reserved.

#include "BrotherBullet.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "CombatDamageable.h"
#include "CXHealthComponent.h"
#include "CXSfx.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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
	// 지형은 막힘(Block)으로 받는다 — 레벨 벽/COL 박스는 Generate Overlap Events가 꺼져 있어 겹침으로는 감지되지 않아
	// 총알이 벽을 뚫고 지나갔다(자동 조준 사격 구현.md 체크리스트: 지형 충돌 시 소멸). 막히면 ProjectileMovement가 멈추고 OnBulletStop.
	CollisionComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
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

	for (const TCHAR* Path : {
		TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Railgun_Fire_01.SW_Brother_Railgun_Fire_01"),
		TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Railgun_Fire_02.SW_Brother_Railgun_Fire_02"),
		TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Railgun_Fire_03.SW_Brother_Railgun_Fire_03") })
	{
		if (USoundBase* Sound = CXSfx::Find<USoundBase>(Path))
		{
			FireSounds.Add(Sound);
		}
	}
	for (const TCHAR* Path : {
		TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Impact_Wall_01.SW_Brother_Impact_Wall_01"),
		TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Impact_Wall_02.SW_Brother_Impact_Wall_02"),
		TEXT("/Game/Project_CX/Audio/SFX/Brother/SW_Brother_Impact_Wall_03.SW_Brother_Impact_Wall_03") })
	{
		if (USoundBase* Sound = CXSfx::Find<USoundBase>(Path))
		{
			ImpactWallSounds.Add(Sound);
		}
	}
	// 적 명중음(SFX_Brother_Impact_Enemy — 보류 해제): 같은 금속 충돌음을 높은 피치로 재사용(신규 임포트 없음)
	ImpactEnemySounds = ImpactWallSounds;
}

void ABrotherBullet::BeginPlay()
{
	Super::BeginPlay();
	CollisionComp->OnComponentBeginOverlap.AddDynamic(this, &ABrotherBullet::OnBulletOverlap);
	ProjectileMovement->OnProjectileStop.AddDynamic(this, &ABrotherBullet::OnBulletStop);

	// 발사음: 스폰 위치(총구)에 고정 — 총알을 따라가지 않게 위치 재생
	if (USoundBase* FireSound = CXSfx::PickRandom(FireSounds))
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, GetActorLocation());
	}
}

void ABrotherBullet::OnBulletOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 다른 총알끼리는 무시(연사 시 서로 지워지지 않게)
	if (!IsValid(OtherActor) || OtherActor == this || OtherActor == GetOwner() || OtherActor == GetInstigator() || OtherActor->IsA<ABrotherBullet>())
	{
		return;
	}

	// 아군(여동생 등 ICombatDamageable이 아닌 폰)과 시체(체력 0)는 통과 — 아군 사격 없음, 죽은 적에 막히지 않음(전투 시스템 기획 5장, 12장)
	const bool bDamageable = OtherActor->Implements<UCombatDamageable>();
	if ((!bDamageable && OtherActor->IsA<APawn>()) || (bDamageable && (UCXHealthComponent::IsActorDead(OtherActor) || !OtherActor->CanBeDamaged())))
	{
		return;
	}

	// 자동 조준 사격 구현.md 7장: 데미지 확정은 서버 권위로만 처리한다. 클라이언트에서 로컬 시뮬레이션된
	// 총알도 이 함수는 타지만, 실제 ApplyDamage는 서버(HasAuthority)에서만 호출한다.
	// 데미지를 받지 않는 사물(겹침 이벤트를 켠 동적 소품 등)에 맞으면 충돌음. 캐릭터·다른 총알은 제외.
	if (!bDamageable)
	{
		PlayImpactSound(bFromSweep ? FVector(SweepResult.ImpactPoint) : GetActorLocation());
	}
	else if (bDamageable && !bImpactPlayed)
	{
		// 적 명중음: 모든 머신에서 각자(총알 겹침은 클라이언트 시뮬레이션에서도 발생 — 효과음 적용 기획 6.5)
		bImpactPlayed = true;
		if (USoundBase* EnemySound = CXSfx::PickRandom(ImpactEnemySounds))
		{
			UGameplayStatics::PlaySoundAtLocation(this, EnemySound, bFromSweep ? FVector(SweepResult.ImpactPoint) : GetActorLocation(), 1.0f, ImpactEnemyPitch);
		}
	}

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

void ABrotherBullet::OnBulletStop(const FHitResult& ImpactResult)
{
	// 벽·바닥(WorldStatic)에 막힘. 연출 소리는 모든 머신에서 각자, 소멸은 서버 권위(클라이언트의 Destroy는 무시된다).
	PlayImpactSound(ImpactResult.bBlockingHit ? FVector(ImpactResult.ImpactPoint) : GetActorLocation());
	Destroy();
}

void ABrotherBullet::PlayImpactSound(const FVector& Location)
{
	if (bImpactPlayed)
	{
		return;
	}
	bImpactPlayed = true;
	if (USoundBase* ImpactSound = CXSfx::PickRandom(ImpactWallSounds))
	{
		UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Location);
	}
}
