// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXJeminiDrone.h"
#include "BrotherCombatComponent.h"
#include "SisterHackComponent.h"
#include "CXHealthComponent.h"
#include "CXPlayerLifeComponent.h"
#include "CXGameInstance.h"
#include "LocalCoopGameMode.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Camera/PlayerCameraManager.h"
#include "CXJeminiFx.h"
#include "CXHackTarget.h"
#include "CXSfx.h"
#include "CombatDamageable.h"
#include "HackTerminal.h"
#include "SpiderBot.h"
#include "GameFramework/DamageType.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXJeminiDrone, Log, All);

ACXJeminiDrone::ACXJeminiDrone()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// 몸체: SM_SupportDrone(블렌더 제작, 실제 크기 67x44x54cm). 없으면 엔진 구체.
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DroneAsset(TEXT("/Game/Project_CX/Characters/Drone/SM_SupportDrone.SM_SupportDrone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (DroneAsset.Succeeded())
	{
		Body->SetStaticMesh(DroneAsset.Object);
	}
	else if (Sphere.Succeeded())
	{
		Body->SetStaticMesh(Sphere.Object);
		Body->SetRelativeScale3D(FVector(0.28f));
	}

	// 추진기: 아래로 넓어지며 사라지는 빛 기둥(엔진 원뿔) + 떨어지는 작은 불씨 6개 — 색은 감정 색
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BeamMat(TEXT("/Game/Project_CX/Characters/Drone/Materials/M_Drone_ThrusterBeam.M_Drone_ThrusterBeam"));
	ThrusterBeam = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThrusterBeam"));
	ThrusterBeam->SetupAttachment(Body);
	ThrusterBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ThrusterBeam->SetCastShadow(false);
	if (Cone.Succeeded())
	{
		ThrusterBeam->SetStaticMesh(Cone.Object);
	}
	for (int32 i = 0; i < 6; ++i)
	{
		UStaticMeshComponent* Spark = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("ThrusterSpark%d"), i));
		Spark->SetupAttachment(Body);
		Spark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Spark->SetCastShadow(false);
		if (Sphere.Succeeded())
		{
			Spark->SetStaticMesh(Sphere.Object);
		}
		ThrusterSparks.Add(Spark);
	}
	if (BeamMat.Succeeded())
	{
		ThrusterBeam->SetMaterial(0, BeamMat.Object);
		for (UStaticMeshComponent* Spark : ThrusterSparks)
		{
			Spark->SetMaterial(0, BeamMat.Object);
		}
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Root);
	// 추진기 아래 바닥을 감정 색으로 비추는 빛(스크린 앞에 두면 스크린이 하얗게 뜬다)
	Light->SetRelativeLocation(FVector(0.0f, 0.0f, -60.0f));
	Light->SetIntensity(900.0f);
	Light->SetAttenuationRadius(260.0f);
	Light->SetCastShadows(false);

	// 감정 색(기획 4장 표, 선형 RGB): 기본 하늘색 / 기쁨 초록 / 슬픔 파랑 / 화남 빨강
	EmotionColors = {
		FLinearColor(0.35f, 0.85f, 1.0f),
		FLinearColor(0.08f, 1.0f, 0.18f),
		FLinearColor(0.08f, 0.3f, 1.0f),
		FLinearColor(1.0f, 0.05f, 0.03f),
	};
	Light->SetLightColor(EmotionColors[0]);

	// 미끼 경고음: 해킹 감지음 재사용(새 에셋 없음)
	DecoyBeepSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Sister/SW_Hack_Detect.SW_Hack_Detect"));
}

// ---------------------------------------------------------------- 외형·표정

namespace CXDroneVisual
{
	const FName ScreenSlot(TEXT("M_Drone_Screen"));
	const FName GlowSlot(TEXT("M_Drone_EmotionGlow"));
	const FName ThrusterSocket(TEXT("SOCKET_Thruster"));
	const FName EmotionParam(TEXT("Emotion"));
	const FName ThrusterColorParam(TEXT("ThrusterColor"));
}

void ACXJeminiDrone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// 레벨에 놓고 Emotion을 바꾸면 에디터에서 바로 보인다(기획 6장 5번)
	SetupVisuals();
	ApplyEmotionVisuals();
}

void ACXJeminiDrone::SetupVisuals()
{
	if (!DroneMesh.IsNull())
	{
		if (UStaticMesh* Mesh = DroneMesh.LoadSynchronous())
		{
			Body->SetStaticMesh(Mesh);
			Body->SetRelativeScale3D(FVector(1.0f));
		}
	}

	// 스크린이 액터 +X를 보게(SM_SupportDrone은 스크린이 메시 +Y)
	const bool bHasDroneMesh = Body->GetMaterialIndex(CXDroneVisual::ScreenSlot) != INDEX_NONE;
	BodyBaseRotation = FRotator(0.0f, bHasDroneMesh ? MeshYawOffset : 0.0f, 0.0f);
	BodyBaseLocation = FVector::ZeroVector;
	Body->SetRelativeLocationAndRotation(BodyBaseLocation, BodyBaseRotation);

	// 추진기: 소켓(바닥 노즐) 바로 아래, 원뿔 꼭짓점이 노즐 쪽(엔진 원뿔은 꼭짓점이 +Z, 피벗은 가운데)
	FVector Nozzle(0.0f, 0.0f, -26.6f);
	if (Body->DoesSocketExist(CXDroneVisual::ThrusterSocket))
	{
		Nozzle = Body->GetSocketTransform(CXDroneVisual::ThrusterSocket, RTS_Component).GetLocation();
	}
	const FVector BeamScale(ThrusterWidth / 100.0f, ThrusterWidth / 100.0f, ThrusterLength / 100.0f);
	ThrusterBase = FTransform(FRotator::ZeroRotator, Nozzle - FVector(0.0f, 0.0f, ThrusterLength * 0.5f), BeamScale);
	ThrusterBeam->SetRelativeTransform(ThrusterBase);
	ThrusterBeam->SetVisibility(bHasDroneMesh);
	for (UStaticMeshComponent* Spark : ThrusterSparks)
	{
		Spark->SetRelativeLocation(Nozzle);
		Spark->SetRelativeScale3D(FVector(0.025f));
		Spark->SetVisibility(bHasDroneMesh);
	}

	// 다이나믹 머티리얼(스크린·발광·추진기) — 슬롯이 없으면(구체 대체) 만들지 않는다
	ScreenMID = nullptr;
	GlowMID = nullptr;
	const int32 ScreenIndex = Body->GetMaterialIndex(CXDroneVisual::ScreenSlot);
	const int32 GlowIndex = Body->GetMaterialIndex(CXDroneVisual::GlowSlot);
	if (ScreenIndex != INDEX_NONE)
	{
		ScreenMID = Body->CreateDynamicMaterialInstance(ScreenIndex);
	}
	if (GlowIndex != INDEX_NONE)
	{
		GlowMID = Body->CreateDynamicMaterialInstance(GlowIndex);
	}
	ThrusterMID = ThrusterBeam->CreateDynamicMaterialInstance(0);
	if (ThrusterMID)
	{
		for (UStaticMeshComponent* Spark : ThrusterSparks)
		{
			Spark->SetMaterial(0, ThrusterMID);
		}
	}
}

FLinearColor ACXJeminiDrone::GetEmotionColor(ECXDroneEmotion InEmotion) const
{
	const int32 Index = static_cast<int32>(InEmotion);
	return EmotionColors.IsValidIndex(Index) ? EmotionColors[Index] : FLinearColor(0.35f, 0.85f, 1.0f);
}

void ACXJeminiDrone::ApplyEmotionVisuals()
{
	const float Value = static_cast<float>(static_cast<uint8>(Emotion));
	const FLinearColor Color = GetEmotionColor(Emotion);
	if (ScreenMID)
	{
		ScreenMID->SetScalarParameterValue(CXDroneVisual::EmotionParam, Value);
	}
	if (GlowMID)
	{
		GlowMID->SetScalarParameterValue(CXDroneVisual::EmotionParam, Value);
		// 색 표는 C++ 한 곳에서 관리 → 머티리얼 색 파라미터도 덮어 같은 값으로 맞춘다
		for (int32 i = 0; i < EmotionColors.Num() && i < 4; ++i)
		{
			GlowMID->SetVectorParameterValue(*FString::Printf(TEXT("EmotionColor%d"), i), EmotionColors[i]);
		}
	}
	if (ThrusterMID)
	{
		ThrusterMID->SetVectorParameterValue(CXDroneVisual::ThrusterColorParam, Color);
	}
	Light->SetLightColor(Color);
}

void ACXJeminiDrone::SetEmotion(ECXDroneEmotion NewEmotion)
{
	if (Emotion == NewEmotion && ScreenMID)
	{
		return;
	}
	Emotion = NewEmotion;
	ApplyEmotionVisuals();
}

void ACXJeminiDrone::OnRep_Emotion()
{
	ApplyEmotionVisuals();
}

void ACXJeminiDrone::ReactEmotion(ECXDroneEmotion NewEmotion, float HoldSeconds)
{
	if (!HasAuthority() || bEmotionLocked)
	{
		return;
	}
	EmotionHoldRemaining = HoldSeconds >= 0.0f ? HoldSeconds : EmotionHoldTime;
	SetEmotion(NewEmotion);
}

void ACXJeminiDrone::DebugLockEmotion(ECXDroneEmotion NewEmotion, bool bLock)
{
	bEmotionLocked = bLock;
	EmotionHoldRemaining = 0.0f;
	SetEmotion(bLock ? NewEmotion : ComputeBaseEmotion());
}

ECXDroneEmotion ACXJeminiDrone::ComputeBaseEmotion() const
{
	// 플레이어가 쓰러져 있으면 화남, 배터리가 적으면 슬픔, 아니면 기본
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (Pawn && !UCXPlayerLifeComponent::IsPawnAlive(Pawn))
		{
			return ECXDroneEmotion::Angry;
		}
	}
	return Battery <= SadBatteryThreshold ? ECXDroneEmotion::Sad : ECXDroneEmotion::Neutral;
}

void ACXJeminiDrone::TickEmotion(float DeltaSeconds)
{
	if (!HasAuthority() || bEmotionLocked)
	{
		return;
	}
	if (EmotionHoldRemaining > 0.0f)
	{
		EmotionHoldRemaining -= DeltaSeconds;
		return;
	}
	SetEmotion(ComputeBaseEmotion());
}

void ACXJeminiDrone::TickVisuals(float DeltaSeconds)
{
	// 호버(기획 6장 6번): 위아래 ±HoverAmplitude, 주기 HoverPeriod, 살짝 기울어짐 — 복제되는 액터 위치와 분리
	const float W = 2.0f * PI / FMath::Max(0.1f, HoverPeriod);
	const FVector Bob(0.0f, 0.0f, FMath::Sin(BobTime * W) * HoverAmplitude);
	const FRotator Tilt(FMath::Sin(BobTime * W * 0.5f + 1.1f) * HoverTiltDeg * 0.6f, 0.0f, FMath::Sin(BobTime * W * 0.5f) * HoverTiltDeg);
	Body->SetRelativeLocationAndRotation(BodyBaseLocation + Bob, (Tilt.Quaternion() * BodyBaseRotation.Quaternion()).Rotator());

	// 불씨: 노즐에서 아래로 떨어지며 작아진다(각자 다른 위상)
	const FVector Nozzle = ThrusterBase.GetLocation() + FVector(0.0f, 0.0f, ThrusterLength * 0.5f);
	for (int32 i = 0; i < ThrusterSparks.Num(); ++i)
	{
		const float T = FMath::Fmod(BobTime * 1.6f + i / static_cast<float>(ThrusterSparks.Num()), 1.0f);
		const float Angle = i * 2.39996f;	// 황금각으로 흩뿌림
		const float Spread = ThrusterWidth * 0.45f * T;
		const FVector P = Nozzle + FVector(FMath::Cos(Angle) * Spread, FMath::Sin(Angle) * Spread, -ThrusterLength * 1.1f * T);
		ThrusterSparks[i]->SetRelativeLocation(P);
		ThrusterSparks[i]->SetRelativeScale3D(FVector(0.03f * (1.0f - T) + 0.004f));
	}

	// 미끼(모든 머신 — 복제된 Action 기준): 라이트 빠른 점멸(빨강/흰색) + 경고음 반복
	if (Action == ECXJeminiAction::DecoyActive)
	{
		const bool bOn = FMath::Fmod(BobTime * 12.0f, 2.0f) < 1.0f;
		Light->SetIntensity(bOn ? 9000.0f : 300.0f);
		Light->SetAttenuationRadius(700.0f);
		Light->SetLightColor(bOn ? FLinearColor(1.0f, 0.1f, 0.05f) : FLinearColor::White);
		DecoyBeepTimer -= DeltaSeconds;
		if (DecoyBeepTimer <= 0.0f)
		{
			DecoyBeepTimer = 0.5f;
			if (DecoyBeepSound)
			{
				UGameplayStatics::PlaySoundAtLocation(this, DecoyBeepSound, GetActorLocation(), 0.8f, 1.5f);
			}
		}
		bDecoyVisual = true;
	}
	else if (bDecoyVisual)
	{
		bDecoyVisual = false;
		DecoyBeepTimer = 0.0f;
		Light->SetIntensity(900.0f);
		Light->SetAttenuationRadius(260.0f);
		ApplyEmotionVisuals();
	}
}

ACXJeminiDrone* ACXJeminiDrone::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACXJeminiDrone> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			return *It;
		}
	}
	return nullptr;
}

ACXJeminiDrone* ACXJeminiDrone::EnsureSpawned(UWorld* World)
{
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	if (ACXJeminiDrone* Existing = Get(World))
	{
		return Existing;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;	// 레벨에 저장하지 않는다
	return World->SpawnActor<ACXJeminiDrone>(ACXJeminiDrone::StaticClass(), FTransform::Identity, Params);
}

void ACXJeminiDrone::BeginPlay()
{
	Super::BeginPlay();

	// 스크린·발광 슬롯 다이나믹 머티리얼(기획 6장 3번) + 지금 표정 반영
	SetupVisuals();
	ApplyEmotionVisuals();

	if (HasAuthority())
	{
		// 맵 이동·이어하기로 넘어온 배터리
		if (const UCXGameInstance* GI = Cast<UCXGameInstance>(GetGameInstance()))
		{
			Battery = FMath::Clamp(GI->JeminiBattery, 0, 100);
		}
	}
}

void ACXJeminiDrone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACXJeminiDrone, Battery);
	DOREPLIFETIME(ACXJeminiDrone, bRequestPending);
	DOREPLIFETIME(ACXJeminiDrone, Dialogue);
	DOREPLIFETIME(ACXJeminiDrone, Heard);
	DOREPLIFETIME(ACXJeminiDrone, DialogueStatus);
	DOREPLIFETIME(ACXJeminiDrone, DialogueSeq);
	DOREPLIFETIME(ACXJeminiDrone, bServerMock);
	DOREPLIFETIME(ACXJeminiDrone, Action);
	DOREPLIFETIME(ACXJeminiDrone, ActionTarget);
	DOREPLIFETIME(ACXJeminiDrone, Emotion);
}

// ---------------------------------------------------------------- 플레이어 찾기

APawn* ACXJeminiDrone::FindBrother() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (Pawn && Pawn->FindComponentByClass<UBrotherCombatComponent>())
		{
			return Pawn;
		}
	}
	return nullptr;
}

APawn* ACXJeminiDrone::FindSister() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (Pawn && Pawn->FindComponentByClass<USisterHackComponent>())
		{
			return Pawn;
		}
	}
	return nullptr;
}

FVector ACXJeminiDrone::GetHoverTargetFor(const APawn* Pawn) const
{
	return Pawn->GetActorLocation() + HoverOffset;
}

// ---------------------------------------------------------------- 이동

APawn* ACXJeminiDrone::GetFollowTarget() const
{
	APawn* Target = FollowPawn.Get();
	if (!Target || !UCXPlayerLifeComponent::IsPawnAlive(Target))
	{
		Target = FindBrother();
		if (!Target || !UCXPlayerLifeComponent::IsPawnAlive(Target))
		{
			Target = FindSister();
		}
	}
	return Target;
}

void ACXJeminiDrone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BobTime += DeltaSeconds;
	TickVisuals(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}
	TickEmotion(DeltaSeconds);

	// 스크린이 카메라를 보게(사이드뷰: 두 칸 카메라 모두 같은 쪽) — 수평 방향만, 부드럽게
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - GetActorLocation();
			ToCamera.Z = 0.0f;
			if (!ToCamera.IsNearlyZero())
			{
				const FRotator Want(0.0f, ToCamera.Rotation().Yaw, 0.0f);
				SetActorRotation(FMath::RInterpTo(GetActorRotation(), Want, DeltaSeconds, 4.0f));
			}
		}
	}

	FVector Goal = FVector::ZeroVector;
	bool bHasGoal = false;
	bool bDash = false;
	TickAction(DeltaSeconds, Goal, bHasGoal, bDash);

	if (!bHasGoal)
	{
		const APawn* Target = GetFollowTarget();
		if (!Target)
		{
			return;
		}
		Goal = GetHoverTargetFor(Target);
	}

	if (!bSnapped)
	{
		// 첫 위치는 순간 이동(맵 원점에서 날아오지 않게)
		SetActorLocation(Goal);
		bSnapped = true;
		return;
	}
	const FVector NewLocation = bDash
		? FMath::VInterpConstantTo(GetActorLocation(), Goal, DeltaSeconds, DashSpeed)
		: FMath::VInterpTo(GetActorLocation(), Goal, DeltaSeconds, FollowSpeed);
	// 너무 멀어지면(부활·순간 이동·먼 단말기) 붙는다
	SetActorLocation(FVector::DistSquared(NewLocation, Goal) > FMath::Square(3000.0f) ? Goal : NewLocation);
}

// ---------------------------------------------------------------- 작업(서버)

namespace CXJeminiLines
{
	const TCHAR* TargetLost = TEXT("앗, 놓쳤어요! 배터리는 아껴 둘게요.");
	const TCHAR* TerminalGone = TEXT("앗, 단말기가 벌써 열렸어요!");
	const TCHAR* AlreadyRevived = TEXT("벌써 일어나셨네요! 배터리는 그대로예요.");
}

namespace CXJeminiFxColor
{
	const FLinearColor Heal(0.1f, 1.0f, 0.25f);
	const FLinearColor Boost(1.0f, 0.45f, 0.05f);
	const FLinearColor Hack(0.0f, 0.85f, 1.0f);
	const FLinearColor Pulse(1.0f, 0.15f, 0.08f);
}

void ACXJeminiDrone::SetAction(ECXJeminiAction NewAction)
{
	Action = NewAction;
	ActionElapsed = 0.0f;
	ActionPhase = 0;
}

void ACXJeminiDrone::ChargePending()
{
	if (PendingBatteryAfter >= 0)
	{
		UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: %s executed — battery %d -> %d"), *ActionCommand, Battery, PendingBatteryAfter);
		LastRequestDebug += FString::Printf(TEXT(" [charged %d->%d]"), Battery, PendingBatteryAfter);
		SetBattery(PendingBatteryAfter);
		PendingBatteryAfter = -1;
	}
}

void ACXJeminiDrone::EndAction(bool bReturn)
{
	PendingBatteryAfter = -1;
	ActionTarget = nullptr;
	ActionPawn.Reset();
	bActionEffectDone = false;
	SetAction(bReturn ? ECXJeminiAction::Returning : ECXJeminiAction::Idle);
}

void ACXJeminiDrone::FailTargetLost(const FString& Line)
{
	UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: %s TARGET_LOST (no battery used)"), *ActionCommand);
	LastLocalReason = TEXT("TARGET_LOST");
	LastRequestDebug += TEXT(" [TARGET_LOST]");
	SetDialogue(Line, FString(), TEXT("rejected"));
	ReactEmotion(ECXDroneEmotion::Neutral);
	// 대상 쪽으로 날아가 있었으면 복귀까지 작업 중
	const bool bWasAway = Action != ECXJeminiAction::Idle && Action != ECXJeminiAction::HackAssist && Action != ECXJeminiAction::Heal;
	EndAction(bWasAway);
}

float ACXJeminiDrone::GetDecoyRemaining() const
{
	return Action == ECXJeminiAction::DecoyActive ? FMath::Max(0.0f, DecoyDuration - ActionElapsed) : 0.0f;
}

void ACXJeminiDrone::TickAction(float DeltaSeconds, FVector& OutGoal, bool& bHasGoal, bool& bDash)
{
	if (Action == ECXJeminiAction::Idle)
	{
		return;
	}
	ActionElapsed += DeltaSeconds;
	const FVector Here = GetActorLocation();
	auto Arrived = [&Here](const FVector& Goal, float Tolerance) { return FVector::DistSquared(Here, Goal) < FMath::Square(Tolerance); };

	switch (Action)
	{
	case ECXJeminiAction::FlyToReload:
	{
		APawn* Brother = Cast<APawn>(ActionTarget);
		if (!Brother)
		{
			FailTargetLost(CXJeminiLines::TargetLost);
			return;
		}
		OutGoal = GetHoverTargetFor(Brother);
		bHasGoal = bDash = true;
		if (Arrived(OutGoal, 60.0f) || ActionElapsed >= MaxFlyTime)
		{
			FinishReloadFlight();
		}
		return;
	}

	case ECXJeminiAction::HackAssist:
	{
		// 단말기 A: 여동생 곁. 3초 대행이 끝나 단말기가 열린 순간 차감, 여동생이 먼저 풀거나 취소하면 차감 없음.
		APawn* Sister = ActionPawn.Get();
		const AHackTerminal* Terminal = Cast<AHackTerminal>(ActionTarget);
		const USisterHackComponent* Hack = Sister ? Sister->FindComponentByClass<USisterHackComponent>() : nullptr;
		if (Sister)
		{
			OutGoal = GetHoverTargetFor(Sister);
			bHasGoal = true;
		}
		const float DoneAt = AssistTimeTerminal - 0.2f;
		if (Terminal && Terminal->GetState() == EHackTerminalState::Hacked)
		{
			if (ActionElapsed >= DoneAt)
			{
				ChargePending();
				ReactEmotion(ECXDroneEmotion::Happy, 2.0f);
			}
			else
			{
				UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: sister solved the terminal herself — no battery used"));
			}
			EndAction();
		}
		else if (!Terminal || !Hack || (!Hack->IsJeminiAssisting() && ActionElapsed < DoneAt) || ActionElapsed > AssistTimeTerminal + 2.0f)
		{
			// 취소(여동생이 닫음·사망) 또는 원격 완료 대기 초과 → 차감 없음
			UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: terminal assist ended without hack — no battery used"));
			EndAction();
		}
		return;
	}

	case ECXJeminiAction::TerminalHack:
	case ECXJeminiAction::TerminalHacking:
	{
		AHackTerminal* Terminal = Cast<AHackTerminal>(ActionTarget);
		if (!Terminal)
		{
			FailTargetLost(CXJeminiLines::TerminalGone);
			return;
		}
		OutGoal = Terminal->GetActorLocation() + FVector(0.0f, 0.0f, 200.0f);
		bHasGoal = bDash = true;
		if (Action == ECXJeminiAction::TerminalHack)
		{
			if (!Terminal->CanBeHacked())
			{
				// 여동생이 먼저 열었거나 이미 풀림
				FailTargetLost(CXJeminiLines::TerminalGone);
				return;
			}
			if (Arrived(OutGoal, 80.0f) || ActionElapsed >= MaxTravelTime)
			{
				// 도착 → 3초 해킹(상태등 노랑 깜빡임 = 기존 InProgress 연출, 드론 빔)
				Terminal->SetInProgress(true);
				ACXJeminiFx::Spawn(GetWorld(), ECXJeminiFxType::Beam, Terminal->GetActorLocation(), Terminal, this, CXJeminiFxColor::Hack, AssistTimeTerminal);
				SetAction(ECXJeminiAction::TerminalHacking);
			}
			return;
		}
		if (Terminal->GetState() != EHackTerminalState::InProgress)
		{
			// 그 사이 다른 경로(디버그 등)로 열림
			FailTargetLost(CXJeminiLines::TerminalGone);
			return;
		}
		if (ActionElapsed >= AssistTimeTerminal)
		{
			Terminal->MarkHacked();
			UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: hacked terminal %s"), *Terminal->TerminalName);
			ChargePending();
			ReactEmotion(ECXDroneEmotion::Happy, 2.0f);
			EndAction(true);
		}
		return;
	}

	case ECXJeminiAction::EnemyHack:
	{
		// 효과는 응답 즉시(B) / 여동생 퍼즐 대행(A)은 그로기가 확인된 순간 차감. 드론은 0.5초 대시 → 0.5초 빔.
		AActor* Enemy = ActionTarget;
		if (IsValid(Enemy))
		{
			OutGoal = Enemy->GetActorLocation() + FVector(0.0f, 0.0f, 180.0f);
			bHasGoal = true;
			bDash = ActionElapsed < 0.5f;
		}
		if (!bActionEffectDone && IsEnemyStunned(Enemy))
		{
			bActionEffectDone = true;
			ChargePending();
		}
		else if (!bActionEffectDone && ActionElapsed >= 0.6f && IsEnemyAlive(Enemy))
		{
			// A 보험: 여동생 경로의 서버 재검증(거리 등)에 걸려 그로기가 안 걸렸으면 드론이 직접 건다
			if (ICXHackTarget* HackTarget = Cast<ICXHackTarget>(Enemy); HackTarget && HackTarget->ApplyHackSuccess(FindSister()))
			{
				bActionEffectDone = true;
				ChargePending();
			}
		}
		if (ActionPhase == 0 && ActionElapsed >= 0.5f && IsValid(Enemy))
		{
			ActionPhase = 1;
			ACXJeminiFx::Spawn(GetWorld(), ECXJeminiFxType::Beam, Enemy->GetActorLocation(), Enemy, this, CXJeminiFxColor::Hack, 0.5f);
		}
		if (ActionElapsed >= 1.0f)
		{
			if (!bActionEffectDone)
			{
				FailTargetLost(CXJeminiLines::TargetLost);
			}
			else
			{
				EndAction(true);
			}
		}
		return;
	}

	case ECXJeminiAction::PulseAttack:
	{
		AActor* Enemy = ActionTarget;
		if (ActionPhase == 0)
		{
			if (!IsEnemyAlive(Enemy))
			{
				// 비행 중 대상이 이미 죽음 → 펄스 생략, 차감 없이 복귀
				FailTargetLost(CXJeminiLines::TargetLost);
				return;
			}
			OutGoal = Enemy->GetActorLocation() + FVector(0.0f, 0.0f, PulseHoverHeight);
			bHasGoal = bDash = true;
			if (Arrived(OutGoal, 70.0f) || ActionElapsed >= MaxTravelTime)
			{
				// 펄스: 링 + 빛 기둥 + 소리 → 남은 체력 이상 피해(한 방)
				ACXJeminiFx::Spawn(GetWorld(), ECXJeminiFxType::Pulse, Enemy->GetActorLocation(), Enemy, this, CXJeminiFxColor::Pulse, 0.6f);
				const UCXHealthComponent* Health = UCXHealthComponent::FindOn(Enemy);
				const float Damage = (Health ? Health->GetHealth() : 100.0f) + 1000.0f;
				if (ICombatDamageable* Damageable = Cast<ICombatDamageable>(Enemy))
				{
					Damageable->ApplyDamage(Damage, this, Enemy->GetActorLocation(), FVector::ZeroVector);
				}
				else
				{
					UGameplayStatics::ApplyDamage(Enemy, Damage, nullptr, this, UDamageType::StaticClass());
				}
				if (!IsEnemyAlive(Enemy))
				{
					UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: pulse killed %s"), *GetNameSafe(Enemy));
					ChargePending();
					ReactEmotion(ECXDroneEmotion::Happy, 2.5f);
				}
				else
				{
					FailTargetLost(CXJeminiLines::TargetLost);
					return;
				}
				ActionPhase = 1;
				ActionElapsed = 0.0f;
				ActionLocation = OutGoal;
			}
			return;
		}
		// 처치 확인 후 잠깐 머물렀다가 플레이어에게 복귀
		OutGoal = ActionLocation;
		bHasGoal = true;
		if (ActionElapsed >= 0.5f)
		{
			EndAction(true);
		}
		return;
	}

	case ECXJeminiAction::BoostFly:
	{
		APawn* Brother = Cast<APawn>(ActionTarget);
		if (!Brother || !UCXPlayerLifeComponent::IsPawnAlive(Brother))
		{
			FailTargetLost(CXJeminiLines::TargetLost);
			return;
		}
		OutGoal = GetHoverTargetFor(Brother);
		bHasGoal = bDash = true;
		if (Arrived(OutGoal, 60.0f) || ActionElapsed >= MaxQuickFlyTime)
		{
			if (UBrotherCombatComponent* Combat = Brother->FindComponentByClass<UBrotherCombatComponent>())
			{
				// 강화 중이면 남은 시간 + DamageBoostDuration(연장)
				Combat->ApplyDamageBoost(DamageBoostDuration, DamageBoostMultiplier);
				ACXJeminiFx::Spawn(GetWorld(), ECXJeminiFxType::Sparkle, Brother->GetActorLocation(), Brother, this, CXJeminiFxColor::Boost, 1.0f);
				ChargePending();
				EndAction();
			}
			else
			{
				FailTargetLost(CXJeminiLines::TargetLost);
			}
		}
		return;
	}

	case ECXJeminiAction::Heal:
	{
		// 효과는 응답 즉시 적용됨. 드론은 살아 있는 두 사람 중간 위에서 1초.
		const TArray<APawn*> Living = GetLivingPlayers();
		if (Living.Num() > 0)
		{
			FVector Mid = FVector::ZeroVector;
			for (const APawn* Pawn : Living)
			{
				Mid += Pawn->GetActorLocation();
			}
			OutGoal = Mid / Living.Num() + FVector(0.0f, 0.0f, HoverOffset.Z + 40.0f);
			bHasGoal = true;
		}
		if (ActionElapsed >= 1.0f)
		{
			EndAction();
		}
		return;
	}

	case ECXJeminiAction::Decoy:
	case ECXJeminiAction::DecoyActive:
	{
		OutGoal = ActionLocation;
		bHasGoal = true;
		if (Action == ECXJeminiAction::Decoy)
		{
			bDash = true;
			if (Arrived(OutGoal, 80.0f) || ActionElapsed >= MaxTravelTime)
			{
				// 도착 = 미끼 시작: 15m 안 움직이는 거미 전부가 5초 동안 드론 아래로
				int32 Lured = 0;
				for (AActor* Enemy : QueryEnemies().ActiveEnemies)
				{
					if (ASpiderBot* Spider = Cast<ASpiderBot>(Enemy); Spider && Spider->ApplyDecoy(this, DecoyDuration))
					{
						++Lured;
					}
				}
				if (Lured == 0)
				{
					FailTargetLost(CXJeminiLines::TargetLost);
					return;
				}
				UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: decoy lured %d enemies"), Lured);
				ChargePending();
				SetAction(ECXJeminiAction::DecoyActive);
			}
			return;
		}
		if (ActionElapsed >= DecoyDuration)
		{
			EndAction(true);
		}
		return;
	}

	case ECXJeminiAction::Reboot:
	case ECXJeminiAction::RebootBeam:
	{
		APawn* Downed = Cast<APawn>(ActionTarget);
		APawn* Partner = ActionPawn.Get();
		const UCXPlayerLifeComponent* DownedLife = Downed ? Downed->FindComponentByClass<UCXPlayerLifeComponent>() : nullptr;
		if (!DownedLife || DownedLife->GetLifeState() != ECXLifeState::AwaitingRespawn)
		{
			if (DownedLife && DownedLife->IsAlive())
			{
				// 연출 중 자연 부활 → 차감 없음
				UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: reboot target respawned naturally — no battery used"));
				LastLocalReason = TEXT("TARGET_LOST");
				SetDialogue(CXJeminiLines::AlreadyRevived, FString(), TEXT("chat"));
				EndAction(true);
			}
			else
			{
				FailTargetLost(CXJeminiLines::TargetLost);
			}
			return;
		}
		if (!Partner || !UCXPlayerLifeComponent::IsPawnAlive(Partner))
		{
			FailTargetLost(CXJeminiLines::TargetLost);
			return;
		}
		OutGoal = GetHoverTargetFor(Partner);
		bHasGoal = true;
		if (Action == ECXJeminiAction::Reboot)
		{
			bDash = true;
			if (Arrived(OutGoal, 70.0f) || ActionElapsed >= MaxTravelTime * 0.5f)
			{
				// 파트너(부활 위치) 곁에서 재부팅 빔 + 링
				ACXJeminiFx::Spawn(GetWorld(), ECXJeminiFxType::Reboot, Partner->GetActorLocation(), Partner, this, GetEmotionColor(ECXDroneEmotion::Happy), RebootBeamTime);
				SetAction(ECXJeminiAction::RebootBeam);
			}
			return;
		}
		if (ActionElapsed >= RebootBeamTime)
		{
			ALocalCoopGameMode* GameMode = GetWorld()->GetAuthGameMode<ALocalCoopGameMode>();
			if (GameMode && GameMode->JeminiRebootNow(Downed->GetController()))
			{
				ChargePending();
				ReactEmotion(ECXDroneEmotion::Happy, 2.5f);
				EndAction();
			}
			else
			{
				FailTargetLost(CXJeminiLines::TargetLost);
			}
		}
		return;
	}

	case ECXJeminiAction::ShieldFly:
	{
		APawn* Target = Cast<APawn>(ActionTarget);
		if (!Target || !UCXPlayerLifeComponent::IsPawnAlive(Target))
		{
			FailTargetLost(CXJeminiLines::TargetLost);
			return;
		}
		OutGoal = GetHoverTargetFor(Target);
		bHasGoal = bDash = true;
		if (Arrived(OutGoal, 60.0f) || ActionElapsed >= MaxQuickFlyTime)
		{
			UCXHealthComponent* Health = UCXHealthComponent::FindOn(Target);
			if (Health && Health->GrantShield(ShieldDuration))
			{
				// 이미 보호막 중이면 5초로 다시 채움 → 기존 구체는 지우고 새로
				for (TActorIterator<ACXJeminiFx> It(GetWorld()); It; ++It)
				{
					if (It->GetFxType() == ECXJeminiFxType::Shield && It->GetFollow() == Target)
					{
						It->Destroy();
					}
				}
				ACXJeminiFx::Spawn(GetWorld(), ECXJeminiFxType::Shield, Target->GetActorLocation(), Target, this, GetEmotionColor(ECXDroneEmotion::Neutral), ShieldDuration);
				ChargePending();
				EndAction();
			}
			else
			{
				FailTargetLost(CXJeminiLines::TargetLost);
			}
		}
		return;
	}

	case ECXJeminiAction::Returning:
	{
		if (const APawn* Target = GetFollowTarget())
		{
			OutGoal = GetHoverTargetFor(Target);
			bHasGoal = bDash = true;
			if (Arrived(OutGoal, 100.0f))
			{
				EndAction();
			}
		}
		if (ActionElapsed >= MaxTravelTime)
		{
			EndAction();
		}
		return;
	}

	default:
		EndAction();
		return;
	}
}

// ---------------------------------------------------------------- 적·단말기 조회

bool ACXJeminiDrone::IsEnemyAlive(const AActor* Enemy)
{
	if (!IsValid(Enemy) || UCXHealthComponent::IsActorDead(Enemy))
	{
		return false;
	}
	const ASpiderBot* Spider = Cast<ASpiderBot>(Enemy);
	return !Spider || !Spider->IsDead();
}

bool ACXJeminiDrone::IsEnemyStunned(const AActor* Enemy)
{
	const ICXHackTarget* Target = Cast<ICXHackTarget>(Enemy);
	return Target && IsEnemyAlive(Enemy) && Target->GetHackStunRemaining() > 0.0f;
}

TArray<APawn*> ACXJeminiDrone::GetLivingPlayers() const
{
	TArray<APawn*> Out;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (Pawn && UCXPlayerLifeComponent::IsPawnAlive(Pawn))
		{
			Out.Add(Pawn);
		}
	}
	return Out;
}

FCXJeminiEnemyQuery ACXJeminiDrone::QueryEnemies() const
{
	// 후보 = ICXHackTarget 구현 액터(거미). 플레이어·더미·상자처럼 ICombatDamageable만 구현한 액터는 제외(5-2절).
	// 거리 = 살아 있는 두 사람 중 더 가까운 쪽. 범위 안에서 가장 가까운 적 = 가장 위협적인 적.
	FCXJeminiEnemyQuery Query;
	const TArray<APawn*> Living = GetLivingPlayers();
	if (Living.Num() == 0)
	{
		return Query;
	}
	const float RangeSq = FMath::Square(EnemySearchRange);
	float BestAliveSq = TNumericLimits<float>::Max();
	float BestHackSq = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		const ICXHackTarget* HackTarget = Cast<ICXHackTarget>(Actor);
		if (!HackTarget || !IsEnemyAlive(Actor))
		{
			continue;
		}
		float DistSq = TNumericLimits<float>::Max();
		for (const APawn* Pawn : Living)
		{
			DistSq = FMath::Min(DistSq, static_cast<float>(FVector::DistSquared(Pawn->GetActorLocation(), Actor->GetActorLocation())));
		}
		if (DistSq > RangeSq)
		{
			continue;
		}
		++Query.AliveCount;
		if (DistSq < BestAliveSq)
		{
			BestAliveSq = DistSq;
			Query.NearestAlive = Actor;
		}
		if (HackTarget->IsHackTargetAvailable())
		{
			++Query.HackableCount;
			if (DistSq < BestHackSq)
			{
				BestHackSq = DistSq;
				Query.NearestHackable = Actor;
			}
		}
		if (HackTarget->GetHackStunRemaining() <= 0.0f)
		{
			++Query.ActiveCount;
			Query.ActiveEnemies.Add(Actor);
		}
	}
	if (Query.NearestAlive)
	{
		Query.NearestDistCm = FMath::Sqrt(BestAliveSq);
	}
	return Query;
}

namespace CXJeminiTerminalOrder
{
	/** TerminalName 끝 숫자(TERMINAL_02 → 2). 없으면 -1. */
	int32 ParseNumber(const FString& Name)
	{
		int32 End = Name.Len();
		int32 Start = End;
		while (Start > 0 && FChar::IsDigit(Name[Start - 1]))
		{
			--Start;
		}
		return Start < End ? FCString::Atoi(*Name.Mid(Start)) : -1;
	}
}

AHackTerminal* ACXJeminiDrone::FindNextLockedTerminal() const
{
	TArray<AHackTerminal*> Locked;
	bool bAllNumbered = true;
	for (TActorIterator<AHackTerminal> It(GetWorld()); It; ++It)
	{
		if (It->CanBeHacked())
		{
			Locked.Add(*It);
			bAllNumbered &= CXJeminiTerminalOrder::ParseNumber(It->TerminalName) >= 0;
		}
	}
	if (Locked.Num() == 0)
	{
		return nullptr;
	}
	// 진행 순서: 모든 후보에 번호가 있으면 번호 순(같으면 +Y 좌표), 아니면 진행 방향 +Y 좌표가 작은 순
	Locked.Sort([bAllNumbered](const AHackTerminal& A, const AHackTerminal& B)
	{
		if (bAllNumbered)
		{
			const int32 NA = CXJeminiTerminalOrder::ParseNumber(A.TerminalName);
			const int32 NB = CXJeminiTerminalOrder::ParseNumber(B.TerminalName);
			if (NA != NB)
			{
				return NA < NB;
			}
		}
		return A.GetActorLocation().Y < B.GetActorLocation().Y;
	});
	return Locked[0];
}

int32 ACXJeminiDrone::CountLockedTerminals() const
{
	int32 Count = 0;
	for (TActorIterator<AHackTerminal> It(GetWorld()); It; ++It)
	{
		if (It->GetState() != EHackTerminalState::Hacked)
		{
			++Count;
		}
	}
	return Count;
}

// ---------------------------------------------------------------- 요청

bool ACXJeminiDrone::CanAcceptRequest() const
{
	if (IsBusy())
	{
		return false;
	}
	if (const ALocalCoopGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ALocalCoopGameMode>() : nullptr)
	{
		if (GameMode->IsGameOver() || GameMode->IsGameCleared() || GameMode->IsTravelling())
		{
			return false;
		}
	}
	return true;
}

FCXJeminiState ACXJeminiDrone::GatherState(const APawn* Speaker) const
{
	FCXJeminiState State;
	State.Battery = Battery;
	State.bDroneBusy = Action != ECXJeminiAction::Idle;
	State.Speaker = Speaker && Speaker->FindComponentByClass<USisterHackComponent>() ? TEXT("sister") : TEXT("brother");

	if (const APawn* Brother = FindBrother())
	{
		if (const UBrotherCombatComponent* Combat = Brother->FindComponentByClass<UBrotherCombatComponent>())
		{
			State.BrotherAmmo = Combat->CurrentAmmo;
			State.BrotherAmmoMax = Combat->GetMaxAmmo();
			State.bBrotherReloading = Combat->bIsReloading;
			State.DamageBoostRemaining = Combat->GetDamageBoostRemaining();
		}
		if (const UCXHealthComponent* Health = UCXHealthComponent::FindOn(Brother))
		{
			State.BrotherHp = FMath::RoundToInt(Health->GetHealth());
			State.BrotherHpMax = FMath::RoundToInt(Health->GetMaxHealth());
			State.BrotherShieldRemaining = Health->GetShieldRemaining();
		}
		State.bBrotherAlive = UCXPlayerLifeComponent::IsPawnAlive(Brother);
		if (const UCXPlayerLifeComponent* Life = Brother->FindComponentByClass<UCXPlayerLifeComponent>())
		{
			State.BrotherRespawnRemaining = Life->GetRespawnRemaining();
		}
	}
	else
	{
		// 폰이 파괴된 채 사망(안전망 경로)
		State.bBrotherAlive = false;
		State.BrotherHp = 0;
	}

	if (const APawn* Sister = FindSister())
	{
		if (const USisterHackComponent* Hack = Sister->FindComponentByClass<USisterHackComponent>())
		{
			// 퍼즐 중이 아니면 null(비밀번호 퍼즐은 진행도가 항상 0이라 진행도로 구분하지 않는다)
			State.bHasPuzzle = Hack->GetJeminiPuzzleState(State.PuzzleType, State.PuzzleProgress);
		}
		if (const UCXHealthComponent* Health = UCXHealthComponent::FindOn(Sister))
		{
			State.SisterHp = FMath::RoundToInt(Health->GetHealth());
			State.SisterHpMax = FMath::RoundToInt(Health->GetMaxHealth());
			State.SisterShieldRemaining = Health->GetShieldRemaining();
		}
		State.bSisterAlive = UCXPlayerLifeComponent::IsPawnAlive(Sister);
		if (const UCXPlayerLifeComponent* Life = Sister->FindComponentByClass<UCXPlayerLifeComponent>())
		{
			State.SisterRespawnRemaining = Life->GetRespawnRemaining();
		}
	}
	else
	{
		State.bSisterAlive = false;
		State.SisterHp = 0;
	}

	const FCXJeminiEnemyQuery Enemies = QueryEnemies();
	State.EnemyAliveCount = Enemies.AliveCount;
	State.EnemyHackableCount = Enemies.HackableCount;
	State.EnemyActiveCount = Enemies.ActiveCount;
	State.EnemyNearestDist = Enemies.NearestDistCm >= 0.0f ? Enemies.NearestDistCm / 100.0f : -1.0f;
	State.TerminalLockedCount = CountLockedTerminals();
	return State;
}

FString ACXJeminiDrone::GetConfiguredServerUrl()
{
	const ACXJeminiDrone* Defaults = GetDefault<ACXJeminiDrone>();
	FString Url = Defaults->ServerUrl.TrimStartAndEnd().TrimQuotes();
	// ini에서 따옴표 없이 쓰면 "//" 뒤가 잘려 "http:"만 남는다 → 기본 로컬 주소로
	if (!Url.Contains(TEXT("://")) || Url.EndsWith(TEXT("://")))
	{
		UE_LOG(LogCXJeminiDrone, Warning, TEXT("Jemini: invalid ServerUrl '%s' (quote it in DefaultGame.ini) — using http://127.0.0.1:8000"), *Defaults->ServerUrl);
		Url = TEXT("http://127.0.0.1:8000");
	}
	Url.RemoveFromEnd(TEXT("/"));
	return Url;
}

CXJemini::FEndpoint ACXJeminiDrone::GetEndpoint() const
{
	CXJemini::FEndpoint Endpoint;
	Endpoint.BaseUrl = GetConfiguredServerUrl();
	Endpoint.ApiToken = ApiToken;
	Endpoint.TimeoutSec = RequestTimeout;
	return Endpoint;
}

bool ACXJeminiDrone::BeginRequest(APawn* From, bool bVoice)
{
	// 쓰러진(부활 대기) 플레이어도 말할 수 있다(2026-11-07 결정) — 게임 오버·클리어·이동 중만 CanAcceptRequest가 막는다
	if (!HasAuthority() || !CanAcceptRequest())
	{
		return false;
	}
	bRequestPending = true;
	RequestSpeaker = From;
	if (From && UCXPlayerLifeComponent::IsPawnAlive(From))
	{
		FollowPawn = From;
	}
	LastLocalReason.Reset();
	LastRequestDebug = FString::Printf(TEXT("%s from %s"), bVoice ? TEXT("voice") : TEXT("text"), *GetNameSafe(From));
	return true;
}

bool ACXJeminiDrone::SubmitText(APawn* From, const FString& Text)
{
	const FString Trimmed = Text.TrimStartAndEnd().Left(300);
	if (Trimmed.IsEmpty() || !BeginRequest(From, false))
	{
		return false;
	}
	const FCXJeminiState State = GatherState(From);
	UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: text \"%s\" state=%s"), *Trimmed, *CXJemini::StateToJson(State));
	LastRequestDebug += FString::Printf(TEXT(" \"%s\""), *Trimmed);

	TWeakObjectPtr<ACXJeminiDrone> WeakThis(this);
	CXJemini::SendText(GetEndpoint(), Trimmed, State, FOnCXJeminiResponse::CreateLambda([WeakThis](const FCXJeminiResponse& Response)
	{
		if (ACXJeminiDrone* Drone = WeakThis.Get())
		{
			Drone->HandleResponse(Response);
		}
	}));
	return true;
}

bool ACXJeminiDrone::SubmitVoice(APawn* From, const TArray<uint8>& Wav)
{
	if (Wav.Num() < 64 || !BeginRequest(From, true))
	{
		return false;
	}
	const FCXJeminiState State = GatherState(From);
	UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: voice %d bytes state=%s"), Wav.Num(), *CXJemini::StateToJson(State));

	TWeakObjectPtr<ACXJeminiDrone> WeakThis(this);
	CXJemini::SendVoice(GetEndpoint(), Wav, State, FOnCXJeminiResponse::CreateLambda([WeakThis](const FCXJeminiResponse& Response)
	{
		if (ACXJeminiDrone* Drone = WeakThis.Get())
		{
			Drone->HandleResponse(Response);
		}
	}));
	return true;
}

int32 ACXJeminiDrone::GetCommandCost(const FString& CommandName)
{
	// Jemini/app/config.py COST_* 와 같은 값(08_밸런스.md)
	static const TMap<FString, int32> Costs = {
		{ TEXT("ReloadSupport"), 5 }, { TEXT("HackTerminal"), 20 }, { TEXT("HackEnemy"), 20 },
		{ TEXT("PulseAttack"), 50 }, { TEXT("DamageBoost"), 10 }, { TEXT("HealSupport"), 30 },
		{ TEXT("Decoy"), 15 }, { TEXT("QuickReboot"), 30 }, { TEXT("Shield"), 25 },
	};
	for (const TPair<FString, int32>& Pair : Costs)
	{
		if (Pair.Key.Equals(CommandName, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}
	return -1;
}

bool ACXJeminiDrone::DebugExecuteCommand(APawn* Speaker, const FString& CommandName, const FString& Target)
{
	const int32 Cost = GetCommandCost(CommandName);
	if (!HasAuthority() || Cost < 0 || !BeginRequest(Speaker, false))
	{
		return false;
	}
	// 이름 표기를 서버와 같게(대소문자 무시 입력 허용)
	static const TCHAR* Names[] = { TEXT("ReloadSupport"), TEXT("HackTerminal"), TEXT("HackEnemy"), TEXT("PulseAttack"), TEXT("DamageBoost"),
		TEXT("HealSupport"), TEXT("Decoy"), TEXT("QuickReboot"), TEXT("Shield") };
	FString Canonical = CommandName;
	for (const TCHAR* Name : Names)
	{
		if (CommandName.Equals(Name, ESearchCase::IgnoreCase))
		{
			Canonical = Name;
		}
	}
	FCXJeminiResponse Response;
	if (Battery < Cost)
	{
		Response.Status = TEXT("rejected");
		Response.Reason = TEXT("LOW_BATTERY");
		Response.BatteryAfter = Battery;
		Response.Dialogue = TEXT("[DEBUG] 배터리가 부족해요...");
	}
	else
	{
		Response.Status = TEXT("ok");
		Response.CommandName = Canonical;
		Response.CommandTarget = Target;
		Response.BatteryAfter = Battery - Cost;
		Response.Dialogue = FString::Printf(TEXT("[DEBUG] %s 실행!"), *Canonical);
	}
	LastRequestDebug += FString::Printf(TEXT(" debug %s %s"), *Canonical, *Target);
	HandleResponse(Response);
	return true;
}

void ACXJeminiDrone::HandleResponse(const FCXJeminiResponse& Response)
{
	bRequestPending = false;
	LastRequestDebug += FString::Printf(TEXT(" -> %s %s(%s) battery %d (after %d) reason=%s"), *Response.Status, *Response.CommandName, *Response.CommandTarget,
		Battery, Response.BatteryAfter, *Response.Reason);
	if (Response.bMock)
	{
		LastRequestDebug += TEXT(" [mock]");
	}
	// fallback(연결 끊김 등)은 mock 여부를 알 수 없으니 이전 값 유지
	if (Response.Status != TEXT("fallback"))
	{
		bServerMock = Response.bMock;
	}

	// 응답을 기다리는 사이 게임 오버·클리어·맵 이동이 됐으면 대사만
	const ALocalCoopGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ALocalCoopGameMode>() : nullptr;
	const bool bGameBlocked = GameMode && (GameMode->IsGameOver() || GameMode->IsGameCleared() || GameMode->IsTravelling());

	if (Response.IsOk() && !bGameBlocked)
	{
		// 배터리는 바로 바꾸지 않는다 — 효과가 실제로 실행된 순간 ChargePending(5-4절)
		if (ExecuteCommand(Response))
		{
			SetDialogue(Response.Dialogue, Response.Heard, Response.Status);
			// 표정: 펄스 = 화남(1.5초, 처치 후 기쁨), 그 외 실행 = 기쁨
			ReactEmotion(Response.CommandName == TEXT("PulseAttack") ? ECXDroneEmotion::Angry : ECXDroneEmotion::Happy,
				Response.CommandName == TEXT("PulseAttack") ? 1.5f : -1.0f);
		}
		else
		{
			// 응답 사이 대상이 사라짐 → 차감 없이 TARGET_LOST
			FailTargetLost(Response.CommandName == TEXT("HackTerminal") ? CXJeminiLines::TerminalGone : CXJeminiLines::TargetLost);
		}
		return;
	}

	SetDialogue(Response.Dialogue, Response.Heard, Response.Status);
	// 표정 반응: 배터리 부족·연결 실패 = 슬픔, 그 외 거절 = 기본(잠깐), 잡담 = 기본 표정 유지
	if (Response.Status == TEXT("fallback") || Response.Reason == TEXT("LOW_BATTERY"))
	{
		ReactEmotion(ECXDroneEmotion::Sad);
	}
	else if (Response.Status == TEXT("rejected"))
	{
		ReactEmotion(ECXDroneEmotion::Neutral);
	}
}

bool ACXJeminiDrone::ExecuteCommand(const FCXJeminiResponse& Response)
{
	const FString& Name = Response.CommandName;
	const FString& TargetName = Response.CommandTarget;
	ActionCommand = Name;
	PendingBatteryAfter = Response.BatteryAfter;
	bActionEffectDone = false;
	ActionTarget = nullptr;
	ActionPawn.Reset();

	APawn* Brother = FindBrother();
	APawn* Sister = FindSister();

	if (Name == TEXT("ReloadSupport"))
	{
		if (!Brother || !UCXPlayerLifeComponent::IsPawnAlive(Brother))
		{
			return false;
		}
		// 오빠에게 날아간 뒤(최대 MaxFlyTime) 재장전 — 장전이 시작된 순간 차감
		ActionTarget = Brother;
		SetAction(ECXJeminiAction::FlyToReload);
		return true;
	}

	if (Name == TEXT("HackTerminal"))
	{
		USisterHackComponent* Hack = Sister ? Sister->FindComponentByClass<USisterHackComponent>() : nullptr;
		FString PuzzleType;
		float Progress = 0.0f;
		if (Hack && Hack->GetJeminiPuzzleState(PuzzleType, Progress) && PuzzleType == TEXT("terminal") && Hack->GetActiveTerminal())
		{
			// A: 여동생이 연 단말기 — 3초 대행(원격 여동생은 Client RPC), 드론은 여동생 곁
			Hack->BeginJeminiAssist(AssistTimeTerminal);
			ActionPawn = Sister;
			ActionTarget = Hack->GetActiveTerminal();
			SetAction(ECXJeminiAction::HackAssist);
			return true;
		}
		// B: 진행 순서상 다음 잠긴 단말기로 날아가 3초 해킹
		if (AHackTerminal* Terminal = FindNextLockedTerminal())
		{
			UE_LOG(LogCXJeminiDrone, Log, TEXT("Jemini: HackTerminal mode B -> %s"), *Terminal->TerminalName);
			ActionTarget = Terminal;
			SetAction(ECXJeminiAction::TerminalHack);
			return true;
		}
		return false;
	}

	if (Name == TEXT("HackEnemy"))
	{
		USisterHackComponent* Hack = Sister ? Sister->FindComponentByClass<USisterHackComponent>() : nullptr;
		FString PuzzleType;
		float Progress = 0.0f;
		if (Hack && Hack->GetJeminiPuzzleState(PuzzleType, Progress) && PuzzleType == TEXT("enemy") && IsEnemyAlive(Hack->GetActiveEnemy()))
		{
			// A: 여동생 화살표 퍼즐 중 → 그 퍼즐을 즉시 성공(기존 ResolveEnemyHack → 서버 재검증 → ApplyHackSuccess, 퍼즐 UI도 성공으로 닫힘)
			Hack->BeginJeminiAssist(0.1f);
			ActionTarget = Hack->GetActiveEnemy();
			SetAction(ECXJeminiAction::EnemyHack);
			return true;
		}
		// B: 살아 있는 플레이어에게 가장 가까운 해킹 가능한 적(15m) 즉시 그로기 — 여동생 쿨타임과 무관
		AActor* Enemy = QueryEnemies().NearestHackable;
		ICXHackTarget* HackTarget = Cast<ICXHackTarget>(Enemy);
		if (!HackTarget || !HackTarget->ApplyHackSuccess(Sister))
		{
			return false;
		}
		ChargePending();
		bActionEffectDone = true;
		ActionTarget = Enemy;
		SetAction(ECXJeminiAction::EnemyHack);
		return true;
	}

	if (Name == TEXT("PulseAttack"))
	{
		// 대상은 응답 시점에 고정(그로기 중인 적도 대상), 비행 중 대상을 따라간다
		AActor* Enemy = QueryEnemies().NearestAlive;
		if (!Enemy)
		{
			return false;
		}
		ActionTarget = Enemy;
		SetAction(ECXJeminiAction::PulseAttack);
		return true;
	}

	if (Name == TEXT("DamageBoost"))
	{
		if (!Brother || !UCXPlayerLifeComponent::IsPawnAlive(Brother) || !Brother->FindComponentByClass<UBrotherCombatComponent>())
		{
			return false;
		}
		ActionTarget = Brother;
		SetAction(ECXJeminiAction::BoostFly);
		return true;
	}

	if (Name == TEXT("HealSupport"))
	{
		// 응답 즉시: 살아 있는 사람만 +HealAmount(최대 체력 클램프) + 초록 반짝이. 부활 없음.
		int32 Healed = 0;
		for (APawn* Pawn : GetLivingPlayers())
		{
			UCXHealthComponent* Health = UCXHealthComponent::FindOn(Pawn);
			if (Health && Health->GetHealth() < Health->GetMaxHealth() && Health->Heal(HealAmount) > 0.0f)
			{
				++Healed;
				ACXJeminiFx::Spawn(GetWorld(), ECXJeminiFxType::Sparkle, Pawn->GetActorLocation(), Pawn, this, CXJeminiFxColor::Heal, 1.0f);
			}
		}
		if (Healed == 0)
		{
			return false;
		}
		ChargePending();
		SetAction(ECXJeminiAction::Heal);
		return true;
	}

	if (Name == TEXT("Decoy"))
	{
		const FCXJeminiEnemyQuery Enemies = QueryEnemies();
		if (Enemies.ActiveEnemies.Num() == 0)
		{
			return false;
		}
		// 미끼 위치: 움직이는 적 무리 중심 너머(플레이어 반대쪽) 공중 — 거미들이 플레이어에게서 멀어지는 쪽으로 끌려간다
		FVector Center = FVector::ZeroVector;
		for (const AActor* Enemy : Enemies.ActiveEnemies)
		{
			Center += Enemy->GetActorLocation();
		}
		Center /= Enemies.ActiveEnemies.Num();
		const APawn* Nearest = nullptr;
		for (const APawn* Pawn : GetLivingPlayers())
		{
			if (!Nearest || FVector::DistSquared(Pawn->GetActorLocation(), Center) < FVector::DistSquared(Nearest->GetActorLocation(), Center))
			{
				Nearest = Pawn;
			}
		}
		FVector Away = Nearest ? (Center - Nearest->GetActorLocation()).GetSafeNormal2D() : FVector::ZeroVector;
		if (Away.IsNearlyZero())
		{
			Away = FVector(0.0f, 1.0f, 0.0f);
		}
		ActionLocation = Center + Away * DecoyOffset + FVector(0.0f, 0.0f, DecoyHeight);
		SetAction(ECXJeminiAction::Decoy);
		return true;
	}

	if (Name == TEXT("QuickReboot"))
	{
		auto IsAwaiting = [](const APawn* Pawn)
		{
			const UCXPlayerLifeComponent* Life = Pawn ? Pawn->FindComponentByClass<UCXPlayerLifeComponent>() : nullptr;
			return Life && Life->GetLifeState() == ECXLifeState::AwaitingRespawn;
		};
		// 대상: target(brother/sister)이 쓰러져 있으면 그 사람, 아니면 쓰러진 쪽
		APawn* Downed = TargetName == TEXT("sister") ? Sister : (TargetName == TEXT("brother") ? Brother : nullptr);
		if (!IsAwaiting(Downed))
		{
			Downed = IsAwaiting(Brother) ? Brother : (IsAwaiting(Sister) ? Sister : nullptr);
		}
		APawn* Partner = Downed == Brother ? Sister : Brother;
		if (!Downed || !Partner || !UCXPlayerLifeComponent::IsPawnAlive(Partner))
		{
			return false;
		}
		ActionTarget = Downed;
		ActionPawn = Partner;
		SetAction(ECXJeminiAction::Reboot);
		return true;
	}

	if (Name == TEXT("Shield"))
	{
		// 대상: target(brother/sister), 없으면 말한 사람
		APawn* Target = TargetName == TEXT("sister") ? Sister : (TargetName == TEXT("brother") ? Brother : RequestSpeaker.Get());
		if (!Target || !UCXPlayerLifeComponent::IsPawnAlive(Target) || !UCXHealthComponent::FindOn(Target))
		{
			return false;
		}
		ActionTarget = Target;
		SetAction(ECXJeminiAction::ShieldFly);
		return true;
	}

	// 모르는 명령(옛 HackAssist 등)은 무시(연동 계약) — 차감 없음
	UE_LOG(LogCXJeminiDrone, Warning, TEXT("Jemini: unknown command %s ignored"), *Name);
	PendingBatteryAfter = -1;
	return false;
}

void ACXJeminiDrone::FinishReloadFlight()
{
	APawn* Brother = Cast<APawn>(ActionTarget);
	UBrotherCombatComponent* Combat = Brother ? Brother->FindComponentByClass<UBrotherCombatComponent>() : nullptr;
	if (Combat && UCXPlayerLifeComponent::IsPawnAlive(Brother) && Combat->RequestJeminiReload())
	{
		// 재장전이 시작된 순간 차감
		ChargePending();
		EndAction();
		return;
	}
	FailTargetLost(CXJeminiLines::TargetLost);
}

void ACXJeminiDrone::SetDialogue(const FString& Text, const FString& InHeard, const FString& Status)
{
	Dialogue = Text;
	Heard = InHeard;
	DialogueStatus = Status;
	++DialogueSeq;
}

void ACXJeminiDrone::SetBattery(int32 NewBattery)
{
	Battery = FMath::Clamp(NewBattery, 0, 100);
	if (UCXGameInstance* GI = Cast<UCXGameInstance>(GetGameInstance()))
	{
		GI->JeminiBattery = Battery;
	}
}

void ACXJeminiDrone::NotifyProgressSaving()
{
	if (!HasAuthority())
	{
		return;
	}
	const FString MapName = UGameplayStatics::GetCurrentLevelName(this, true);
	if (!RechargeMapName.IsEmpty() && MapName.Equals(RechargeMapName, ESearchCase::IgnoreCase))
	{
		const bool bWasLow = Battery < 100;
		SetBattery(100);
		if (bWasLow)
		{
			SetDialogue(TEXT("충전 완료! 이제 다시 힘껏 도울게요."), FString(), TEXT("chat"));
		}
	}
	else
	{
		SetBattery(Battery);	// 게임 인스턴스와 맞춰 둔다
	}
}

FString ACXJeminiDrone::GetDebugState() const
{
	return FString::Printf(TEXT("emotion=%d battery=%d pending=%d action=%s target=%s pendingBattery=%d reason=%s seq=%d dialogue=\"%s\" heard=\"%s\" | last: %s"),
		static_cast<int32>(Emotion), Battery, bRequestPending ? 1 : 0, *UEnum::GetValueAsString(Action), *GetNameSafe(ActionTarget), PendingBatteryAfter,
		*LastLocalReason, DialogueSeq, *Dialogue, *Heard, *LastRequestDebug);
}
