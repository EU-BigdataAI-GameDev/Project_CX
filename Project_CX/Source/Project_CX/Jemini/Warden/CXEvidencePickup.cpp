// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXEvidencePickup.h"
#include "CXWardenTerminal.h"
#include "CoopInteractComponent.h"
#include "CXPlayerLifeComponent.h"
#include "CXShapeKit.h"
#include "CXSfx.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace CXEvidenceLook
{
	constexpr int32 SparkleCount = 8;
	constexpr float BeamHeight = 420.0f;
}

ACXEvidencePickup::ACXEvidencePickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(CXShapeKit::CubePath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> NeonFinder(CXShapeKit::NeonMaterialPath);
	CubeMesh = CubeFinder.Object;
	NeonMaterial = NeonFinder.Object;
	PickupSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Sister/SW_Hack_Granted.SW_Hack_Granted"));

	// 루트 = 겹침 구(바닥에서 HoverHeight 위 칩 중심). 레인 깊이(X) 어디서 지나가도 닿게 넉넉히.
	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(110.0f);
	Trigger->SetCollisionProfileName(TEXT("Trigger"));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->SetCanEverAffectNavigation(false);
	RootComponent = Trigger;

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Trigger);

	Chip = CXShapeKit::MakeCube(this, TEXT("Chip"), Visual, CubeMesh, FVector::ZeroVector, FVector(10.0f, 34.0f, 44.0f));
	ChipFrame = CXShapeKit::MakeCube(this, TEXT("ChipFrame"), Visual, CubeMesh, FVector::ZeroVector, FVector(6.0f, 46.0f, 56.0f));
	// 빛기둥: 칩 위로 길게(사이드뷰에서 멀리서도 보이는 세로 줄)
	Beam = CXShapeKit::MakeCube(this, TEXT("Beam"), Trigger, CubeMesh, FVector(0.0f, 0.0f, CXEvidenceLook::BeamHeight * 0.5f - 40.0f), FVector(3.0f, 6.0f, CXEvidenceLook::BeamHeight));
	// 바닥 고리(납작한 판) — 착지 위치 표시
	Ring = CXShapeKit::MakeCube(this, TEXT("Ring"), Trigger, CubeMesh, FVector(0.0f, 0.0f, -70.0f), FVector(90.0f, 90.0f, 2.0f), FRotator(0.0f, 45.0f, 0.0f));
	for (int32 i = 0; i < CXEvidenceLook::SparkleCount; ++i)
	{
		Sparkles.Add(CXShapeKit::MakeCube(this, *FString::Printf(TEXT("Sparkle%d"), i), Trigger, CubeMesh, FVector::ZeroVector, FVector(7.0f), FRotator(45.0f, 0.0f, 45.0f)));
	}

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Trigger);
	Glow->SetRelativeLocation(FVector(-60.0f, 0.0f, 20.0f));
	Glow->SetIntensity(6000.0f);
	Glow->SetAttenuationRadius(650.0f);
	Glow->SetCastShadows(false);
}

ACXEvidencePickup* ACXEvidencePickup::SpawnDrop(UWorld* World, FName InEvidenceId, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	// 바닥 찾기(정적 지형만 — 폰·시체 무시)
	FVector Spawn = Location + FVector(0.0f, 0.0f, 75.0f);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXEvidenceDrop), false);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	if (World->LineTraceSingleByObjectType(Hit, Location + FVector(0.0f, 0.0f, 100.0f), Location - FVector(0.0f, 0.0f, 1500.0f), Objects, Params))
	{
		Spawn = Hit.ImpactPoint + FVector(0.0f, 0.0f, 75.0f);
	}
	ACXEvidencePickup* Pickup = World->SpawnActorDeferred<ACXEvidencePickup>(ACXEvidencePickup::StaticClass(), FTransform(Spawn), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Pickup)
	{
		Pickup->EvidenceId = InEvidenceId;
		Pickup->FinishSpawning(FTransform(Spawn));
		UE_LOG(LogTemp, Log, TEXT("EvidencePickup: %s dropped at %s"), *InEvidenceId.ToString(), *Spawn.ToString());
	}
	return Pickup;
}

void ACXEvidencePickup::BeginPlay()
{
	Super::BeginPlay();
	CXShapeKit::SetNeon(Chip, NeonMaterial, GlowColor, 30.0f);
	CXShapeKit::SetNeon(ChipFrame, NeonMaterial, FLinearColor::White, 8.0f);
	CXShapeKit::SetNeon(Beam, NeonMaterial, GlowColor, 12.0f);
	CXShapeKit::SetNeon(Ring, NeonMaterial, GlowColor, 6.0f);
	for (UStaticMeshComponent* S : Sparkles)
	{
		CXShapeKit::SetNeon(S, NeonMaterial, FLinearColor(1.0f, 0.95f, 0.75f, 1.0f), 40.0f);
	}
	Glow->SetLightColor(GlowColor);
	SparklePhase.SetNum(Sparkles.Num());
	for (float& P : SparklePhase)
	{
		P = FMath::FRandRange(0.0f, 2.0f * PI);
	}
	if (HasAuthority())
	{
		// 오빠 위에 떨어진 경우(이미 겹쳐 있음)
		TArray<AActor*> Overlaps;
		GetOverlappingActors(Overlaps, APawn::StaticClass());
		for (AActor* A : Overlaps)
		{
			TryCollect(A);
		}
	}
}

void ACXEvidencePickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	// 칩: 회전 + 부유
	Visual->SetRelativeLocation(FVector(0.0f, 0.0f, 8.0f * FMath::Sin(Age * 2.4f)));
	Visual->SetRelativeRotation(FRotator(0.0f, Age * 120.0f, 0.0f));
	// 맥동(빛·발광)
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Age * 6.0f);
	Glow->SetIntensity(3500.0f + 6500.0f * Pulse);
	if (UMaterialInstanceDynamic* MID = CXShapeKit::GetOrCreateMID(Beam, NeonMaterial))
	{
		MID->SetScalarParameterValue(TEXT("Intensity"), 6.0f + 14.0f * Pulse);
	}
	// 반짝이: 칩 주위를 돌며(사이드뷰 기준 Y·Z 원) 따로 깜빡인다
	for (int32 i = 0; i < Sparkles.Num(); ++i)
	{
		const float A = Age * 1.6f + i * (2.0f * PI / Sparkles.Num());
		const float R = 55.0f + 10.0f * FMath::Sin(Age * 3.0f + i);
		Sparkles[i]->SetRelativeLocation(FVector(-25.0f, R * FMath::Cos(A), R * FMath::Sin(A)));
		const float Twinkle = FMath::Max(0.0f, FMath::Sin(Age * 7.0f + SparklePhase[i]));
		Sparkles[i]->SetRelativeScale3D(FVector(0.02f + 0.09f * Twinkle * Twinkle));
	}
}

void ACXEvidencePickup::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);
	if (HasAuthority())
	{
		TryCollect(OtherActor);
	}
}

void ACXEvidencePickup::TryCollect(AActor* OtherActor)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (bCollected || !Pawn || UCoopInteractComponent::GetPawnRole(Pawn) != ECoopCharacterRole::Brother || !UCXPlayerLifeComponent::IsPawnAlive(Pawn))
	{
		return;
	}
	bCollected = true;
	ACXWardenTerminal::AwardEvidence(this, EvidenceId, Pawn);
	if (PickupSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, PickupSound, GetActorLocation());
	}
	Destroy();
}

void ACXEvidencePickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACXEvidencePickup, EvidenceId);
}
