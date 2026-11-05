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
#include "CoopInteractComponent.h"
#include "CXPlayerLifeComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

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
	DOREPLIFETIME(AHackTerminal, MemorySequence);
	DOREPLIFETIME(AHackTerminal, MemoryRound);
	DOREPLIFETIME(AHackTerminal, MemoryInputIndex);
	DOREPLIFETIME(AHackTerminal, bMemoryRelayActive);
	DOREPLIFETIME(AHackTerminal, MemoryPlaybackStart);
	DOREPLIFETIME(AHackTerminal, MemoryLastEvent);
	DOREPLIFETIME(AHackTerminal, MemoryEventTime);
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

	// 화살표 시퀀스는 적 해킹 전용으로 바뀌었다(해킹 시스템 기획 0-1장) — 예전 값으로 남은 단말기는 주파수 동조로 돌린다.
	// PuzzleType은 복제하지 않고 맵 데이터에서 읽으므로 모든 머신이 같은 결과.
	if (PuzzleType == EHackPuzzleType::ArrowSequence)
	{
		UE_LOG(LogTemp, Warning, TEXT("HackTerminal %s (%s): PuzzleType ArrowSequence is enemy-hack only now - running FrequencyLock instead. Set PuzzleType explicitly."),
			*TerminalName, *GetName());
		PuzzleType = EHackPuzzleType::FrequencyLock;
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

	// 메모리 패턴: 서버가 오빠의 중계 거리·생존을 매 프레임 판정(복제 → 오빠 HUD 재생 / 여동생 HUD 안내)
	if (HasAuthority() && IsMemoryPattern())
	{
		UpdateMemoryRelay();
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

// ---------------------------------------------------------------- 메모리 패턴(해킹 시스템 기획 11장)

float AHackTerminal::GetSyncedTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : World->GetTimeSeconds();
}

APawn* AHackTerminal::FindRelayPawn() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn && UCoopInteractComponent::GetPawnRole(Pawn) == ECoopCharacterRole::Brother)
		{
			return Pawn;
		}
	}
	return nullptr;
}

bool AHackTerminal::IsMemorySessionActive() const
{
	return IsMemoryPattern() && State == EHackTerminalState::InProgress && MemorySequence.Num() > 0;
}

void AHackTerminal::UpdateMemoryRelay()
{
	if (!IsMemorySessionActive())
	{
		bMemoryRelayActive = false;
		return;
	}

	// 오빠가 살아 있고(부활 대기·사망 아님) 단말기 중계 거리 안이어야 재생 — 아니면 패턴은 기다린다
	const APawn* Relay = FindRelayPawn();
	const bool bInRange = Relay && UCXPlayerLifeComponent::IsPawnAlive(Relay) && !Relay->IsHidden()
		&& FVector::Dist(Relay->GetActorLocation(), GetActorLocation()) <= MemoryRelayRange;
	if (bInRange == bMemoryRelayActive)
	{
		return;
	}
	bMemoryRelayActive = bInRange;
	if (bInRange)
	{
		// 새로 연결되면 처음부터(중간부터 보면 순서를 놓친다)
		RestartMemoryPlayback(MemoryLeadIn);
	}
	UE_LOG(LogTemp, Log, TEXT("HackTerminal %s: memory relay %s"), *TerminalName, bInRange ? TEXT("LINKED") : TEXT("LOST"));
}

void AHackTerminal::RestartMemoryPlayback(float Delay)
{
	MemoryPlaybackStart = GetSyncedTime() + FMath::Max(0.0f, Delay);
}

void AHackTerminal::SetMemoryEvent(EHackFeedback Event)
{
	MemoryLastEvent = Event;
	MemoryEventTime = GetSyncedTime();
}

void AHackTerminal::ExtendMemorySequence(int32 Length)
{
	Length = FMath::Clamp(Length, 1, 9);
	if (MemorySequence.Num() > Length)
	{
		MemorySequence.Reset();
	}
	while (MemorySequence.Num() < Length)
	{
		// 바로 앞과 같은 노드는 피한다(같은 칸이 두 번 연속 깜빡이면 오빠가 한 번으로 읽기 쉽다)
		int32 Node = FMath::RandRange(0, 8);
		if (MemorySequence.Num() > 0 && Node == MemorySequence.Last())
		{
			Node = (Node + FMath::RandRange(1, 8)) % 9;
		}
		MemorySequence.Add(static_cast<uint8>(Node));
	}
}

void AHackTerminal::ServerMemoryBegin(APawn* Hacker)
{
	if (!HasAuthority() || !IsMemoryPattern() || State == EHackTerminalState::Hacked)
	{
		return;
	}
	MemoryHacker = Hacker;
	MemoryRound = 0;
	MemoryInputIndex = 0;
	MemorySequence.Reset();
	ExtendMemorySequence(MemoryRoundLengths.IsValidIndex(0) ? MemoryRoundLengths[0] : 3);
	SetMemoryEvent(EHackFeedback::None);
	SetInProgress(true);
	// 중계 판정을 바로 한 번(오빠가 이미 옆이면 준비 시간 뒤 재생 시작)
	bMemoryRelayActive = false;
	UpdateMemoryRelay();
	UE_LOG(LogTemp, Log, TEXT("HackTerminal %s: memory session begin (%s)"), *TerminalName, *GetMemoryDebugState());
}

EHackFeedback AHackTerminal::ServerMemoryPress(APawn* Hacker, int32 Node)
{
	if (!HasAuthority() || !IsMemorySessionActive() || Node < 0 || Node > 8)
	{
		return EHackFeedback::None;
	}
	// 입력 검증: 세션을 연 여동생, 살아 있음, 단말기 가까이
	if (!Hacker || Hacker != MemoryHacker.Get() || !UCXPlayerLifeComponent::IsPawnAlive(Hacker)
		|| FVector::Dist(Hacker->GetActorLocation(), GetActorLocation()) > InteractRange + MemoryServerRangeSlack)
	{
		UE_LOG(LogTemp, Warning, TEXT("HackTerminal %s: rejected memory input from %s"), *TerminalName, *GetNameSafe(Hacker));
		return EHackFeedback::None;
	}
	if (!MemorySequence.IsValidIndex(MemoryInputIndex))
	{
		return EHackFeedback::None;
	}
	// 중계(오빠가 거리 안에 살아 있음) 없이는 입력을 받지 않는다 — 혼자 찍어서 맞히는 것 방지, "PARTNER RELAY REQUIRED"
	if (!bMemoryRelayActive)
	{
		return EHackFeedback::None;
	}

	if (MemorySequence[MemoryInputIndex] != Node)
	{
		// 오답: 이 라운드 처음부터, 같은 패턴을 오빠 화면에 다시 재생(짧은 텀 뒤)
		MemoryInputIndex = 0;
		SetMemoryEvent(EHackFeedback::Denied);
		RestartMemoryPlayback(MemoryDeniedReplayDelay);
		return EHackFeedback::Denied;
	}

	++MemoryInputIndex;
	if (MemoryInputIndex < MemorySequence.Num())
	{
		SetMemoryEvent(EHackFeedback::Correct);
		return EHackFeedback::Correct;
	}

	if (MemoryRound + 1 < GetMemoryRoundCount())
	{
		// 라운드 클리어: 같은 패턴 뒤에 노드를 덧붙여(사이먼식) 처음부터 재생
		++MemoryRound;
		MemoryInputIndex = 0;
		ExtendMemorySequence(MemoryRoundLengths[MemoryRound]);
		SetMemoryEvent(EHackFeedback::RoundClear);
		RestartMemoryPlayback(MemoryLeadIn);
		return EHackFeedback::RoundClear;
	}

	SetMemoryEvent(EHackFeedback::Granted);
	bMemoryRelayActive = false;
	MarkHacked();
	UE_LOG(LogTemp, Log, TEXT("HackTerminal %s: memory pattern GRANTED"), *TerminalName);
	return EHackFeedback::Granted;
}

void AHackTerminal::ServerMemoryEnd(APawn* Hacker)
{
	if (!HasAuthority() || !IsMemoryPattern() || State == EHackTerminalState::Hacked)
	{
		return;
	}
	if (MemoryHacker.IsValid() && Hacker && Hacker != MemoryHacker.Get())
	{
		return;
	}
	MemoryHacker.Reset();
	MemorySequence.Reset();
	MemoryRound = 0;
	MemoryInputIndex = 0;
	bMemoryRelayActive = false;
	SetMemoryEvent(EHackFeedback::None);
	SetInProgress(false);
}

int32 AHackTerminal::GetMemoryFlashNode(int32& OutStep, float& OutAlpha, float& OutWait) const
{
	OutStep = INDEX_NONE;
	OutAlpha = 0.0f;
	OutWait = 0.0f;
	const int32 Count = MemorySequence.Num();
	if (!IsMemorySessionActive() || !bMemoryRelayActive || Count == 0)
	{
		return INDEX_NONE;
	}

	const float On = FMath::Max(0.05f, MemoryFlashOnTime);
	const float Slot = On + FMath::Max(0.0f, MemoryFlashGapTime);
	const float Period = Slot * Count + FMath::Max(0.0f, MemoryLoopPause);
	const float T = GetSyncedTime() - MemoryPlaybackStart;
	if (T < 0.0f)
	{
		OutWait = -T;
		return INDEX_NONE;
	}

	const float InLoop = FMath::Fmod(T, Period);
	const int32 Step = FMath::FloorToInt(InLoop / Slot);
	if (Step >= Count)
	{
		// 반복 사이 쉼
		OutWait = Period - InLoop;
		return INDEX_NONE;
	}
	OutStep = Step;
	const float InSlot = InLoop - Step * Slot;
	if (InSlot >= On)
	{
		return INDEX_NONE;
	}
	// 켜질 때 바로 밝게, 끝에서 살짝 페이드
	OutAlpha = FMath::Clamp((On - InSlot) / (On * 0.25f), 0.0f, 1.0f);
	return MemorySequence[Step];
}

int32 AHackTerminal::GetMemoryExpectedNode() const
{
	return IsMemorySessionActive() && MemorySequence.IsValidIndex(MemoryInputIndex) ? MemorySequence[MemoryInputIndex] : INDEX_NONE;
}

FString AHackTerminal::GetMemoryDebugState() const
{
	static const TCHAR* EventNames[] = { TEXT("None"), TEXT("Correct"), TEXT("RoundClear"), TEXT("Denied"), TEXT("Granted"), TEXT("Failed") };
	static const TCHAR* StateNames[] = { TEXT("Locked"), TEXT("InProgress"), TEXT("Hacked") };
	FString Seq;
	for (const uint8 Node : MemorySequence)
	{
		Seq += FString::FromInt(Node + 1);
	}
	int32 Step = INDEX_NONE;
	float Alpha = 0.0f, Wait = 0.0f;
	const int32 Flash = GetMemoryFlashNode(Step, Alpha, Wait);
	const APawn* Relay = FindRelayPawn();
	return FString::Printf(TEXT("terminal=%s state=%s round=%d/%d seq=%s input=%d relay=%d brotherDist=%.0f/%.0f flash=%d step=%d wait=%.2f event=%s age=%.2f"),
		*TerminalName, StateNames[FMath::Clamp(static_cast<int32>(State), 0, 2)], MemoryRound + 1, GetMemoryRoundCount(), *Seq, MemoryInputIndex,
		bMemoryRelayActive ? 1 : 0, Relay ? FVector::Dist(Relay->GetActorLocation(), GetActorLocation()) : -1.0f, MemoryRelayRange,
		Flash == INDEX_NONE ? 0 : Flash + 1, Step, Wait, EventNames[FMath::Clamp(static_cast<int32>(MemoryLastEvent), 0, 5)], GetMemoryEventAge());
}
