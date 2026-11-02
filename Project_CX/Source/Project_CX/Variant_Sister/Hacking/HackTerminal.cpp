// Copyright Epic Games, Inc. All Rights Reserved.

#include "HackTerminal.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "CXSfx.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace HackTerminalColors
{
	const FLinearColor Locked(1.0f, 0.08f, 0.05f);
	const FLinearColor InProgress(1.0f, 0.75f, 0.1f);
	const FLinearColor Hacked(0.2f, 1.0f, 0.4f);
}

AHackTerminal::AHackTerminal()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetupAttachment(Root);
	Collision->SetBoxExtent(FVector(25.0f, 45.0f, 75.0f));
	Collision->SetRelativeLocation(FVector(0.0f, 0.0f, 75.0f));
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	// 스프링암이 단말기에 걸려 카메라가 당겨지지 않도록(맵 COL 박스와 같은 규칙)
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Collision->SetCanEverAffectNavigation(true);

	StatusLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StatusLight"));
	StatusLight->SetupAttachment(Root);
	StatusLight->SetRelativeLocation(FVector(-40.0f, 0.0f, 120.0f));
	StatusLight->SetIntensityUnits(ELightUnits::Candelas);
	StatusLight->SetIntensity(30.0f);
	StatusLight->SetAttenuationRadius(450.0f);
	StatusLight->SetCastShadows(false);
	StatusLight->SetLightColor(HackTerminalColors::Locked);

	HumAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("HumAudio"));
	HumAudio->SetupAttachment(Root);
	HumAudio->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	// 에디터 뷰포트에서는 울리지 않게 — BeginPlay에서 상태를 보고 재생
	HumAudio->bAutoActivate = false;
	HumAudio->SetSound(CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/World/SW_Terminal_Hum.SW_Terminal_Hum")));

	GateOffSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/World/SW_Gate_Off.SW_Gate_Off"));
}

void AHackTerminal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHackTerminal, State);
	DOREPLIFETIME(AHackTerminal, Passcode);
}

void AHackTerminal::BeginPlay()
{
	Super::BeginPlay();

	// 에디터에서 메시를 나중에 지정한 경우에도 충돌 박스가 메시 크기를 따르도록 런타임에 한 번 더 맞춘다
	if (Mesh->GetStaticMesh())
	{
		const FBox LocalBox = Mesh->CalcBounds(Mesh->GetRelativeTransform()).GetBox();
		Collision->SetRelativeLocation(LocalBox.GetCenter());
		Collision->SetBoxExtent(LocalBox.GetExtent());
	}

	if (HasAuthority() && PuzzleType == EHackPuzzleType::Passcode && bRandomizePasscode)
	{
		Passcode = FString::Printf(TEXT("%04d"), FMath::RandRange(0, 9999));
	}
	ApplyStateVisuals();

	// 대기음: 이미 해킹된 단말기는 조용히. 여러 대가 같은 위상으로 겹치지 않게 시작 위치를 흩뜨린다.
	if (HumAudio && HumAudio->Sound && State != EHackTerminalState::Hacked)
	{
		HumAudio->SetPitchMultiplier(State == EHackTerminalState::InProgress ? HumPitchInProgress : 1.0f);
		HumAudio->Play(FMath::FRandRange(0.0f, FMath::Max(0.0f, HumAudio->Sound->GetDuration() - 0.1f)));
	}
}

void AHackTerminal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 해킹 중에는 상태등을 깜빡여 멀리서도 진행 중임을 알 수 있게 한다
	if (State == EHackTerminalState::InProgress)
	{
		const float Blink = 0.5f + 0.5f * FMath::Sin(GetWorld()->GetTimeSeconds() * 12.0f);
		StatusLight->SetIntensity(10.0f + 40.0f * Blink);
	}
}

void AHackTerminal::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!Mesh->GetStaticMesh())
	{
		return;
	}

	// 메시의 액터 기준 바운드(메시 상대 위치/회전/스케일 반영)에 박스를 맞춘다
	const FBox LocalBox = Mesh->CalcBounds(Mesh->GetRelativeTransform()).GetBox();
	Collision->SetRelativeLocation(LocalBox.GetCenter());
	Collision->SetBoxExtent(LocalBox.GetExtent());
}

int32 AHackTerminal::GetPasscodeDigit(int32 Index) const
{
	if (!Passcode.IsValidIndex(Index))
	{
		return 0;
	}
	const TCHAR C = Passcode[Index];
	return FChar::IsDigit(C) ? (C - TEXT('0')) : 0;
}

TArray<EHackArrow> AHackTerminal::MakeArrowRound(int32 Length) const
{
	TArray<EHackArrow> Round;
	Length = FMath::Clamp(Length, 1, 7);
	for (int32 i = 0; i < Length; ++i)
	{
		Round.Add(static_cast<EHackArrow>(FMath::RandRange(0, 3)));
	}
	return Round;
}

void AHackTerminal::SetInProgress(bool bInProgress)
{
	if (State == EHackTerminalState::Hacked)
	{
		return;
	}
	State = bInProgress ? EHackTerminalState::InProgress : EHackTerminalState::Locked;
	ApplyStateVisuals();
}

void AHackTerminal::MarkHacked()
{
	if (State == EHackTerminalState::Hacked)
	{
		return;
	}
	State = EHackTerminalState::Hacked;
	ApplyStateVisuals();
	OnHacked.Broadcast(this);
}

void AHackTerminal::OnRep_State()
{
	ApplyStateVisuals();
}

void AHackTerminal::ApplyStateVisuals()
{
	switch (State)
	{
	case EHackTerminalState::Locked:
		StatusLight->SetLightColor(HackTerminalColors::Locked);
		StatusLight->SetIntensity(30.0f);
		break;
	case EHackTerminalState::InProgress:
		StatusLight->SetLightColor(HackTerminalColors::InProgress);
		break;
	case EHackTerminalState::Hacked:
		StatusLight->SetLightColor(HackTerminalColors::Hacked);
		StatusLight->SetIntensity(40.0f);
		// 연결된 장애물 해제(복제되는 State로 모든 머신에서 같은 결과)
		for (AActor* Target : UnlockActors)
		{
			if (IsValid(Target))
			{
				Target->SetActorHiddenInGame(true);
				Target->SetActorEnableCollision(false);
			}
		}
		break;
	}

	UpdateStateSounds();
}

void AHackTerminal::UpdateStateSounds()
{
	const EHackTerminalState Previous = AppliedState;
	const bool bWasApplied = bStateApplied;
	AppliedState = State;
	// BeginPlay 이전(초기 복제)에 들어온 상태는 "시작 상태"로만 기록. BeginPlay 안의 첫 적용도 시작 상태.
	const bool bPlaying = HasActorBegunPlay() || IsActorBeginningPlay();
	bStateApplied = bPlaying;
	if (!bWasApplied || !bPlaying)
	{
		return;
	}

	if (HumAudio)
	{
		if (State == EHackTerminalState::Hacked)
		{
			if (HumAudio->IsPlaying())
			{
				HumAudio->FadeOut(0.8f, 0.0f);
			}
		}
		else
		{
			HumAudio->SetPitchMultiplier(State == EHackTerminalState::InProgress ? HumPitchInProgress : 1.0f);
		}
	}

	// 게이트 꺼짐: 실제 해킹 진행(InProgress)에서 Hacked로 넘어온 경우만 — 접속 시 이미 Hacked로 받은 경우(Locked → Hacked)는 제외
	if (State == EHackTerminalState::Hacked && Previous == EHackTerminalState::InProgress && GateOffSound)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const AActor* Target : UnlockActors)
		{
			if (IsValid(Target))
			{
				Sum += Target->GetActorLocation();
				++Count;
			}
		}
		// 게이트가 여러 조각이어도 한 번만(겹쳐서 커지지 않게), 연결된 게 없으면 단말기 위치
		const FVector Location = Count > 0 ? Sum / Count : GetActorLocation();
		UGameplayStatics::PlaySoundAtLocation(this, GateOffSound, Location);
	}
}
