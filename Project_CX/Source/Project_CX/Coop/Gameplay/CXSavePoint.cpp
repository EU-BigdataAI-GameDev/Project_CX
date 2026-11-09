// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXSavePoint.h"
#include "CXJeminiDrone.h"
#include "CXShapeKit.h"
#include "CXSaveGame.h"
#include "CXSfx.h"
#include "CXPlayerLifeComponent.h"
#include "CoopInteractComponent.h"
#include "CoopHUD.h"
#include "LocalCoopGameMode.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace CXSavePointStyle
{
	const FLinearColor Idle(1.0f, 0.55f, 0.08f, 1.0f);		// 주황 — 아직 저장 안 함
	const FLinearColor Saved(0.15f, 1.0f, 0.42f, 1.0f);		// 초록 — 저장됨
	const FLinearColor Steel(0.045f, 0.05f, 0.065f, 1.0f);
	const FLinearColor SteelLight(0.11f, 0.115f, 0.13f, 1.0f);
	constexpr float FlashTime = 0.8f;
	constexpr float CheckInterval = 0.1f;

	// 대표 에셋: Soul City 발광 튜브(맵 어디에도 배치하지 않은 메시 — 장식과 헷갈리지 않음)
	const TCHAR* PylonMeshPath = TEXT("/Game/SoulCity/Environment/Meshes/Props/SM_glowstick02.SM_glowstick02");
	constexpr float PlinthTop = 20.0f;
	constexpr float BodyX = 55.0f;		// 몸체 깊이 중심(로컬 +X = 뒤)
	constexpr float SignHalfHeight = 32.0f;
	// BodyPieces 순서(생성자와 맞춤)
	constexpr int32 PlinthIndex = 0;
	constexpr int32 AntennaIndex = 4;
}

ACXSavePoint::ACXSavePoint()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.033f;	// 표시등 펄스만

	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(CXShapeKit::CubePath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyFinder(CXShapeKit::BodyMaterialPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> NeonFinder(CXShapeKit::NeonMaterialPath);
	CubeMesh = CubeFinder.Object;
	BodyMaterial = BodyFinder.Object;
	NeonMaterial = NeonFinder.Object;

	PylonMesh = CXSfx::Find<UStaticMesh>(CXSavePointStyle::PylonMeshPath);

	// 저장 = "접속 허가" 계열 확인음(해킹 성공과 같은 에셋 재사용 — 효과음 적용 기획)
	SaveSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/Sister/SW_Hack_Granted.SW_Hack_Granted"));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	ZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ZoneBox"));
	ZoneBox->SetupAttachment(Root);
	ZoneBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneBox->SetGenerateOverlapEvents(false);
	ZoneBox->SetHiddenInGame(true);
	ZoneBox->ShapeColor = FColor(255, 140, 20);
	ZoneBox->SetCanEverAffectNavigation(false);

	using namespace CXShapeKit;
	// 단말기: 받침 + 기둥 + 화면 베젤 + 비컨 하우징(뒤쪽 +X에 세운다)
	BodyPieces.Add(MakeCube(this, TEXT("Plinth"), Root, CubeMesh, FVector(45.0f, 0.0f, 10.0f), FVector(90.0f, 150.0f, 20.0f), FRotator::ZeroRotator, true));
	BodyPieces.Add(MakeCube(this, TEXT("Pillar"), Root, CubeMesh, FVector(55.0f, 0.0f, 115.0f), FVector(50.0f, 74.0f, 190.0f), FRotator::ZeroRotator, true));
	BodyPieces.Add(MakeCube(this, TEXT("ScreenBezel"), Root, CubeMesh, FVector(27.0f, 0.0f, 150.0f), FVector(8.0f, 64.0f, 54.0f)));
	BodyPieces.Add(MakeCube(this, TEXT("BeaconHousing"), Root, CubeMesh, FVector(55.0f, 0.0f, 222.0f), FVector(44.0f, 44.0f, 24.0f), FRotator::ZeroRotator, true));
	BodyPieces.Add(MakeCube(this, TEXT("Antenna"), Root, CubeMesh, FVector(62.0f, 26.0f, 270.0f), FVector(4.0f, 4.0f, 70.0f)));

	Screen = MakeCube(this, TEXT("Screen"), Root, CubeMesh, FVector(22.0f, 0.0f, 150.0f), FVector(2.0f, 54.0f, 42.0f));
	Beacon = MakeCube(this, TEXT("Beacon"), Root, CubeMesh, FVector(55.0f, 0.0f, 248.0f), FVector(28.0f, 28.0f, 28.0f));

	// 기둥 앞면 세로 상태 띠 2줄
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float Y = Side == 0 ? -31.0f : 31.0f;
		StatusStrips.Add(MakeCube(this, *FString::Printf(TEXT("StatusStrip%d"), Side), Root, CubeMesh, FVector(29.0f, Y, 85.0f), FVector(2.0f, 4.0f, 110.0f)));
	}

	// 바닥 패드(두 스폰 자리를 감싸는 사각 테두리) — 사이드뷰에서 앞쪽 테두리가 바닥 위 빛줄로 보인다
	const float PadHalfY = 150.0f;
	const float PadFront = -110.0f;
	const float PadBack = 20.0f;
	PadLines.Add(MakeCube(this, TEXT("PadFront"), Root, CubeMesh, FVector(PadFront, 0.0f, 1.5f), FVector(4.0f, PadHalfY * 2.0f, 3.0f)));
	PadLines.Add(MakeCube(this, TEXT("PadBack"), Root, CubeMesh, FVector(PadBack, 0.0f, 1.5f), FVector(4.0f, PadHalfY * 2.0f, 3.0f)));
	PadLines.Add(MakeCube(this, TEXT("PadLeft"), Root, CubeMesh, FVector((PadFront + PadBack) * 0.5f, -PadHalfY, 1.5f), FVector(PadBack - PadFront, 4.0f, 3.0f)));
	PadLines.Add(MakeCube(this, TEXT("PadRight"), Root, CubeMesh, FVector((PadFront + PadBack) * 0.5f, PadHalfY, 1.5f), FVector(PadBack - PadFront, 4.0f, 3.0f)));

	// 대표 에셋 빛 기둥 2개 + 상태색 고리/간판 막대 + 간판 뒤판 — 위치·크기는 LayoutTerminal(PylonMesh 바운드 기준)
	for (int32 Side = 0; Side < 2; ++Side)
	{
		UStaticMeshComponent* Pylon = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Pylon%d"), Side));
		Pylon->SetupAttachment(Root);
		Pylon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Pylon->SetGenerateOverlapEvents(false);
		Pylon->SetCastShadow(false);
		Pylon->SetCanEverAffectNavigation(false);
		Pylons.Add(Pylon);
	}
	for (int32 Index = 0; Index < 6; ++Index)
	{
		AccentNeon.Add(MakeCube(this, *FString::Printf(TEXT("Accent%d"), Index), Root, CubeMesh, FVector::ZeroVector, FVector(4.0f)));
	}
	SignPlate = MakeCube(this, TEXT("SignPlate"), Root, CubeMesh, FVector::ZeroVector, FVector(10.0f));

	BodyCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("BodyCollision"));
	BodyCollision->SetupAttachment(Root);
	BodyCollision->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	// 스프링암이 걸려 카메라가 당겨지지 않게(맵 COL 박스·해킹 단말기와 같은 규칙), 바닥/스폰 트레이스(Visibility)에도 안 걸리게
	BodyCollision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	BodyCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	BodyCollision->SetGenerateOverlapEvents(false);
	BodyCollision->ShapeColor = FColor(255, 60, 60);

	StatusLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StatusLight"));
	StatusLight->SetupAttachment(Root);
	StatusLight->SetRelativeLocation(FVector(-30.0f, 0.0f, 200.0f));
	StatusLight->SetIntensityUnits(ELightUnits::Candelas);
	StatusLight->SetIntensity(12.0f);
	StatusLight->SetAttenuationRadius(450.0f);
	StatusLight->SetCastShadows(false);
	StatusLight->SetLightColor(CXSavePointStyle::Idle);

	auto MakeLabel = [this](FName Name, float Z, float Size)
	{
		UTextRenderComponent* Text = CreateDefaultSubobject<UTextRenderComponent>(Name);
		Text->SetupAttachment(Root);
		Text->SetRelativeLocation(FVector(21.0f, 0.0f, Z));
		Text->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));	// 카메라(-X 쪽)를 향하게
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetWorldSize(Size);
		Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Text;
	};
	TitleLabel = MakeLabel(TEXT("TitleLabel"), 300.0f, 26.0f);
	TitleLabel->SetRelativeLocation(FVector(30.0f, 0.0f, 300.0f));
	TitleLabel->SetText(FText::FromString(TEXT("SAVE POINT")));
	TitleLabel->SetTextRenderColor(FColor(230, 235, 240));
	StatusLabel = MakeLabel(TEXT("StatusLabel"), 150.0f, 15.0f);
	StatusLabel->SetText(FText::FromString(TEXT("STANDBY")));

	LayoutTerminal();
}

void ACXSavePoint::LayoutTerminal()
{
	using namespace CXSavePointStyle;
	const bool bAsset = UsesPylonAsset();

	UStaticMeshComponent* Plinth = BodyPieces.IsValidIndex(PlinthIndex) ? BodyPieces[PlinthIndex].Get() : nullptr;
	UStaticMeshComponent* Antenna = BodyPieces.IsValidIndex(AntennaIndex) ? BodyPieces[AntennaIndex].Get() : nullptr;
	if (Antenna)
	{
		Antenna->SetVisibility(!bAsset);	// 에셋 단말기에서는 간판과 겹친다
	}
	for (UStaticMeshComponent* Pylon : Pylons)
	{
		Pylon->SetStaticMesh(PylonMesh);
		Pylon->SetVisibility(bAsset);
	}
	for (UStaticMeshComponent* Accent : AccentNeon)
	{
		Accent->SetVisibility(bAsset);
	}
	SignPlate->SetVisibility(bAsset);

	float BodyTop = 250.0f;
	float BodyHalfY = 40.0f;
	if (bAsset)
	{
		// 기둥: 바운드 중심을 (BodyX, ±PylonSpacing)에, 바닥을 받침 윗면에 — 피벗이 어디든 같은 자리에 선다
		const FBox Bounds = PylonMesh->GetBoundingBox();
		const FVector Center = Bounds.GetCenter() * PylonScale;
		const FVector Size = Bounds.GetSize() * PylonScale;
		const float Height = Size.Z;
		const float Radius = 0.5f * FMath::Max(Size.X, Size.Y);
		const float Top = PlinthTop + Height;
		for (int32 Side = 0; Side < Pylons.Num(); ++Side)
		{
			const float Y = (Side == 0 ? -1.0f : 1.0f) * PylonSpacing;
			Pylons[Side]->SetRelativeScale3D(FVector(PylonScale));
			Pylons[Side]->SetRelativeLocation(FVector(BodyX - Center.X, Y - Center.Y, PlinthTop - Bounds.Min.Z * PylonScale));

			// 상태색 고리(기둥 아래·위 — 발광 튜브의 어두운 마개 바로 안쪽)
			const FVector RingSize(2.0f * Radius + 8.0f, 2.0f * Radius + 8.0f, 6.0f);
			AccentNeon[Side * 2]->SetRelativeLocation(FVector(BodyX, Y, PlinthTop + Height * 0.14f));
			AccentNeon[Side * 2]->SetRelativeScale3D(RingSize / 100.0f);
			AccentNeon[Side * 2 + 1]->SetRelativeLocation(FVector(BodyX, Y, Top - Height * 0.14f));
			AccentNeon[Side * 2 + 1]->SetRelativeScale3D(RingSize / 100.0f);
		}

		// 기둥 꼭대기를 잇는 간판: 어두운 뒤판 + 위아래 상태색 막대 + SAVE POINT 글씨
		const float SignZ = Top + SignHalfHeight + 6.0f;
		const float SignWidth = 2.0f * (PylonSpacing + Radius) + 20.0f;
		SignPlate->SetRelativeLocation(FVector(BodyX + 3.0f, 0.0f, SignZ));
		SignPlate->SetRelativeScale3D(FVector(10.0f, SignWidth, 2.0f * SignHalfHeight) / 100.0f);
		AccentNeon[4]->SetRelativeLocation(FVector(BodyX - 4.0f, 0.0f, SignZ + SignHalfHeight));
		AccentNeon[4]->SetRelativeScale3D(FVector(5.0f, SignWidth + 10.0f, 6.0f) / 100.0f);
		AccentNeon[5]->SetRelativeLocation(FVector(BodyX - 4.0f, 0.0f, SignZ - SignHalfHeight));
		AccentNeon[5]->SetRelativeScale3D(FVector(5.0f, SignWidth + 10.0f, 6.0f) / 100.0f);
		TitleLabel->SetRelativeLocation(FVector(BodyX - 4.0f, 0.0f, SignZ));
		TitleLabel->SetWorldSize(36.0f);

		if (Plinth)
		{
			Plinth->SetRelativeScale3D(FVector(90.0f, 2.0f * (PylonSpacing + Radius) + 40.0f, PlinthTop) / 100.0f);
		}
		BodyTop = SignZ + SignHalfHeight + 3.0f;
		BodyHalfY = PylonSpacing + Radius + 10.0f;
	}
	else
	{
		TitleLabel->SetRelativeLocation(FVector(30.0f, 0.0f, 300.0f));
		TitleLabel->SetWorldSize(26.0f);
		if (Plinth)
		{
			Plinth->SetRelativeScale3D(FVector(90.0f, 150.0f, PlinthTop) / 100.0f);
		}
	}

	// 몸체 충돌: 받침 앞 끝(X 10)부터 뒤로 — 앞쪽(스폰 자리 X -40 ± 캡슐)은 비워 둔다
	BodyCollision->SetRelativeLocation(FVector(BodyX, 0.0f, 0.5f * BodyTop));
	BodyCollision->SetBoxExtent(FVector(BodyX - 10.0f, BodyHalfY, 0.5f * BodyTop));
	BodyCollision->SetCollisionEnabled(bBlockPawns ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

void ACXSavePoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACXSavePoint, bSaved);
	DOREPLIFETIME(ACXSavePoint, SaveSerial);
}

void ACXSavePoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (ZoneBox)
	{
		ZoneBox->SetBoxExtent(ZoneExtent);
		ZoneBox->SetRelativeLocation(FVector(0.0f, 0.0f, ZoneExtent.Z));
	}
	LayoutTerminal();
	ApplyMaterials();
}

void ACXSavePoint::BeginPlay()
{
	Super::BeginPlay();
	ApplyMaterials();
	if (HasAuthority() && bAutoSaveOnEnter)
	{
		GetWorldTimerManager().SetTimer(ZoneTimer, this, &ACXSavePoint::CheckZone, CXSavePointStyle::CheckInterval, true);
	}
}

void ACXSavePoint::ApplyMaterials()
{
	using namespace CXShapeKit;
	for (UStaticMeshComponent* Piece : BodyPieces)
	{
		SetBody(Piece, BodyMaterial, Piece->GetName().Contains(TEXT("Bezel")) ? CXSavePointStyle::SteelLight : CXSavePointStyle::Steel);
	}
	SetBody(SignPlate, BodyMaterial, CXSavePointStyle::Steel * 0.5f);
	RefreshVisuals(0.0f);
}

void ACXSavePoint::RefreshVisuals(float Time)
{
	using namespace CXShapeKit;
	const FLinearColor Color = bSaved ? CXSavePointStyle::Saved : CXSavePointStyle::Idle;

	// 대기 = 느린 숨쉬기, 저장됨 = 고정 + 저장 직후 플래시
	float Boost = 0.0f;
	const float Since = GetTimeSinceSaved();
	if (Since >= 0.0f && Since < CXSavePointStyle::FlashTime)
	{
		Boost = 1.0f - Since / CXSavePointStyle::FlashTime;
	}
	const float Breath = bSaved ? 1.0f : 0.6f + 0.4f * (0.5f + 0.5f * FMath::Sin(Time * 2.4f));

	SetNeon(Beacon, NeonMaterial, Color, 18.0f * Breath + 60.0f * Boost);
	SetNeon(Screen, NeonMaterial, Color * 0.5f + FLinearColor(0.02f, 0.03f, 0.05f, 0.0f), 2.5f + 8.0f * Boost);
	for (UStaticMeshComponent* Strip : StatusStrips)
	{
		SetNeon(Strip, NeonMaterial, Color, 6.0f * Breath + 20.0f * Boost);
	}
	for (UStaticMeshComponent* Line : PadLines)
	{
		SetNeon(Line, NeonMaterial, Color, 3.0f * Breath + 12.0f * Boost);
	}
	for (UStaticMeshComponent* Accent : AccentNeon)
	{
		SetNeon(Accent, NeonMaterial, Color, 10.0f * Breath + 40.0f * Boost);
	}
	if (StatusLight)
	{
		StatusLight->SetLightColor(Color);
		StatusLight->SetIntensity(10.0f * Breath + 40.0f * Boost);
	}
	if (StatusLabel)
	{
		StatusLabel->SetText(FText::FromString(bSaved ? TEXT("SAVED") : TEXT("STANDBY")));
		StatusLabel->SetTextRenderColor(Color.ToFColor(true));
	}
}

void ACXSavePoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const UWorld* World = GetWorld())
	{
		RefreshVisuals(World->GetRealTimeSeconds());
	}
}

// ---------------------------------------------------------------- 판정 / 저장

FName ACXSavePoint::GetSavePointId() const
{
	return SavePointId.IsNone() ? GetFName() : SavePointId;
}

FString ACXSavePoint::ResolveZoneName() const
{
	if (!ZoneName.IsEmpty())
	{
		return ZoneName;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (const ACoopHUD* HUD = PC ? Cast<ACoopHUD>(PC->GetHUD()) : nullptr)
		{
			if (!HUD->GetZoneTitle().IsEmpty())
			{
				return HUD->GetZoneTitle();
			}
		}
	}
	return TEXT("UNDERCITY");
}

bool ACXSavePoint::IsPawnInZone(const APawn* Pawn) const
{
	return Pawn && CXShapeKit::IsInFloorBox(GetActorTransform(), ZoneExtent, Pawn->GetActorLocation());
}

void ACXSavePoint::CheckZone()
{
	bool bAnyInside = false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn && UCXPlayerLifeComponent::IsPawnAlive(Pawn) && IsPawnInZone(Pawn))
		{
			bAnyInside = true;
			break;
		}
	}
	if (!bAnyInside)
	{
		bArmed = true;
		return;
	}
	if (bArmed)
	{
		// 저장 불가 상태(게임 오버/클리어/이동 중)면 무장을 유지해 다음 확인 때 다시 시도
		if (SaveNow())
		{
			bArmed = false;
		}
	}
}

bool ACXSavePoint::CanInteract(const APawn* Pawn) const
{
	// 오빠/여동생 누구나, 살아 있고 구역 안이면 — 이미 저장됐어도 다시 저장 가능(진행 위치 갱신)
	return Pawn && UCoopInteractComponent::IsPawnAbleToInteract(Pawn) && IsPawnInZone(Pawn);
}

void ACXSavePoint::Interact(APawn* Pawn)
{
	if (!HasAuthority() || !CanInteract(Pawn))
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastInteractSaveTime < InteractCooldown)
	{
		return;
	}
	if (SaveNow())
	{
		LastInteractSaveTime = Now;
		UE_LOG(LogTemp, Log, TEXT("CXSavePoint %s: saved by interact (%s)"), *GetSavePointId().ToString(), *GetNameSafe(Pawn));
	}
}

FVector ACXSavePoint::GetInteractLocation(const APawn* Pawn) const
{
	// HUD 안내 = 단말기 화면(사이드뷰에서 몸체 앞면)
	return Screen ? Screen->GetComponentLocation() : GetActorLocation() + FVector(0.0f, 0.0f, 150.0f);
}

bool ACXSavePoint::SaveNow()
{
	if (!HasAuthority())
	{
		return false;
	}
	ALocalCoopGameMode* GameMode = GetWorld()->GetAuthGameMode<ALocalCoopGameMode>();
	if (GameMode && !GameMode->CanSaveProgress())
	{
		return false;
	}

	UCXSaveGame* Save = Cast<UCXSaveGame>(UGameplayStatics::CreateSaveGameObject(UCXSaveGame::StaticClass()));
	if (!Save)
	{
		return false;
	}
	// 진행 플래그는 기존 세이브 것을 이어받는다(지금은 쓰는 곳 없음)
	UCXSaveGame* Previous = nullptr;
	if (UCXSaveGame::LoadSlot(Previous) == ECXSaveLoadResult::Loaded && Previous)
	{
		Save->ProgressFlags = Previous->ProgressFlags;
	}
	Save->MapName = UCXSaveGame::GetWorldMapPackageName(this);
	Save->SavePointId = GetSavePointId();
	Save->ZoneName = ResolveZoneName();
	Save->SaveTime = FDateTime::Now();
	Save->SpawnLocation = GetActorLocation();
	Save->SpawnYaw = GetActorRotation().Yaw;
	// 제미니 배터리: 세이브 복도면 저장 직전에 100으로 충전한 뒤 기록(제미니 AI봇 기획 B2)
	if (ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this))
	{
		Drone->NotifyProgressSaving();
		Save->JeminiBattery = Drone->GetBattery();
	}

	if (!UGameplayStatics::SaveGameToSlot(Save, UCXSaveGame::SlotName, UCXSaveGame::UserIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("CXSavePoint %s: SaveGameToSlot failed"), *GetSavePointId().ToString());
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("CXSavePoint %s: saved (%s)"), *GetSavePointId().ToString(), *UCXSaveGame::GetSlotDebugState());

	bArmed = false;	// 지금 구역 안에 있는 사람은 나갔다 들어와야 다시 저장
	SetSavedState(true);
	if (GameMode)
	{
		GameMode->NotifyProgressSaved(this);
	}
	return true;
}

void ACXSavePoint::MarkSpawnedHere()
{
	if (!HasAuthority())
	{
		return;
	}
	bArmed = false;
	SetSavedState(false);
}

void ACXSavePoint::SetSavedState(bool bWithFeedback)
{
	bSaved = true;
	if (bWithFeedback)
	{
		++SaveSerial;
	}
	else
	{
		// 피드백 없이 저장됨 상태만(스폰 직후) — 서버 로컬 OnRep에서 배너가 뜨지 않게 같이 맞춘다
		LastSeenSerial = SaveSerial;
	}
	OnRep_SaveState();
}

void ACXSavePoint::OnRep_SaveState()
{
	// 클라이언트가 처음 받은 값이 이미 저장된 상태면 피드백 없이 맞추기만
	if (!HasAuthority() && !bSeenFirstRep)
	{
		bSeenFirstRep = true;
		LastSeenSerial = SaveSerial;
	}
	if (SaveSerial != LastSeenSerial)
	{
		LastSeenSerial = SaveSerial;
		LastFeedbackRealTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
		if (SaveSound)
		{
			UGameplayStatics::PlaySound2D(this, SaveSound);
		}
	}
	RefreshVisuals(GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0f);
}

float ACXSavePoint::GetTimeSinceSaved() const
{
	const UWorld* World = GetWorld();
	if (LastFeedbackRealTime < 0.0 || !World)
	{
		return -1.0f;
	}
	return static_cast<float>(World->GetRealTimeSeconds() - LastFeedbackRealTime);
}

FVector ACXSavePoint::GetSpawnFloorFor(bool bSister) const
{
	const FVector Local(SpawnDepth, (bSister ? 1.0f : -1.0f) * SpawnSpacing, 0.0f);
	FVector Floor = GetActorTransform().TransformPositionNoScale(Local);

	// 바닥 찾기(폰 무시) — 원점이 바닥에서 조금 떠 있거나 묻혀 있어도 맞춘다
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXSavePointSpawn), false, this);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			Params.AddIgnoredActor(PC->GetPawn());
		}
	}
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Floor + FVector(0.0f, 0.0f, 150.0f), Floor - FVector(0.0f, 0.0f, 400.0f), ECC_Visibility, Params) && !Hit.bStartPenetrating)
	{
		Floor.Z = Hit.ImpactPoint.Z;
	}
	return Floor;
}

ACXSavePoint* ACXSavePoint::FindById(const UWorld* World, FName Id)
{
	if (!World || Id.IsNone())
	{
		return nullptr;
	}
	for (TActorIterator<ACXSavePoint> It(World); It; ++It)
	{
		if (It->GetSavePointId() == Id)
		{
			return *It;
		}
	}
	return nullptr;
}

FString ACXSavePoint::GetDebugState() const
{
	return FString::Printf(TEXT("%s id=%s saved=%d serial=%d armed=%d since=%.2f loc=%s terminal=%s"), *GetName(), *GetSavePointId().ToString(),
		bSaved ? 1 : 0, SaveSerial, bArmed ? 1 : 0, GetTimeSinceSaved(), *GetActorLocation().ToCompactString(),
		UsesPylonAsset() ? *PylonMesh->GetName() : TEXT("shape-fallback"));
}

// ---------------------------------------------------------------- 테스트용

ACXSavePoint* ACXSavePoint::DebugSpawnSavePoint(const UObject* WorldContextObject, float OffsetY, float OffsetX)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}
	const APlayerController* PC = World->GetFirstPlayerController();
	const APawn* Anchor = PC ? PC->GetPawn() : nullptr;
	if (!Anchor)
	{
		return nullptr;
	}
	FVector Location = Anchor->GetActorLocation() + FVector(OffsetX, OffsetY, -Anchor->GetSimpleCollisionHalfHeight());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXSavePointDebugSpawn), false);
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* Each = It->Get())
		{
			Params.AddIgnoredActor(Each->GetPawn());
		}
	}
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Location + FVector(0.0f, 0.0f, 20.0f), Location - FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params) && !Hit.bStartPenetrating)
	{
		Location.Z = Hit.ImpactPoint.Z;
	}
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ACXSavePoint>(Location, FRotator::ZeroRotator, SpawnParams);
}
