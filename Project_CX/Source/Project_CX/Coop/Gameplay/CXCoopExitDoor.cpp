// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXCoopExitDoor.h"
#include "CoopInteractComponent.h"
#include "LocalCoopGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace CXExitDoor
{
	// 문 치수(cm)
	constexpr float DoorWidth = 240.0f;
	constexpr float DoorHeight = 300.0f;
	/** 버튼 중심 가로 위치(±). 문틀(±160) 바깥. */
	constexpr float ButtonY = 240.0f;
	constexpr float ButtonZ = 130.0f;
	/** 문 위 표시등 높이(상인방 300~340 위). 사이드뷰 카메라 화면 안에 들어오는 높이. */
	constexpr float LampZ = 362.0f;

	// 색(CXMenuStyle/CoopHUD와 같은 청록·마젠타)
	const FLinearColor Brother(0.0f, 0.85f, 1.0f, 1.0f);
	const FLinearColor Sister(1.0f, 0.22f, 0.62f, 1.0f);
	const FLinearColor LightOff(0.22f, 0.22f, 0.25f, 1.0f);
	const FLinearColor Steel(0.045f, 0.05f, 0.065f, 1.0f);
	const FLinearColor SteelLight(0.11f, 0.115f, 0.13f, 1.0f);

	constexpr float OnIntensity = 20.0f;
	constexpr float OffIntensity = 0.3f;
	constexpr float RimIntensity = 4.0f;
}

ACXCoopExitDoor::ACXCoopExitDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;	// 열림 연출 중에만 틱
	PrimaryActorTick.bTickEvenWhenPaused = false;

	// 서버 권위 버튼 상태를 클라이언트로(listen server)
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> NeonFinder(TEXT("/Game/Project_CX/Environment/Materials/M_NeonEmissive.M_NeonEmissive"));
	CubeMesh = CubeFinder.Object;
	BodyMaterial = BodyFinder.Object;
	NeonMaterial = NeonFinder.Object;

	using namespace CXExitDoor;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	// 문 뒤 통로(검정) — 셔터가 올라가면 드러난다
	PassageBack = MakeCube(TEXT("PassageBack"), Root, FVector(40.0f, 0.0f, DoorHeight * 0.5f), FVector(10.0f, DoorWidth, DoorHeight), false);

	// 셔터: 위쪽 피벗 아래로 매달고, 열릴 때 피벗의 세로 스케일을 줄인다(말려 올라감)
	ShutterPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ShutterPivot"));
	ShutterPivot->SetupAttachment(Root);
	ShutterPivot->SetRelativeLocation(FVector(0.0f, 0.0f, DoorHeight));

	DoorPanel = MakeCube(TEXT("DoorPanel"), ShutterPivot, FVector(0.0f, 0.0f, -DoorHeight * 0.5f), FVector(20.0f, DoorWidth, DoorHeight));
	DoorPanel->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	DoorPanel->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	for (int32 i = 1; i <= 7; ++i)
	{
		// 셔터 가로 슬랫 라인(약한 네온) — 사이드뷰에서 "셔터"로 읽히게
		const FName Name(*FString::Printf(TEXT("Slat%d"), i));
		ShutterSlats.Add(MakeCube(Name, ShutterPivot, FVector(-12.0f, 0.0f, -DoorHeight * i / 8.0f), FVector(4.0f, DoorWidth - 16.0f, 3.0f), false));
	}

	// 문틀(기둥 2 + 상인방), 표시등 하우징
	FramePieces.Add(MakeCube(TEXT("PillarLeft"), Root, FVector(0.0f, -(DoorWidth * 0.5f + 20.0f), (DoorHeight + 40.0f) * 0.5f), FVector(60.0f, 40.0f, DoorHeight + 40.0f)));
	FramePieces.Add(MakeCube(TEXT("PillarRight"), Root, FVector(0.0f, DoorWidth * 0.5f + 20.0f, (DoorHeight + 40.0f) * 0.5f), FVector(60.0f, 40.0f, DoorHeight + 40.0f)));
	FramePieces.Add(MakeCube(TEXT("Lintel"), Root, FVector(0.0f, 0.0f, DoorHeight + 20.0f), FVector(60.0f, DoorWidth + 80.0f, 40.0f)));
	FramePieces.Add(MakeCube(TEXT("LampHousing"), Root, FVector(-10.0f, 0.0f, LampZ), FVector(24.0f, 150.0f, 44.0f)));

	// 문틀 안쪽 네온 — 왼쪽 청록 / 오른쪽 마젠타(두 사람의 문)
	FrameNeonLeft = MakeCube(TEXT("FrameNeonLeft"), Root, FVector(-32.0f, -(DoorWidth * 0.5f + 2.0f), DoorHeight * 0.5f), FVector(4.0f, 4.0f, DoorHeight), false);
	FrameNeonRight = MakeCube(TEXT("FrameNeonRight"), Root, FVector(-32.0f, DoorWidth * 0.5f + 2.0f, DoorHeight * 0.5f), FVector(4.0f, 4.0f, DoorHeight), false);
	FrameNeonTop = MakeCube(TEXT("FrameNeonTop"), Root, FVector(-32.0f, 0.0f, DoorHeight + 2.0f), FVector(4.0f, DoorWidth + 8.0f, 4.0f), false);

	// 문 위 표시등 2칸
	BrotherLamp = MakeCube(TEXT("BrotherLamp"), Root, FVector(-24.0f, -36.0f, LampZ), FVector(6.0f, 54.0f, 24.0f), false);
	SisterLamp = MakeCube(TEXT("SisterLamp"), Root, FVector(-24.0f, 36.0f, LampZ), FVector(6.0f, 54.0f, 24.0f), false);

	ExitLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ExitLabel"));
	ExitLabel->SetupAttachment(Root);
	ExitLabel->SetRelativeLocation(FVector(-31.0f, 0.0f, DoorHeight + 20.0f));	// 상인방 앞면
	ExitLabel->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));	// 카메라(-X 쪽)를 향하게
	ExitLabel->SetHorizontalAlignment(EHTA_Center);
	ExitLabel->SetVerticalAlignment(EVRTA_TextCenter);
	ExitLabel->SetWorldSize(30.0f);
	ExitLabel->SetText(FText::FromString(TEXT("EXIT")));
	ExitLabel->SetTextRenderColor(FColor(230, 235, 240));
	ExitLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 버튼 2개(왼쪽 오빠 / 오른쪽 여동생): 기둥 + 판 + 버튼 등 + 담당 색 띠
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const float Y = (Side == 0 ? -1.0f : 1.0f) * ButtonY;
		const TCHAR* Who = Side == 0 ? TEXT("Brother") : TEXT("Sister");
		FramePieces.Add(MakeCube(*FString::Printf(TEXT("%sButtonPost"), Who), Root, FVector(-10.0f, Y, 50.0f), FVector(30.0f, 44.0f, 100.0f)));
		FramePieces.Add(MakeCube(*FString::Printf(TEXT("%sButtonPlate"), Who), Root, FVector(-29.0f, Y, ButtonZ), FVector(8.0f, 64.0f, 64.0f)));
		UStaticMeshComponent* Light = MakeCube(*FString::Printf(TEXT("%sButton"), Who), Root, FVector(-35.0f, Y, ButtonZ), FVector(6.0f, 38.0f, 38.0f), false);
		UStaticMeshComponent* Rim = MakeCube(*FString::Printf(TEXT("%sRim"), Who), Root, FVector(-29.0f, Y, ButtonZ - 40.0f), FVector(4.0f, 66.0f, 6.0f), false);
		(Side == 0 ? BrotherButton : SisterButton) = Light;
		(Side == 0 ? BrotherRim : SisterRim) = Rim;
	}
}

UStaticMeshComponent* ACXCoopExitDoor::MakeCube(FName Name, USceneComponent* Parent, const FVector& Location, const FVector& Size, bool bShadow)
{
	UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Mesh->SetupAttachment(Parent);
	Mesh->SetStaticMesh(CubeMesh);
	Mesh->SetRelativeLocation(Location);
	Mesh->SetRelativeScale3D(Size / 100.0f);	// 기본 큐브 = 100cm
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCastShadow(bShadow);
	Mesh->SetCanEverAffectNavigation(false);
	return Mesh;
}

void ACXCoopExitDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACXCoopExitDoor, bBrotherPressed);
	DOREPLIFETIME(ACXCoopExitDoor, bSisterPressed);
	DOREPLIFETIME(ACXCoopExitDoor, bOpening);
}

// ---------------------------------------------------------------- 머티리얼

void ACXCoopExitDoor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyMaterials();
}

static UMaterialInstanceDynamic* GetOrCreateMID(UStaticMeshComponent* Mesh, UMaterialInterface* Parent)
{
	if (!Mesh || !Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (!MID || MID->Parent != Parent)
	{
		MID = UMaterialInstanceDynamic::Create(Parent, Mesh);
		Mesh->SetMaterial(0, MID);
	}
	return MID;
}

void ACXCoopExitDoor::SetNeon(UStaticMeshComponent* Mesh, const FLinearColor& Color, float Intensity)
{
	if (UMaterialInstanceDynamic* MID = GetOrCreateMID(Mesh, NeonMaterial))
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	}
}

void ACXCoopExitDoor::SetBody(UStaticMeshComponent* Mesh, const FLinearColor& Color)
{
	if (UMaterialInstanceDynamic* MID = GetOrCreateMID(Mesh, BodyMaterial))
	{
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void ACXCoopExitDoor::ApplyMaterials()
{
	using namespace CXExitDoor;
	SetBody(PassageBack, FLinearColor::Black);
	SetBody(DoorPanel, Steel);
	for (UStaticMeshComponent* Piece : FramePieces)
	{
		SetBody(Piece, Piece->GetName().Contains(TEXT("Plate")) ? SteelLight : Steel);
	}
	for (UStaticMeshComponent* Slat : ShutterSlats)
	{
		SetNeon(Slat, FLinearColor(0.55f, 0.62f, 0.72f, 1.0f), 1.5f);
	}
	SetNeon(FrameNeonLeft, Brother, 6.0f);
	SetNeon(FrameNeonRight, Sister, 6.0f);
	SetNeon(FrameNeonTop, FLinearColor(0.8f, 0.85f, 0.9f, 1.0f), 3.0f);
	SetNeon(BrotherRim, Brother, RimIntensity);
	SetNeon(SisterRim, Sister, RimIntensity);
	RefreshLights();
	ApplyOpenAlpha();
}

void ACXCoopExitDoor::RefreshLights()
{
	using namespace CXExitDoor;
	// 눌린 버튼: 버튼 등 + 같은 쪽 표시등이 자기 색으로
	SetNeon(BrotherButton, bBrotherPressed ? Brother : LightOff, bBrotherPressed ? OnIntensity : OffIntensity);
	SetNeon(BrotherLamp, bBrotherPressed ? Brother : LightOff, bBrotherPressed ? OnIntensity : OffIntensity);
	SetNeon(SisterButton, bSisterPressed ? Sister : LightOff, bSisterPressed ? OnIntensity : OffIntensity);
	SetNeon(SisterLamp, bSisterPressed ? Sister : LightOff, bSisterPressed ? OnIntensity : OffIntensity);
}

// ---------------------------------------------------------------- 상호작용

bool ACXCoopExitDoor::IsButtonPressed(ECoopCharacterRole InRole) const
{
	return InRole == ECoopCharacterRole::Brother ? bBrotherPressed : (InRole == ECoopCharacterRole::Sister ? bSisterPressed : false);
}

FVector ACXCoopExitDoor::GetButtonLocation(ECoopCharacterRole InRole) const
{
	const UStaticMeshComponent* Button = InRole == ECoopCharacterRole::Sister ? SisterButton : BrotherButton;
	return Button ? Button->GetComponentLocation() : GetActorLocation();
}

FVector ACXCoopExitDoor::GetInteractLocation(const APawn* Pawn) const
{
	return GetButtonLocation(UCoopInteractComponent::GetPawnRole(Pawn));
}

bool ACXCoopExitDoor::CanInteract(const APawn* Pawn) const
{
	// 자기 버튼만(상대 버튼 앞에서는 안내도 뜨지 않음), 이미 켰으면/열리는 중이면 불가, 살아 있는 폰만
	const ECoopCharacterRole PawnRole = UCoopInteractComponent::GetPawnRole(Pawn);
	if (PawnRole == ECoopCharacterRole::None || bOpening || IsButtonPressed(PawnRole) || !UCoopInteractComponent::IsPawnAbleToInteract(Pawn))
	{
		return false;
	}
	// 버튼 앞 상자 범위(로컬 좌표) — 사이드뷰에서 "버튼 앞에 서 있음"
	const FTransform& T = GetActorTransform();
	const FVector Local = T.InverseTransformPosition(Pawn->GetActorLocation());
	const FVector ButtonLocal = T.InverseTransformPosition(GetButtonLocation(PawnRole));
	return FMath::Abs(Local.Y - ButtonLocal.Y) <= ButtonReachHalfWidth
		&& Local.X <= ButtonLocal.X + 60.0f && Local.X >= ButtonLocal.X - ButtonReachDepth
		&& FMath::Abs(Local.Z - ButtonLocal.Z) <= ButtonReachHeight;
}

void ACXCoopExitDoor::Interact(APawn* Pawn)
{
	if (HasAuthority() && CanInteract(Pawn))
	{
		PressButton(UCoopInteractComponent::GetPawnRole(Pawn));
	}
}

void ACXCoopExitDoor::PressButton(ECoopCharacterRole InRole)
{
	// 서버에서만. 눌린 버튼은 꺼지지 않는다(사망해도 유지 — 6장)
	if (InRole == ECoopCharacterRole::Brother)
	{
		bBrotherPressed = true;
	}
	else if (InRole == ECoopCharacterRole::Sister)
	{
		bSisterPressed = true;
	}
	OnRep_Buttons();

	if (bBrotherPressed && bSisterPressed && !bOpening)
	{
		bOpening = true;
		OnRep_Opening();
	}
}

void ACXCoopExitDoor::OnRep_Buttons()
{
	RefreshLights();
	if (ButtonSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ButtonSound, GetActorLocation());
	}
}

void ACXCoopExitDoor::OnRep_Opening()
{
	if (bOpening)
	{
		StartOpening();
	}
}

// ---------------------------------------------------------------- 열림 연출

void ACXCoopExitDoor::StartOpening()
{
	OpenAlpha = 0.0f;
	SetActorTickEnabled(true);
	if (OpenSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, OpenSound, GetActorLocation());
	}
}

void ACXCoopExitDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bOpening)
	{
		SetActorTickEnabled(false);
		return;
	}
	OpenAlpha = FMath::Min(1.0f, OpenAlpha + DeltaSeconds / FMath::Max(0.01f, OpenDuration));
	ApplyOpenAlpha();
	if (OpenAlpha >= 1.0f)
	{
		SetActorTickEnabled(false);
		FinishOpening();
	}
}

void ACXCoopExitDoor::ApplyOpenAlpha()
{
	if (!ShutterPivot)
	{
		return;
	}
	// 셔터가 위로 말려 올라감(천천히 시작 → 끝에서 감속)
	const float Eased = FMath::InterpEaseInOut(0.0f, 1.0f, OpenAlpha, 2.0f);
	ShutterPivot->SetRelativeScale3D(FVector(1.0f, 1.0f, FMath::Max(0.001f, 1.0f - Eased)));
	ShutterPivot->SetVisibility(OpenAlpha < 1.0f, true);
}

void ACXCoopExitDoor::FinishOpening()
{
	// 다 열리면 문 충돌을 끈다
	DoorPanel->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (HasAuthority() && !bClearNotified)
	{
		bClearNotified = true;
		if (ALocalCoopGameMode* GameMode = GetWorld()->GetAuthGameMode<ALocalCoopGameMode>())
		{
			GameMode->NotifyGameCleared();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("CXCoopExitDoor: 게임모드가 ALocalCoopGameMode가 아니라 클리어 판정을 보낼 곳이 없습니다."));
		}
	}
}

// ---------------------------------------------------------------- 테스트용

ACXCoopExitDoor* ACXCoopExitDoor::DebugSpawnExitDoor(const UObject* WorldContextObject, float OffsetY, float OffsetX)
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

	// 폰 발밑 높이에서 아래로 바닥을 찾아 그 위에 세운다(폰은 무시, 위에 놓인 소품 윗면은 건너뜀)
	FVector Location = Anchor->GetActorLocation() + FVector(OffsetX, OffsetY, -Anchor->GetSimpleCollisionHalfHeight());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXExitDoorSpawn), false);
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* Each = It->Get())
		{
			Params.AddIgnoredActor(Each->GetPawn());
		}
	}
	FHitResult Hit;
	// 시작점이 소품 안이면(bStartPenetrating) 폰 발밑 높이를 그대로 쓴다
	if (World->LineTraceSingleByChannel(Hit, Location + FVector(0.0f, 0.0f, 20.0f), Location - FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params) && !Hit.bStartPenetrating)
	{
		Location.Z = Hit.ImpactPoint.Z;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ACXCoopExitDoor>(Location, FRotator::ZeroRotator, SpawnParams);
}
