// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXWardenTerminal.h"
#include "CXWardenClient.h"
#include "CXJeminiDrone.h"
#include "CoopInteractComponent.h"
#include "CXPlayerLifeComponent.h"
#include "CXShapeKit.h"
#include "CXSfx.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXWardenTerminal, Log, All);

namespace CXWardenLook
{
	const FLinearColor Red(1.0f, 0.08f, 0.05f, 1.0f);
	const FLinearColor Amber(1.0f, 0.55f, 0.05f, 1.0f);
	const FLinearColor Cyan(0.0f, 0.85f, 1.0f, 1.0f);
	const FLinearColor Steel(0.04f, 0.045f, 0.055f, 1.0f);
	const FLinearColor SteelLight(0.10f, 0.105f, 0.12f, 1.0f);

	/** 패널 가로(Y) 폭 — 사이드뷰에서 보이는 문 폭. */
	constexpr float PanelWidth = 110.0f;
	constexpr float PillarWidth = 30.0f;
	constexpr int32 StripeCount = 7;

	const TCHAR* IntroLine = TEXT("사고 대응 중. 냉각 구역 출입을 봉쇄합니다. 용건을 말씀하십시오.");
	const TCHAR* HintLine = TEXT("잠금 해제에는 근거가 필요합니다. 기록은 하나씩 말씀하셔도 누적해 확인합니다.");
	const TCHAR* CooldownLine = TEXT("처리 중입니다. 잠시 후 다시 말씀하십시오.");
	const TCHAR* AnalyzingQueuedLine = TEXT("분석 중… 방금 말씀은 접수했습니다. 분석이 끝나면 이어서 처리합니다.");
	const TCHAR* AnalyzingVoiceLine = TEXT("분석 중… 잠시 후 다시 말씀해 주십시오.");
	const TCHAR* SolvedLine = TEXT("판단을 갱신합니다. 생명 위협이 확인되어 규정에 따라 잠금을 해제합니다.");
}

ACXWardenTerminal::ACXWardenTerminal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;	// 눈 맥동 + 문 열림
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(CXShapeKit::CubePath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyFinder(CXShapeKit::BodyMaterialPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> NeonFinder(CXShapeKit::NeonMaterialPath);
	CubeMesh = CubeFinder.Object;
	BodyMaterial = BodyFinder.Object;
	NeonMaterial = NeonFinder.Object;

	ReplySound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/UI/SW_UI_Confirm.SW_UI_Confirm"));
	DeniedSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Sister/SW_Hack_Denied.SW_Hack_Denied"));
	OpenSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/World/SW_Door_Open.SW_Door_Open"));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// ---- 단말기(레인 뒤쪽에 서서 카메라(-X)를 본다)
	auto Cube = [this](const TCHAR* Name, USceneComponent* Parent, const FVector& Loc, const FVector& Size)
	{
		return CXShapeKit::MakeCube(this, Name, Parent, CubeMesh, Loc, Size);
	};
	BodyPieces.Add(Cube(TEXT("Base"), Root, FVector(0.0f, 0.0f, 45.0f), FVector(70.0f, 110.0f, 90.0f)));
	BodyPieces.Add(Cube(TEXT("Tower"), Root, FVector(10.0f, 0.0f, 170.0f), FVector(50.0f, 90.0f, 160.0f)));
	BodyPieces.Add(Cube(TEXT("Cap"), Root, FVector(0.0f, 0.0f, 258.0f), FVector(80.0f, 124.0f, 16.0f)));
	BodyPieces.Add(Cube(TEXT("FinL"), Root, FVector(0.0f, -62.0f, 170.0f), FVector(34.0f, 10.0f, 150.0f)));
	BodyPieces.Add(Cube(TEXT("FinR"), Root, FVector(0.0f, 62.0f, 170.0f), FVector(34.0f, 10.0f, 150.0f)));
	Screen = Cube(TEXT("Screen"), Root, FVector(-16.0f, 0.0f, 185.0f), FVector(4.0f, 76.0f, 92.0f));
	Eye = Cube(TEXT("Eye"), Root, FVector(-19.0f, 0.0f, 195.0f), FVector(3.0f, 28.0f, 28.0f));

	EyeLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("EyeLight"));
	EyeLight->SetupAttachment(Root);
	EyeLight->SetRelativeLocation(FVector(-70.0f, 0.0f, 195.0f));
	EyeLight->SetIntensity(2500.0f);
	EyeLight->SetAttenuationRadius(450.0f);
	EyeLight->SetLightColor(CXWardenLook::Red);
	EyeLight->SetCastShadows(false);

	auto Text = [this](const TCHAR* Name, const FVector& Loc, float Size, const FColor& Color, const TCHAR* Value)
	{
		UTextRenderComponent* T = CreateDefaultSubobject<UTextRenderComponent>(Name);
		T->SetupAttachment(Root);
		T->SetRelativeLocation(Loc);
		T->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));	// 카메라(-X 쪽)를 향하게
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextCenter);
		T->SetWorldSize(Size);
		T->SetTextRenderColor(Color);
		T->SetText(FText::FromString(Value));
		T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return T;
	};
	NameLabel = Text(TEXT("NameLabel"), FVector(-42.0f, 0.0f, 292.0f), 34.0f, FColor(255, 70, 50), TEXT("WARDEN"));
	StatusLabel = Text(TEXT("StatusLabel"), FVector(-37.0f, 0.0f, 62.0f), 13.0f, FColor(255, 150, 60), TEXT("LOCKDOWN // INCIDENT RESPONSE"));

	// ---- 보안 문(셔터). 위치·크기는 ApplyGateLayout(레벨에서 GateOffset/GateExtent를 맞춘다)
	GateRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GateRoot"));
	GateRoot->SetupAttachment(Root);

	GateBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("GateBlocker"));
	GateBlocker->SetupAttachment(GateRoot);
	GateBlocker->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	GateBlocker->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GateBlocker->SetGenerateOverlapEvents(false);
	GateBlocker->SetCanEverAffectNavigation(false);
	GateBlocker->SetHiddenInGame(true);

	for (int32 i = 0; i < 2; ++i)
	{
		GateFrame.Add(Cube(i == 0 ? TEXT("GatePillarL") : TEXT("GatePillarR"), GateRoot, FVector::ZeroVector, FVector(100.0f)));
	}
	GateFrame.Add(Cube(TEXT("GateLintel"), GateRoot, FVector::ZeroVector, FVector(100.0f)));
	GateLamp = Cube(TEXT("GateLamp"), GateRoot, FVector::ZeroVector, FVector(100.0f));

	ShutterPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ShutterPivot"));
	ShutterPivot->SetupAttachment(GateRoot);
	ShutterPanel = Cube(TEXT("ShutterPanel"), ShutterPivot, FVector::ZeroVector, FVector(100.0f));
	for (int32 i = 0; i < CXWardenLook::StripeCount; ++i)
	{
		ShutterStripes.Add(Cube(*FString::Printf(TEXT("ShutterStripe%d"), i), ShutterPivot, FVector::ZeroVector, FVector(100.0f)));
	}
}

// ---------------------------------------------------------------- 배치·비주얼

void ACXWardenTerminal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyGateLayout();
	ApplyVisualState();
}

void ACXWardenTerminal::ApplyGateLayout()
{
	using namespace CXWardenLook;
	const FVector Ext(FMath::Max(50.0f, GateExtent.X), FMath::Max(10.0f, GateExtent.Y), FMath::Max(100.0f, GateExtent.Z));
	const float H = FMath::Max(150.0f, GateVisualHeight);
	const float Depth = Ext.X * 2.0f;
	auto Place = [](UStaticMeshComponent* C, const FVector& Loc, const FVector& Size)
	{
		if (C)
		{
			C->SetRelativeLocation(Loc);
			C->SetRelativeScale3D(Size / 100.0f);
		}
	};

	GateRoot->SetRelativeLocation(GateOffset);
	GateBlocker->SetRelativeLocation(FVector(0.0f, 0.0f, Ext.Z));
	GateBlocker->SetBoxExtent(Ext);

	const float PillarY = PanelWidth * 0.5f + PillarWidth * 0.5f;
	Place(GateFrame[0], FVector(0.0f, -PillarY, (H + 40.0f) * 0.5f), FVector(Depth, PillarWidth, H + 40.0f));
	Place(GateFrame[1], FVector(0.0f, PillarY, (H + 40.0f) * 0.5f), FVector(Depth, PillarWidth, H + 40.0f));
	Place(GateFrame[2], FVector(0.0f, 0.0f, H + 20.0f), FVector(Depth, PanelWidth + 2.0f * PillarWidth + 20.0f, 40.0f));
	Place(GateLamp, FVector(-Ext.X - 3.0f, 0.0f, H + 20.0f), FVector(4.0f, PanelWidth, 14.0f));

	// 셔터: 위쪽 피벗에 매달고 열릴 때 세로 스케일을 줄인다(말려 올라감)
	ShutterPivot->SetRelativeLocation(FVector(0.0f, 0.0f, H));
	Place(ShutterPanel, FVector(0.0f, 0.0f, -H * 0.5f), FVector(Depth - 10.0f, PanelWidth, H));
	for (int32 i = 0; i < ShutterStripes.Num(); ++i)
	{
		// 카메라 쪽 면의 가로 경고선(사이드뷰에서 셔터로 읽히게)
		const float Z = -H * (i + 1) / (ShutterStripes.Num() + 1);
		Place(ShutterStripes[i], FVector(-Ext.X + 2.0f, 0.0f, Z), FVector(4.0f, PanelWidth - 14.0f, 6.0f));
	}
	ApplyOpenAlpha();
}

void ACXWardenTerminal::ApplyVisualState()
{
	using namespace CXWardenLook;
	for (UStaticMeshComponent* Piece : BodyPieces)
	{
		CXShapeKit::SetBody(Piece, BodyMaterial, Steel);
	}
	for (UStaticMeshComponent* Piece : GateFrame)
	{
		CXShapeKit::SetBody(Piece, BodyMaterial, SteelLight);
	}
	CXShapeKit::SetBody(ShutterPanel, BodyMaterial, Steel);
	const FLinearColor Accent = bSolved ? Cyan : Red;
	CXShapeKit::SetNeon(Screen, NeonMaterial, Accent * 0.25f, 1.5f);
	CXShapeKit::SetNeon(Eye, NeonMaterial, Accent, 25.0f);
	CXShapeKit::SetNeon(GateLamp, NeonMaterial, Accent, 18.0f);
	for (UStaticMeshComponent* Stripe : ShutterStripes)
	{
		CXShapeKit::SetNeon(Stripe, NeonMaterial, bSolved ? Cyan : Amber, 6.0f);
	}
	EyeLight->SetLightColor(Accent);
	NameLabel->SetTextRenderColor(bSolved ? FColor(60, 220, 255) : FColor(255, 70, 50));
	StatusLabel->SetText(FText::FromString(bSolved ? TEXT("EVACUATION ROUTE OPEN") : TEXT("LOCKDOWN // INCIDENT RESPONSE")));
	StatusLabel->SetTextRenderColor(bSolved ? FColor(60, 220, 255) : FColor(255, 150, 60));
}

void ACXWardenTerminal::ApplyOpenAlpha()
{
	if (ShutterPivot)
	{
		const float A = FMath::InterpEaseInOut(0.0f, 1.0f, OpenAlpha, 2.0f);
		ShutterPivot->SetRelativeScale3D(FVector(1.0f, 1.0f, FMath::Max(0.03f, 1.0f - A)));
	}
}

void ACXWardenTerminal::BeginPlay()
{
	Super::BeginPlay();
	ApplyGateLayout();
	ApplyVisualState();
	if (bSolved)
	{
		OpenAlpha = 1.0f;
		ApplyOpenAlpha();
		GateBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(ApproachTimer, this, &ACXWardenTerminal::CheckApproach, 0.3f, true);
	}
}

void ACXWardenTerminal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 눈 맥동(요청 중이면 빠르게)
	if (UMaterialInstanceDynamic* MID = CXShapeKit::GetOrCreateMID(Eye, NeonMaterial))
	{
		const float Speed = bRequestPending ? 9.0f : 2.2f;
		const float P = 0.5f + 0.5f * FMath::Sin(GetWorld()->GetTimeSeconds() * Speed);
		MID->SetScalarParameterValue(TEXT("Intensity"), bSolved ? 20.0f : 10.0f + 30.0f * P);
		EyeLight->SetIntensity(bSolved ? 2500.0f : 1200.0f + 2600.0f * P);
	}
	if (bSolved && OpenAlpha < 1.0f)
	{
		OpenAlpha = FMath::Min(1.0f, OpenAlpha + DeltaSeconds / FMath::Max(0.01f, OpenDuration));
		ApplyOpenAlpha();
	}
}

// ---------------------------------------------------------------- 찾기·증거

ACXWardenTerminal* ACXWardenTerminal::FindFirst(const UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<ACXWardenTerminal> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

bool ACXWardenTerminal::AwardEvidence(const UObject* WorldContextObject, FName EvidenceId, const APawn* By)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const int32 Index = CXWarden::IndexOf(EvidenceId);
	if (!World || Index == INDEX_NONE || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	bool bNew = false;
	int32 Terminals = 0;
	for (TActorIterator<ACXWardenTerminal> It(World); It; ++It)
	{
		++Terminals;
		bNew |= It->GrantEvidence(Index);
	}
	UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN evidence %s (%s) by %s: %s"), CXWarden::GetInfo(Index).Id, CXWarden::GetInfo(Index).Title,
		By ? *By->GetName() : TEXT("-"), Terminals == 0 ? TEXT("no WARDEN terminal in this map") : (bNew ? TEXT("new") : TEXT("already had")));
	return bNew;
}

bool ACXWardenTerminal::GrantEvidence(int32 Index)
{
	if (!HasAuthority() || Index < 0 || Index >= CXWarden::EvidenceCount || HasEvidence(Index))
	{
		return false;
	}
	EvidenceMask |= static_cast<uint8>(1 << Index);
	ForceNetUpdate();
	return true;
}

int32 ACXWardenTerminal::GetEvidenceNum() const
{
	int32 Num = 0;
	for (int32 i = 0; i < CXWarden::EvidenceCount; ++i)
	{
		Num += HasEvidence(i) ? 1 : 0;
	}
	return Num;
}

int32 ACXWardenTerminal::GetCreditedNum() const
{
	int32 Num = 0;
	for (int32 i = 0; i < CXWarden::EvidenceCount; ++i)
	{
		Num += (CreditedMask & (1 << i)) != 0 ? 1 : 0;
	}
	return Num;
}

TArray<FString> ACXWardenTerminal::GetCreditedIds() const
{
	TArray<FString> Ids;
	for (int32 i = 0; i < CXWarden::EvidenceCount; ++i)
	{
		if ((CreditedMask & (1 << i)) != 0 && HasEvidence(i))
		{
			Ids.Add(CXWarden::GetInfo(i).Id);
		}
	}
	return Ids;
}

TArray<FString> ACXWardenTerminal::GetEvidenceIds() const
{
	TArray<FString> Ids;
	for (int32 i = 0; i < CXWarden::EvidenceCount; ++i)
	{
		if (HasEvidence(i))
		{
			Ids.Add(CXWarden::GetInfo(i).Id);
		}
	}
	return Ids;
}

FString ACXWardenTerminal::GetSessionId() const
{
	// 서버 시도 간격 단위: 맵 + 액터 이름(영숫자·_.:- 만)
	FString Id = UGameplayStatics::GetCurrentLevelName(this, true) + TEXT(":") + GetName();
	FString Clean;
	for (const TCHAR C : Id)
	{
		Clean.AppendChar(FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('.') || C == TEXT(':') || C == TEXT('-') ? C : TEXT('_'));
	}
	return Clean.Left(64);
}

ACXWardenTerminal* ACXWardenTerminal::FindTalkTarget(const APawn* Pawn)
{
	// 2026-11-11: 오빠·여동생 모두 단말기 근처에서는 WARDEN과 말한다
	if (!Pawn || !Pawn->GetWorld() || !UCXPlayerLifeComponent::IsPawnAlive(Pawn))
	{
		return nullptr;
	}
	const FVector P = Pawn->GetActorLocation();
	for (TActorIterator<ACXWardenTerminal> It(Pawn->GetWorld()); It; ++It)
	{
		if (It->IsSolved())
		{
			continue;
		}
		// 사이드뷰: 깊이(X)는 무시하고 가로(Y)·높이(Z)로 잰다
		const FVector T = It->GetActorLocation();
		if (FVector2D(P.Y - T.Y, P.Z - T.Z).Size() <= It->TalkRange)
		{
			return *It;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------- 상호작용(F/X)

bool ACXWardenTerminal::CanInteract(const APawn* Pawn) const
{
	if (bSolved || !Pawn || !UCXPlayerLifeComponent::IsPawnAlive(Pawn))
	{
		return false;
	}
	const FVector P = Pawn->GetActorLocation();
	const FVector T = GetActorLocation();
	return FVector2D(P.Y - T.Y, P.Z - T.Z).Size() <= FMath::Min(TalkRange, 320.0f);
}

void ACXWardenTerminal::Interact(APawn* Pawn)
{
	if (bRequestPending || bSolved)
	{
		return;
	}
	// 상황 다시 듣기 — 증거가 하나도 없으면 봉쇄 안내, 있으면 근거 요구
	SetLine(GetEvidenceNum() == 0 ? CXWardenLook::IntroLine : CXWardenLook::HintLine, FString(), TEXT("intro"));
}

FVector ACXWardenTerminal::GetInteractLocation(const APawn* Pawn) const
{
	return GetActorTransform().TransformPosition(FVector(-40.0f, 0.0f, 120.0f));
}

FVector ACXWardenTerminal::GetApproachLocation(float BackY) const
{
	return GetActorTransform().TransformPosition(FVector(GateOffset.X, -BackY, 100.0f));
}

FVector ACXWardenTerminal::GetScreenAnchor() const
{
	return GetActorTransform().TransformPosition(FVector(-40.0f, 0.0f, 330.0f));
}

void ACXWardenTerminal::CheckApproach()
{
	if (bIntroduced || bSolved)
	{
		GetWorldTimerManager().ClearTimer(ApproachTimer);
		return;
	}
	const FVector T = GetActorLocation();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (Pawn && FVector2D(Pawn->GetActorLocation().Y - T.Y, Pawn->GetActorLocation().Z - T.Z).Size() <= TalkRange * 1.6f)
		{
			bIntroduced = true;
			SetLine(CXWardenLook::IntroLine, FString(), TEXT("intro"));
			GetWorldTimerManager().ClearTimer(ApproachTimer);
			return;
		}
	}
}

// ---------------------------------------------------------------- 설득(서버)

float ACXWardenTerminal::GetCooldownRemaining() const
{
	return FMath::Max(0.0f, static_cast<float>(AttemptCooldown - (GetWorld()->GetTimeSeconds() - LastAttemptTime)));
}

bool ACXWardenTerminal::BeginAttempt(APawn* From, const TCHAR* Kind, int32 InputLen)
{
	if (!HasAuthority() || bSolved)
	{
		UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN input rejected (%s, len=%d): %s"), Kind, InputLen, bSolved ? TEXT("already solved") : TEXT("no authority"));
		return false;
	}
	if (bRequestPending)
	{
		// 2026-11-13 #25: 조용히 버리지 않는다 — 이유를 대사로 보여 준다(음성은 보관 불가)
		UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN input rejected (%s, len=%d): analyzing"), Kind, InputLen);
		SetLine(CXWardenLook::AnalyzingVoiceLine, PlayerLine, TEXT("busy"));
		return false;
	}
	const float Wait = GetCooldownRemaining();
	if (Wait > 0.0f)
	{
		UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN input rejected (%s, len=%d): cooldown %.1fs left"), Kind, InputLen, Wait);
		SetLine(FString::Printf(TEXT("%s (%.0f초)"), CXWardenLook::CooldownLine, FMath::CeilToFloat(Wait)), PlayerLine, TEXT("cooldown"));
		return false;
	}
	LastAttemptTime = GetWorld()->GetTimeSeconds();
	bIntroduced = true;
	bRequestPending = true;
	ForceNetUpdate();
	return true;
}

void ACXWardenTerminal::QueueText(const FString& Text, const TCHAR* Why)
{
	const bool bReplaced = !QueuedText.IsEmpty();
	QueuedText = Text;
	UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN input queued (text, len=%d): %s%s"), Text.Len(), Why, bReplaced ? TEXT(" (replaced older queued text)") : TEXT(""));
	if (bRequestPending)
	{
		SetLine(CXWardenLook::AnalyzingQueuedLine, Text, TEXT("busy"));
	}
	else
	{
		const float Wait = GetCooldownRemaining();
		SetLine(FString::Printf(TEXT("%s %.0f초 뒤 자동으로 전달합니다."), CXWardenLook::CooldownLine, FMath::CeilToFloat(Wait)), Text, TEXT("cooldown"));
		ScheduleQueuedFlush(0.0f);
	}
}

void ACXWardenTerminal::ScheduleQueuedFlush(float MinDelay)
{
	if (QueuedText.IsEmpty() || bSolved || !GetWorld())
	{
		return;
	}
	GetWorldTimerManager().SetTimer(QueueTimer, this, &ACXWardenTerminal::FlushQueued, FMath::Max(0.05f, FMath::Max(MinDelay, GetCooldownRemaining() + 0.05f)), false);
}

void ACXWardenTerminal::FlushQueued()
{
	if (QueuedText.IsEmpty() || bSolved || bRequestPending)
	{
		return;	// 요청 중이면 HandleResponse가 다시 예약한다
	}
	const FString Text = MoveTemp(QueuedText);
	QueuedText.Reset();
	UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN queued text sent (len=%d)"), Text.Len());
	SubmitText(nullptr, Text);
}

bool ACXWardenTerminal::SubmitText(APawn* From, const FString& Text)
{
	const FString Clean = Text.TrimStartAndEnd().Left(200);
	if (Clean.IsEmpty())
	{
		return false;
	}
	if (HasAuthority() && !bSolved && (bRequestPending || GetCooldownRemaining() > 0.0f))
	{
		QueueText(Clean, bRequestPending ? TEXT("analyzing") : TEXT("cooldown"));
		return true;
	}
	if (!BeginAttempt(From, TEXT("text"), Clean.Len()))
	{
		return false;
	}
	QueuedText.Reset();
	PendingPlayerText = Clean;
	PlayerLine = Clean;
	ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	CXJemini::FEndpoint Endpoint = Drone ? Drone->GetEndpoint() : CXJemini::FEndpoint{ ACXJeminiDrone::GetConfiguredServerUrl(), FString(), 12.0f };
	TWeakObjectPtr<ACXWardenTerminal> WeakThis(this);
	CXWardenClient::SendText(Endpoint, Clean, GetEvidenceIds(), GetCreditedIds(), GetSessionId(), FOnCXWardenResponse::CreateLambda([WeakThis](const FCXWardenResponse& Response)
	{
		if (ACXWardenTerminal* Self = WeakThis.Get())
		{
			Self->HandleResponse(Response);
		}
	}));
	UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN <- text (len=%d) \"%s\" evidence=%s"), Clean.Len(), *Clean, *FString::Join(GetEvidenceIds(), TEXT(",")));
	return true;
}

bool ACXWardenTerminal::SubmitVoice(APawn* From, const TArray<uint8>& Wav)
{
	if (Wav.Num() == 0 || !BeginAttempt(From, TEXT("voice"), Wav.Num()))
	{
		return false;
	}
	PendingPlayerText.Reset();
	PlayerLine = TEXT("…");
	ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	CXJemini::FEndpoint Endpoint = Drone ? Drone->GetEndpoint() : CXJemini::FEndpoint{ ACXJeminiDrone::GetConfiguredServerUrl(), FString(), 12.0f };
	TWeakObjectPtr<ACXWardenTerminal> WeakThis(this);
	CXWardenClient::SendVoice(Endpoint, Wav, GetEvidenceIds(), GetCreditedIds(), GetSessionId(), FOnCXWardenResponse::CreateLambda([WeakThis](const FCXWardenResponse& Response)
	{
		if (ACXWardenTerminal* Self = WeakThis.Get())
		{
			Self->HandleResponse(Response);
		}
	}));
	UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN <- voice %d bytes evidence=%s"), Wav.Num(), *FString::Join(GetEvidenceIds(), TEXT(",")));
	return true;
}

void ACXWardenTerminal::HandleResponse(const FCXWardenResponse& Response)
{
	bRequestPending = false;
	bLastMock = Response.bMock;
	const FString Player = !Response.Heard.IsEmpty() ? Response.Heard : (PendingPlayerText.IsEmpty() ? PlayerLine : PendingPlayerText);
	UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN -> %s reason=%s mock=%d [%s] \"%s\""), *Response.Status, *Response.Reason, Response.bMock ? 1 : 0,
		*Response.ChecklistDebug, *Response.Reply);
	if (Response.bHasCredited)
	{
		uint8 NewMask = 0;
		for (const FString& Id : Response.Credited)
		{
			const int32 Index = CXWarden::IndexOf(Id);
			if (Index != INDEX_NONE && HasEvidence(Index))
			{
				NewMask |= static_cast<uint8>(1 << Index);
			}
		}
		CreditedMask = NewMask;
		ForceNetUpdate();
	}
	SetLine(Response.Reply, Player, Response.Status);
	// 제미니 조언 힌트용(기능 C). 시도 간격·연결 실패는 판정이 아니므로 이전 사유 유지.
	if (Response.Status == TEXT("rejected"))
	{
		LastReason = Response.Reason;
	}
	if (Response.bSolved)
	{
		QueuedText.Reset();
		Solve(TEXT("persuaded"));
	}
	else
	{
		// 분석 중에 받아 둔 텍스트가 있으면 방금 답을 읽을 시간(1.5초)을 두고 이어서 보낸다
		ScheduleQueuedFlush(1.5f);
	}
}

void ACXWardenTerminal::SetLine(const FString& Warden, const FString& Player, const FString& Status)
{
	if (!HasAuthority())
	{
		return;
	}
	WardenLine = Warden;
	PlayerLine = Player;
	LineStatus = Status;
	++LineSeq;
	ForceNetUpdate();
	OnRep_Line();
}

void ACXWardenTerminal::OnRep_Line()
{
	if (LineSeq == LocalSoundSeq)
	{
		return;
	}
	LocalSoundSeq = LineSeq;
	USoundBase* Sound = (LineStatus == TEXT("rejected") || LineStatus == TEXT("cooldown") || LineStatus == TEXT("busy")) ? DeniedSound.Get() : ReplySound.Get();
	if (Sound && LineStatus != TEXT("solved"))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation());
	}
}

void ACXWardenTerminal::ForceSolve(const FString& Why)
{
	if (!HasAuthority() || bSolved)
	{
		return;
	}
	bRequestPending = false;
	QueuedText.Reset();
	SetLine(CXWardenLook::SolvedLine, PlayerLine, TEXT("solved"));
	Solve(Why);
}

void ACXWardenTerminal::Solve(const FString& Why)
{
	if (bSolved)
	{
		return;
	}
	bSolved = true;
	ForceNetUpdate();
	UE_LOG(LogCXWardenTerminal, Log, TEXT("WARDEN solved (%s) — gate opening, evidence=%s"), *Why, *FString::Join(GetEvidenceIds(), TEXT(",")));
	OnRep_Solved();
}

void ACXWardenTerminal::OnRep_Solved()
{
	if (!bSolved)
	{
		return;
	}
	GateBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ApplyVisualState();
	if (OpenSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, OpenSound, GetActorTransform().TransformPosition(GateOffset));
	}
}

FString ACXWardenTerminal::GetDebugState() const
{
	return FString::Printf(TEXT("%s evidence=[%s] solved=%d pending=%d seq=%d status=%s line=\"%s\" player=\"%s\" gateCollision=%d"),
		*GetName(), *FString::Join(GetEvidenceIds(), TEXT(",")), bSolved ? 1 : 0, bRequestPending ? 1 : 0, LineSeq, *LineStatus, *WardenLine, *PlayerLine,
		GateBlocker && GateBlocker->IsCollisionEnabled() ? 1 : 0);
}

void ACXWardenTerminal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACXWardenTerminal, EvidenceMask);
	DOREPLIFETIME(ACXWardenTerminal, CreditedMask);
	DOREPLIFETIME(ACXWardenTerminal, bSolved);
	DOREPLIFETIME(ACXWardenTerminal, bRequestPending);
	DOREPLIFETIME(ACXWardenTerminal, WardenLine);
	DOREPLIFETIME(ACXWardenTerminal, PlayerLine);
	DOREPLIFETIME(ACXWardenTerminal, LineStatus);
	DOREPLIFETIME(ACXWardenTerminal, LineSeq);
	DOREPLIFETIME(ACXWardenTerminal, bLastMock);
}
