#include "BulletBase.h"
#include "BulletPoolSubsystem.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"

ABulletBase::ABulletBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(12.f);
	Collision->SetCollisionProfileName(TEXT("OverlapOnlyPawn"));
	Collision->SetGenerateOverlapEvents(true);
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
}

void ABulletBase::BeginPlay()
{
	Super::BeginPlay();

	Collision->OnComponentBeginOverlap.AddDynamic(this, &ABulletBase::OnOverlap);

	// 풀에서 스폰된 직후에는 비활성 상태로 시작
	if (!bActive)
	{
		SetVisualActive(false);
	}
}

void ABulletBase::Fire(const FVector& StartLocation, float InAngleDeg, float InSpeed)
{
	FixedY = StartLocation.Y;
	AngleDeg = InAngleDeg;
	Speed = InSpeed;
	Age = 0.f;
	bActive = true;

	SetActorLocationAndRotation(StartLocation, FRotator(AngleDeg, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	SetVisualActive(true);
}

void ABulletBase::Deactivate()
{
	if (!bActive)
	{
		return;
	}

	bActive = false;
	SetVisualActive(false);

	if (UWorld* World = GetWorld())
	{
		if (UBulletPoolSubsystem* Pool = World->GetSubsystem<UBulletPoolSubsystem>())
		{
			Pool->ReturnBullet(this);
		}
	}
}

void ABulletBase::SetVisualActive(bool bNewActive)
{
	SetActorHiddenInGame(!bNewActive);
	SetActorEnableCollision(bNewActive);
	SetActorTickEnabled(bNewActive);
}

void ABulletBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bActive)
	{
		return;
	}

	Age += DeltaSeconds;
	if (Age >= LifeTime)
	{
		Deactivate();
		return;
	}

	Speed += Acceleration * DeltaSeconds;
	AngleDeg += AngularSpeed * DeltaSeconds;

	// X-Z 평면 방향 벡터 (0도 = +X, 90도 = +Z)
	const float Rad = FMath::DegreesToRadians(AngleDeg);
	const FVector Dir(FMath::Cos(Rad), 0.f, FMath::Sin(Rad));

	FVector NewLocation = GetActorLocation() + Dir * Speed * DeltaSeconds;
	NewLocation.Y = FixedY; // 평면 밖으로 절대 안 나가게 고정

	SetActorLocationAndRotation(NewLocation, FRotator(AngleDeg, 0.f, 0.f));
}

void ABulletBase::OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bActive)
	{
		return;
	}

	// 플레이어가 조종하는 폰에만 반응 (보스나 다른 적은 무시)
	APawn* HitPawn = Cast<APawn>(OtherActor);
	if (HitPawn && HitPawn->IsPlayerControlled())
	{
		UGameplayStatics::ApplyDamage(HitPawn, Damage, nullptr, this, UDamageType::StaticClass());
		Deactivate();
	}
}
