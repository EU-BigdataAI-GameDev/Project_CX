// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXLevelExit.h"
#include "CXShapeKit.h"
#include "CXPlayerLifeComponent.h"
#include "LocalCoopGameMode.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace CXLevelExitStyle
{
	const FLinearColor Idle(0.45f, 0.88f, 1.0f, 1.0f);		// 차가운 청백 — 출구
	const FLinearColor Waiting(1.0f, 0.55f, 0.08f, 1.0f);	// 주황 — 누군가 들어와 대기 중
	const FLinearColor Offline(1.0f, 0.12f, 0.08f, 1.0f);	// 빨강 — 다음 구역 없음
	const FLinearColor Steel(0.045f, 0.05f, 0.065f, 1.0f);
	constexpr int32 NumArrows = 3;
	constexpr float ArrowSpacing = 62.0f;
	constexpr float StrokeLength = 52.0f;
	constexpr float CheckInterval = 0.1f;
}

ACXLevelExit::ACXLevelExit()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.033f;	// 화살표 순차 점멸

	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(CXShapeKit::CubePath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyFinder(CXShapeKit::BodyMaterialPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> NeonFinder(CXShapeKit::NeonMaterialPath);
	CubeMesh = CubeFinder.Object;
	BodyMaterial = BodyFinder.Object;
	NeonMaterial = NeonFinder.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	ZoneBox = CreateDefaultSubobject<UBoxComponent>(TEXT("ZoneBox"));
	ZoneBox->SetupAttachment(Root);
	ZoneBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ZoneBox->SetGenerateOverlapEvents(false);
	ZoneBox->SetHiddenInGame(true);
	ZoneBox->ShapeColor = FColor(110, 220, 255);
	ZoneBox->SetCanEverAffectNavigation(false);

	// 위치/크기는 LayoutVisuals(OnConstruction)가 설정값에 맞춰 잡는다
	using namespace CXShapeKit;
	const TCHAR* FloorNames[] = { TEXT("FloorFront"), TEXT("FloorBack"), TEXT("FloorLeft"), TEXT("FloorRight") };
	for (const TCHAR* Name : FloorNames)
	{
		FloorLines.Add(MakeCube(this, Name, Root, CubeMesh, FVector::ZeroVector, FVector(4.0f)));
	}
	const TCHAR* BodyNames[] = { TEXT("PostLeft"), TEXT("PostRight"), TEXT("Beam") };
	for (const TCHAR* Name : BodyNames)
	{
		FrameBody.Add(MakeCube(this, Name, Root, CubeMesh, FVector::ZeroVector, FVector(4.0f), FRotator::ZeroRotator, true));
	}
	const TCHAR* NeonNames[] = { TEXT("NeonLeft"), TEXT("NeonRight"), TEXT("NeonTop") };
	for (const TCHAR* Name : NeonNames)
	{
		FrameNeon.Add(MakeCube(this, Name, Root, CubeMesh, FVector::ZeroVector, FVector(4.0f)));
	}
	for (int32 i = 0; i < CXLevelExitStyle::NumArrows * 2; ++i)
	{
		ArrowStrokes.Add(MakeCube(this, *FString::Printf(TEXT("Arrow%d_%s"), i / 2, (i % 2) ? TEXT("Low") : TEXT("Up")), Root, CubeMesh, FVector::ZeroVector, FVector(4.0f)));
	}

	LabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LabelText"));
	LabelText->SetupAttachment(Root);
	LabelText->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));	// 카메라(-X 쪽)를 향하게
	LabelText->SetHorizontalAlignment(EHTA_Center);
	LabelText->SetVerticalAlignment(EVRTA_TextCenter);
	LabelText->SetWorldSize(40.0f);
	LabelText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACXLevelExit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACXLevelExit, bTravelling);
	DOREPLIFETIME(ACXLevelExit, OfflineSerial);
}

// ---------------------------------------------------------------- 비주얼

void ACXLevelExit::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	LayoutVisuals();
}

void ACXLevelExit::LayoutVisuals()
{
	using namespace CXShapeKit;
	const float Dir = ArrowDirection >= 0.0f ? 1.0f : -1.0f;
	const FVector E = ZoneExtent.ComponentMax(FVector(20.0f));

	if (ZoneBox)
	{
		ZoneBox->SetBoxExtent(E);
		ZoneBox->SetRelativeLocation(FVector(0.0f, 0.0f, E.Z));
	}

	auto PlaceRot = [](UStaticMeshComponent* Mesh, const FVector& Location, const FVector& Size, const FRotator& Rotation)
	{
		if (Mesh)
		{
			Mesh->SetRelativeLocation(Location);
			Mesh->SetRelativeRotation(Rotation);
			Mesh->SetRelativeScale3D(Size / 100.0f);
		}
	};
	auto Place = [&PlaceRot](UStaticMeshComponent* Mesh, const FVector& Location, const FVector& Size)
	{
		PlaceRot(Mesh, Location, Size, FRotator::ZeroRotator);
	};

	// 바닥 구역 테두리(앞/뒤는 레인 가장자리에서 조금 안쪽)
	const float FrontX = -E.X * 0.85f;
	const float BackX = FMath::Min(E.X * 0.85f, FrameDepth - 20.0f);
	if (FloorLines.Num() == 4)
	{
		Place(FloorLines[0], FVector(FrontX, 0.0f, 1.5f), FVector(5.0f, E.Y * 2.0f, 3.0f));
		Place(FloorLines[1], FVector(BackX, 0.0f, 1.5f), FVector(5.0f, E.Y * 2.0f, 3.0f));
		Place(FloorLines[2], FVector((FrontX + BackX) * 0.5f, -E.Y, 1.5f), FVector(BackX - FrontX, 5.0f, 3.0f));
		Place(FloorLines[3], FVector((FrontX + BackX) * 0.5f, E.Y, 1.5f), FVector(BackX - FrontX, 5.0f, 3.0f));
	}

	// 뒤쪽 프레임(정면 — 카메라에서 문틀로 읽힘)
	const float FrameH = SignHeight + 90.0f;
	if (FrameBody.Num() == 3 && FrameNeon.Num() == 3)
	{
		Place(FrameBody[0], FVector(FrameDepth + 10.0f, -E.Y, FrameH * 0.5f), FVector(24.0f, 24.0f, FrameH));
		Place(FrameBody[1], FVector(FrameDepth + 10.0f, E.Y, FrameH * 0.5f), FVector(24.0f, 24.0f, FrameH));
		Place(FrameBody[2], FVector(FrameDepth + 10.0f, 0.0f, FrameH), FVector(24.0f, E.Y * 2.0f + 24.0f, 24.0f));
		Place(FrameNeon[0], FVector(FrameDepth - 4.0f, -E.Y + 14.0f, FrameH * 0.5f), FVector(4.0f, 4.0f, FrameH - 10.0f));
		Place(FrameNeon[1], FVector(FrameDepth - 4.0f, E.Y - 14.0f, FrameH * 0.5f), FVector(4.0f, 4.0f, FrameH - 10.0f));
		Place(FrameNeon[2], FVector(FrameDepth - 4.0f, 0.0f, FrameH - 14.0f), FVector(4.0f, E.Y * 2.0f - 24.0f, 4.0f));
		for (UStaticMeshComponent* Mesh : FrameBody)
		{
			Mesh->SetVisibility(bShowFrame);
		}
		for (UStaticMeshComponent* Mesh : FrameNeon)
		{
			Mesh->SetVisibility(bShowFrame);
		}
	}

	// 화살표 3개(진행 방향 쪽이 끝) — 획 2개가 끝점에서 만나는 ">" 모양
	// 롤 +45°면 큐브의 Y축이 (0, cos, -sin) = 진행 방향 아래쪽 → 위 획. 아래 획은 반대 롤.
	const float Half = CXLevelExitStyle::StrokeLength * 0.5f * UE_INV_SQRT_2;
	for (int32 i = 0; i < CXLevelExitStyle::NumArrows && ArrowStrokes.Num() >= (i + 1) * 2; ++i)
	{
		const float TipY = Dir * (i - (CXLevelExitStyle::NumArrows - 1) * 0.5f) * CXLevelExitStyle::ArrowSpacing + Dir * Half;
		const float CenterY = TipY - Dir * Half;
		PlaceRot(ArrowStrokes[i * 2], FVector(FrameDepth - 8.0f, CenterY, SignHeight + Half), FVector(4.0f, CXLevelExitStyle::StrokeLength, 9.0f), FRotator(0.0f, 0.0f, 45.0f * Dir));
		PlaceRot(ArrowStrokes[i * 2 + 1], FVector(FrameDepth - 8.0f, CenterY, SignHeight - Half), FVector(4.0f, CXLevelExitStyle::StrokeLength, 9.0f), FRotator(0.0f, 0.0f, -45.0f * Dir));
	}

	if (LabelText)
	{
		LabelText->SetRelativeLocation(FVector(FrameDepth - 8.0f, 0.0f, SignHeight + 62.0f));
		LabelText->SetText(FText::FromString(Label));
	}

	for (UStaticMeshComponent* Mesh : FrameBody)
	{
		SetBody(Mesh, BodyMaterial, CXLevelExitStyle::Steel);
	}
	RefreshVisuals(0.0f);
}

void ACXLevelExit::RefreshVisuals(float Time)
{
	using namespace CXShapeKit;

	// 상태색: 오프라인 알림 중 빨강 > 이동 중 흰색 > 누군가 구역 안 주황 > 기본 청백
	FLinearColor Color = CXLevelExitStyle::Idle;
	bool bSomeoneInside = false;
	if (const UWorld* World = GetWorld(); World && World->IsGameWorld())
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (It->IsPlayerControlled() && UCXPlayerLifeComponent::IsPawnAlive(*It) && IsPawnInZone(*It))
			{
				bSomeoneInside = true;
				break;
			}
		}
	}
	const float OfflineAge = GetOfflineNoticeAge();
	const bool bOffline = OfflineAge >= 0.0f && OfflineAge < OfflineNoticeTime;
	if (bOffline)
	{
		Color = CXLevelExitStyle::Offline;
	}
	else if (bTravelling)
	{
		Color = FLinearColor(0.9f, 0.95f, 1.0f, 1.0f);
	}
	else if (bSomeoneInside)
	{
		Color = CXLevelExitStyle::Waiting;
	}

	const float Blink = bOffline ? (FMath::Fmod(Time * 4.0f, 1.0f) < 0.5f ? 1.0f : 0.35f) : 1.0f;
	for (UStaticMeshComponent* Mesh : FrameNeon)
	{
		SetNeon(Mesh, NeonMaterial, Color, 8.0f * Blink);
	}
	for (UStaticMeshComponent* Mesh : FloorLines)
	{
		SetNeon(Mesh, NeonMaterial, Color, 5.0f * Blink);
	}
	// 화살표 순차 점멸(뒤 → 앞으로 흐름)
	for (int32 i = 0; i < CXLevelExitStyle::NumArrows; ++i)
	{
		const float Phase = FMath::Fmod(Time * 1.4f - i * 0.22f + 10.0f, 1.0f);
		const float Glow = bOffline ? 0.3f * Blink : 0.25f + 0.75f * FMath::Clamp(1.0f - FMath::Abs(Phase - 0.3f) * 3.0f, 0.0f, 1.0f);
		for (int32 s = 0; s < 2; ++s)
		{
			if (ArrowStrokes.IsValidIndex(i * 2 + s))
			{
				SetNeon(ArrowStrokes[i * 2 + s], NeonMaterial, Color, 4.0f + 22.0f * Glow);
			}
		}
	}
	if (LabelText)
	{
		LabelText->SetText(FText::FromString(bOffline ? TEXT("OFFLINE") : Label));
		LabelText->SetTextRenderColor(Color.ToFColor(true));
	}
}

void ACXLevelExit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (const UWorld* World = GetWorld())
	{
		RefreshVisuals(World->GetRealTimeSeconds());
	}
}

// ---------------------------------------------------------------- 판정

void ACXLevelExit::BeginPlay()
{
	Super::BeginPlay();
	LayoutVisuals();
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(ZoneTimer, this, &ACXLevelExit::CheckZone, CXLevelExitStyle::CheckInterval, true);
	}
}

FString ACXLevelExit::GetNextMapPackage() const
{
	// PIE에서 맵을 OpenLevel로 다시 읽으면 소프트 경로가 UEDPIE_N_ 접두사로 고쳐진다 → 원래 패키지 이름으로
	return NextMap.IsNull() ? FString() : UWorld::RemovePIEPrefix(NextMap.ToSoftObjectPath().GetLongPackageName());
}

void ACXLevelExit::SetNextMapPackage(const FString& MapPackageName)
{
	Modify();
	if (MapPackageName.IsEmpty())
	{
		NextMap.Reset();
		return;
	}
	// 롱 패키지 이름 → 월드 에셋 경로(/Game/.../Map.Map). 패키지가 없어도 경로만 저장한다.
	const FString AssetName = FPackageName::GetLongPackageAssetName(MapPackageName);
	NextMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(MapPackageName + TEXT(".") + AssetName));
}

bool ACXLevelExit::IsNextMapAvailable() const
{
	const FString Package = GetNextMapPackage();
	return !Package.IsEmpty() && FPackageName::DoesPackageExist(Package);
}

bool ACXLevelExit::IsPawnInZone(const APawn* Pawn) const
{
	return Pawn && CXShapeKit::IsInFloorBox(GetActorTransform(), ZoneExtent, Pawn->GetActorLocation());
}

void ACXLevelExit::CheckZone()
{
	if (bTravelling)
	{
		return;
	}
	int32 Total = 0;
	int32 AliveInside = 0;
	int32 AnyInside = 0;
	bool bAllAlive = true;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		++Total;
		const APawn* Pawn = PC->GetPawn();
		const bool bAlive = Pawn && UCXPlayerLifeComponent::IsPawnAlive(Pawn);
		bAllAlive &= bAlive;
		if (Pawn && IsPawnInZone(Pawn))
		{
			++AnyInside;
			AliveInside += bAlive ? 1 : 0;
		}
	}
	if (AnyInside == 0)
	{
		bArmed = true;
		return;
	}
	// 살아 있는 전원이 안에 있어야(사망/부활 대기 중인 사람이 있으면 막힘)
	if (!bArmed || !bAllAlive || Total == 0 || AliveInside < Total)
	{
		return;
	}
	bArmed = false;
	ForceTravel();
}

bool ACXLevelExit::ForceTravel()
{
	if (!HasAuthority() || bTravelling)
	{
		return false;
	}
	const FString Package = GetNextMapPackage();
	if (!IsNextMapAvailable())
	{
		UE_LOG(LogTemp, Warning, TEXT("CXLevelExit %s: next map '%s' does not exist yet -> NEXT AREA OFFLINE"), *GetName(), *Package);
		ShowOffline();
		return false;
	}
	bool bStarted = false;
	if (ALocalCoopGameMode* GameMode = GetWorld()->GetAuthGameMode<ALocalCoopGameMode>())
	{
		bStarted = GameMode->TravelToMap(Package);
	}
	else
	{
		UGameplayStatics::OpenLevel(this, FName(*Package));
		bStarted = true;
	}
	if (bStarted)
	{
		bTravelling = true;
	}
	else
	{
		// 게임 오버/클리어/이미 이동 중 — 다음 진입 때 다시
		bArmed = true;
	}
	UE_LOG(LogTemp, Log, TEXT("CXLevelExit %s: travel to %s -> %s"), *GetName(), *Package, bStarted ? TEXT("started") : TEXT("refused"));
	return bStarted;
}

void ACXLevelExit::ShowOffline()
{
	++OfflineSerial;
	OnRep_Offline();
}

void ACXLevelExit::OnRep_Offline()
{
	if (!HasAuthority() && !bSeenFirstRep)
	{
		bSeenFirstRep = true;
		LastSeenOfflineSerial = OfflineSerial;
		return;
	}
	if (OfflineSerial != LastSeenOfflineSerial)
	{
		LastSeenOfflineSerial = OfflineSerial;
		LastOfflineRealTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	}
}

float ACXLevelExit::GetOfflineNoticeAge() const
{
	const UWorld* World = GetWorld();
	if (LastOfflineRealTime < 0.0 || !World)
	{
		return -1.0f;
	}
	return static_cast<float>(World->GetRealTimeSeconds() - LastOfflineRealTime);
}

FString ACXLevelExit::GetWaitText(const APawn* ForPawn) const
{
	if (bTravelling || !ForPawn || !UCXPlayerLifeComponent::IsPawnAlive(ForPawn) || !IsPawnInZone(ForPawn))
	{
		return FString();
	}
	const float OfflineAge = GetOfflineNoticeAge();
	if (OfflineAge >= 0.0f && OfflineAge < OfflineNoticeTime)
	{
		return FString();
	}
	// 클라이언트에서도 동작하도록 컨트롤러가 아니라 플레이어 폰을 훑는다
	bool bPartnerDown = false;
	bool bPartnerOutside = false;
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const APawn* Other = *It;
		if (Other == ForPawn || !Other->IsPlayerControlled())
		{
			continue;
		}
		if (!UCXPlayerLifeComponent::IsPawnAlive(Other))
		{
			bPartnerDown = true;
		}
		else if (!IsPawnInZone(Other))
		{
			bPartnerOutside = true;
		}
	}
	if (bPartnerDown)
	{
		return TEXT("PARTNER REBOOTING  //  EXIT LOCKED");
	}
	return bPartnerOutside ? TEXT("WAITING FOR PARTNER") : FString();
}

FString ACXLevelExit::GetDebugState() const
{
	return FString::Printf(TEXT("%s next=%s available=%d travelling=%d armed=%d offlineSerial=%d offlineAge=%.2f loc=%s"), *GetName(), *GetNextMapPackage(),
		IsNextMapAvailable() ? 1 : 0, bTravelling ? 1 : 0, bArmed ? 1 : 0, OfflineSerial, GetOfflineNoticeAge(), *GetActorLocation().ToCompactString());
}

// ---------------------------------------------------------------- 테스트용

ACXLevelExit* ACXLevelExit::DebugSpawnLevelExit(const UObject* WorldContextObject, const FString& NextMapPackage, float OffsetY)
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
	FVector Location = Anchor->GetActorLocation() + FVector(0.0f, OffsetY, -Anchor->GetSimpleCollisionHalfHeight());
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.bDeferConstruction = true;
	ACXLevelExit* Exit = World->SpawnActor<ACXLevelExit>(Location, FRotator::ZeroRotator, SpawnParams);
	if (Exit)
	{
		Exit->SetNextMapPackage(NextMapPackage);
		Exit->FinishSpawning(FTransform(FRotator::ZeroRotator, Location));
	}
	return Exit;
}
