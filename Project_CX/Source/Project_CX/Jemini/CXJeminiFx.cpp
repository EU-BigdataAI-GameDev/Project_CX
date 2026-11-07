// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXJeminiFx.h"
#include "CXSfx.h"
#include "CXHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace CXJeminiFxDefs
{
	constexpr int32 MoteCount = 12;
	const FName ColorParam(TEXT("ThrusterColor"));
	/** 가산 머티리얼 밝기 배율(감정 색 0~1 그대로는 낮에 약하다). */
	constexpr float GlowBoost = 3.0f;
}

ACXJeminiFx::ACXJeminiFx()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetCanBeDamaged(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMat(TEXT("/Game/Project_CX/Characters/Drone/Materials/M_Drone_ThrusterBeam.M_Drone_ThrusterBeam"));

	auto MakeMesh = [this](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Comp->SetupAttachment(Root);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(false);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetUsingAbsoluteLocation(true);
		Comp->SetUsingAbsoluteRotation(true);
		Comp->SetUsingAbsoluteScale(true);
		Comp->SetVisibility(false);
		if (Mesh)
		{
			Comp->SetStaticMesh(Mesh);
		}
		return Comp;
	};

	Pillar = MakeMesh(TEXT("Pillar"), Cylinder.Succeeded() ? Cylinder.Object : nullptr);
	Bubble = MakeMesh(TEXT("Bubble"), Sphere.Succeeded() ? Sphere.Object : nullptr);
	for (int32 i = 0; i < CXJeminiFxDefs::MoteCount; ++i)
	{
		Motes.Add(MakeMesh(*FString::Printf(TEXT("Mote%d"), i), Sphere.Succeeded() ? Sphere.Object : nullptr));
	}
	if (GlowMat.Succeeded())
	{
		Pillar->SetMaterial(0, GlowMat.Object);
		Bubble->SetMaterial(0, GlowMat.Object);
		for (UStaticMeshComponent* Mote : Motes)
		{
			Mote->SetMaterial(0, GlowMat.Object);
		}
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	Light->SetUsingAbsoluteLocation(true);
	Light->SetCastShadows(false);
	Light->SetIntensity(0.0f);
	Light->SetAttenuationRadius(400.0f);

	PulseSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/World/SW_Gate_Off.SW_Gate_Off"));
	SparkleSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Sister/SW_Hack_Granted.SW_Hack_Granted"));
	ShieldSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/UI/SW_UI_NeonBuzz.SW_UI_NeonBuzz"));
	BeamSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Sister/SW_Hack_Start.SW_Hack_Start"));
	RebootSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Sister/SW_Hack_RoundClear.SW_Hack_RoundClear"));
}

ACXJeminiFx* ACXJeminiFx::Spawn(UWorld* World, ECXJeminiFxType InType, const FVector& InOrigin, AActor* InFollow, AActor* InSource, const FLinearColor& InColor, float InDuration)
{
	if (!World || World->GetNetMode() == NM_Client || InType == ECXJeminiFxType::None)
	{
		return nullptr;
	}
	const FTransform Transform(InFollow ? InFollow->GetActorLocation() : InOrigin);
	ACXJeminiFx* Fx = World->SpawnActorDeferred<ACXJeminiFx>(ACXJeminiFx::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Fx)
	{
		return nullptr;
	}
	Fx->Type = InType;
	Fx->Origin = InOrigin;
	Fx->Follow = InFollow;
	Fx->Source = InSource;
	Fx->Color = InColor;
	Fx->Duration = FMath::Max(0.1f, InDuration);
	Fx->FinishSpawning(Transform);
	Fx->SetLifeSpan(Fx->Duration + 0.25f);
	return Fx;
}

void ACXJeminiFx::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACXJeminiFx, Type);
	DOREPLIFETIME(ACXJeminiFx, Origin);
	DOREPLIFETIME(ACXJeminiFx, Follow);
	DOREPLIFETIME(ACXJeminiFx, Source);
	DOREPLIFETIME(ACXJeminiFx, Color);
	DOREPLIFETIME(ACXJeminiFx, Duration);
}

void ACXJeminiFx::BeginPlay()
{
	Super::BeginPlay();

	MID = Pillar->CreateDynamicMaterialInstance(0);
	if (MID)
	{
		Bubble->SetMaterial(0, MID);
		for (UStaticMeshComponent* Mote : Motes)
		{
			Mote->SetMaterial(0, MID);
		}
	}
	if (const ACharacter* Character = Cast<ACharacter>(Follow))
	{
		AnchorHalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	Light->SetLightColor(Color);

	USoundBase* Sound = nullptr;
	float Pitch = 1.0f;
	switch (Type)
	{
	case ECXJeminiFxType::Pulse:
		Pillar->SetVisibility(true);
		for (UStaticMeshComponent* Mote : Motes)
		{
			Mote->SetVisibility(true);
		}
		Sound = PulseSound;
		Pitch = 0.7f;
		break;
	case ECXJeminiFxType::Sparkle:
		for (UStaticMeshComponent* Mote : Motes)
		{
			Mote->SetVisibility(true);
		}
		Sound = SparkleSound;
		Pitch = 1.3f;
		break;
	case ECXJeminiFxType::Shield:
		Bubble->SetVisibility(true);
		Sound = ShieldSound;
		Pitch = 1.4f;
		break;
	case ECXJeminiFxType::Beam:
		Pillar->SetVisibility(true);
		Sound = BeamSound;
		break;
	case ECXJeminiFxType::Reboot:
		Pillar->SetVisibility(true);
		for (UStaticMeshComponent* Mote : Motes)
		{
			Mote->SetVisibility(true);
		}
		Sound = RebootSound;
		break;
	default:
		break;
	}
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetAnchor(), 1.0f, Pitch);
	}
	Tick(0.0f);
}

FVector ACXJeminiFx::GetAnchor() const
{
	return IsValid(Follow) ? Follow->GetActorLocation() : FVector(Origin);
}

void ACXJeminiFx::SetGlow(float Strength)
{
	if (MID)
	{
		MID->SetVectorParameterValue(CXJeminiFxDefs::ColorParam, Color * (FMath::Max(0.0f, Strength) * CXJeminiFxDefs::GlowBoost));
	}
}

void ACXJeminiFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	const float T = FMath::Clamp(Elapsed / FMath::Max(0.01f, Duration), 0.0f, 1.0f);
	SetActorLocation(GetAnchor());

	switch (Type)
	{
	case ECXJeminiFxType::Pulse:
		TickPulse(T);
		break;
	case ECXJeminiFxType::Sparkle:
		TickSparkle(T);
		break;
	case ECXJeminiFxType::Shield:
		TickShield(Elapsed);
		break;
	case ECXJeminiFxType::Beam:
		TickBeam(T, false);
		break;
	case ECXJeminiFxType::Reboot:
		TickBeam(T, true);
		break;
	default:
		break;
	}

	// 수명이 끝나면 숨긴다(서버 LifeSpan 제거 전, 클라이언트도 같은 시간)
	if (Elapsed >= Duration)
	{
		SetActorHiddenInGame(true);
		Light->SetIntensity(0.0f);
	}
}

void ACXJeminiFx::TickPulse(float T)
{
	// 빛 기둥: 위(드론 높이)에서 대상 발밑까지 0.3초 번쩍 → 가늘어지며 사라짐. 링: 발밑에서 30 → 320cm로 퍼짐.
	const FVector Anchor = GetAnchor();
	const FVector Feet = Anchor - FVector(0.0f, 0.0f, AnchorHalfHeight - 5.0f);
	const float PillarHeight = 420.0f;
	const float PillarT = FMath::Clamp(T / 0.5f, 0.0f, 1.0f);
	const float Width = FMath::Lerp(0.9f, 0.1f, PillarT);
	Pillar->SetWorldLocationAndRotation(Feet + FVector(0.0f, 0.0f, PillarHeight * 0.5f), FRotator::ZeroRotator);
	Pillar->SetWorldScale3D(FVector(Width, Width, PillarHeight / 100.0f));
	Pillar->SetVisibility(PillarT < 1.0f);

	const float Radius = FMath::Lerp(30.0f, 320.0f, FMath::Sqrt(T));
	for (int32 i = 0; i < Motes.Num(); ++i)
	{
		const float Angle = 2.0f * PI * i / Motes.Num();
		Motes[i]->SetWorldLocation(Feet + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 10.0f));
		Motes[i]->SetWorldScale3D(FVector(FMath::Lerp(0.22f, 0.05f, T)));
	}
	SetGlow(1.0f - T);
	Light->SetWorldLocation(Feet + FVector(0.0f, 0.0f, 80.0f));
	Light->SetIntensity(30000.0f * FMath::Square(1.0f - T));
}

void ACXJeminiFx::TickSparkle(float T)
{
	// 몸 주위 원통(반지름 45cm)에서 작은 빛 입자가 발밑 → 머리 위로 올라가며 작아진다(각자 다른 위상)
	const FVector Anchor = GetAnchor();
	for (int32 i = 0; i < Motes.Num(); ++i)
	{
		const float Phase = FMath::Fmod(T * 1.6f + i / static_cast<float>(Motes.Num()), 1.0f);
		const float Angle = i * 2.39996f + T * 4.0f;
		const float R = 40.0f + 10.0f * FMath::Sin(i * 1.7f);
		const float Z = FMath::Lerp(-AnchorHalfHeight, AnchorHalfHeight + 40.0f, Phase);
		Motes[i]->SetWorldLocation(Anchor + FVector(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R, Z));
		Motes[i]->SetWorldScale3D(FVector(0.09f * (1.0f - Phase) + 0.02f));
	}
	SetGlow(T < 0.8f ? 1.0f : (1.0f - T) / 0.2f);
	Light->SetWorldLocation(Anchor);
	Light->SetIntensity(4000.0f * (1.0f - T));
}

void ACXJeminiFx::TickShield(float InElapsed)
{
	// 반투명 구체(지름 = 캡슐 높이 + 여유). 끝나기 1초 전부터 깜빡임.
	const FVector Anchor = GetAnchor();
	const float Diameter = FMath::Max(160.0f, AnchorHalfHeight * 2.0f + 50.0f);
	const float Breath = 1.0f + 0.03f * FMath::Sin(InElapsed * 6.0f);
	Bubble->SetWorldLocation(Anchor);
	Bubble->SetWorldScale3D(FVector(Diameter / 100.0f * Breath));
	// 대상이 쓰러져 보호막이 해제되면(복제된 남은 시간 0) 바로 끈다
	const UCXHealthComponent* Health = UCXHealthComponent::FindOn(Follow);
	const float Remaining = Health && InElapsed > 0.3f ? FMath::Min(Duration - InElapsed, Health->GetShieldRemaining()) : Duration - InElapsed;
	const bool bBlinkOff = Remaining < 1.0f && FMath::Fmod(InElapsed * 8.0f, 2.0f) >= 1.0f;
	Bubble->SetVisibility(!bBlinkOff && Remaining > 0.0f);
	SetGlow(0.35f);
	Light->SetWorldLocation(Anchor);
	Light->SetIntensity(bBlinkOff ? 0.0f : 1500.0f);
}

void ACXJeminiFx::TickBeam(float T, bool bRing)
{
	// 드론(Source) → 대상 얇은 원통. 원통은 +Z 축 100cm라 방향에 맞춰 돌리고 길이만큼 늘린다.
	const FVector End = GetAnchor();
	const FVector Start = IsValid(Source) ? Source->GetActorLocation() : End + FVector(0.0f, 0.0f, 250.0f);
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (Length > 1.0f)
	{
		const float Flicker = 0.8f + 0.2f * FMath::Sin(Elapsed * 40.0f);
		Pillar->SetWorldLocationAndRotation((Start + End) * 0.5f, FRotationMatrix::MakeFromZ(Delta / Length).Rotator());
		Pillar->SetWorldScale3D(FVector(0.06f * Flicker, 0.06f * Flicker, Length / 100.0f));
	}
	if (bRing)
	{
		// 대상 발밑에서 도는 감정 색 링(재부팅 신호)
		const FVector Feet = End - FVector(0.0f, 0.0f, AnchorHalfHeight - 10.0f);
		for (int32 i = 0; i < Motes.Num(); ++i)
		{
			const float Angle = 2.0f * PI * i / Motes.Num() + Elapsed * 5.0f;
			const float R = 70.0f + 10.0f * FMath::Sin(Elapsed * 8.0f);
			Motes[i]->SetWorldLocation(Feet + FVector(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R, 30.0f * FMath::Sin(Elapsed * 3.0f + i)));
			Motes[i]->SetWorldScale3D(FVector(0.08f));
		}
	}
	SetGlow(T < 0.85f ? 1.0f : (1.0f - T) / 0.15f);
	Light->SetWorldLocation(End);
	Light->SetIntensity(bRing ? 3000.0f : 1500.0f);
}
