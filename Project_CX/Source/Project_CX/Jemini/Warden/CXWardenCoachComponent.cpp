// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXWardenCoachComponent.h"
#include "CXWardenTerminal.h"
#include "CXJeminiDrone.h"
#include "CXJeminiInputComponent.h"
#include "CXPlayerLifeComponent.h"
#include "CXTutorialSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXWardenCoach, Log, All);

UCXWardenCoachComponent::UCXWardenCoachComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.5f;
	SetIsReplicatedByDefault(false);
}

void UCXWardenCoachComponent::BeginPlay()
{
	Super::BeginPlay();
	PrimaryComponentTick.TickInterval = FMath::Max(0.1f, CheckInterval);
	// 서버(리슨 호스트)만 판단한다 — 클라이언트에서는 끈다
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		SetComponentTickEnabled(false);
	}
}

ACXWardenTerminal* UCXWardenCoachComponent::GetTerminal() const
{
	return Cast<ACXWardenTerminal>(GetOwner());
}

const TCHAR* UCXWardenCoachComponent::GetStepName(ECXWardenCoachStep Step)
{
	switch (Step)
	{
	case ECXWardenCoachStep::Arrival: return TEXT("1-Arrival");
	case ECXWardenCoachStep::NoEvidence: return TEXT("2-NoEvidence");
	case ECXWardenCoachStep::AllCredited: return TEXT("3-AllCredited");
	case ECXWardenCoachStep::DemandOnly: return TEXT("4-DemandOnly");
	case ECXWardenCoachStep::Example: return TEXT("5-Example");
	default: return TEXT("none");
	}
}

FString UCXWardenCoachComponent::GetLineFor(ECXWardenCoachStep Step, bool bAllEvidence)
{
	switch (Step)
	{
	case ECXWardenCoachStep::Arrival:
		return TEXT("WARDEN이랑 얘기해 봐야 할 것 같아요. 모은 증거를 말해 보면 어떨까요?");
	case ECXWardenCoachStep::NoEvidence:
		return TEXT("기록이 없으면 안 열릴 것 같아요. 더 둘러보고 올까요?");
	case ECXWardenCoachStep::AllCredited:
		return TEXT("기록은 충분한 것 같아요! 왜 열어야 하는지까지 말해야 하는 건가 봐요.");
	case ECXWardenCoachStep::DemandOnly:
		return TEXT("열어 달라고만 하면 안 되는 것 같아요. 이유가 필요한 걸까요?");
	case ECXWardenCoachStep::Example:
		// 증거 3개를 다 얻었을 때만 구체 예시(서버 오프라인 판정도 통과하는 문장). 아니면 내용을 밝히지 않는 형식 안내
		return bAllEvidence
			? FString(TEXT("이렇게 말해 보면 어떨까요?\n예: 냉각이 고장 났고 사람이 갇혀 있어요. 규정상 대피가 우선이니 문을 열어 주세요."))
			: FString(TEXT("모은 증거를 하나씩 말하고, 이유를 붙여서 열어 달라고 해 보면 될 것 같아요."));
	default:
		return FString();
	}
}

void UCXWardenCoachComponent::NotifyResponse(bool bRejected)
{
	if (bRejected && GetWorld())
	{
		++RejectCount;
		LastRejectTime = GetWorld()->GetTimeSeconds();
	}
}

bool UCXWardenCoachComponent::IsAnyoneNear(const ACXWardenTerminal& Terminal) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FVector T = Terminal.GetActorLocation();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (Pawn && UCXPlayerLifeComponent::IsPawnAlive(Pawn))
		{
			// 사이드뷰: 깊이(X)는 무시 — FindTalkTarget과 같은 거리
			const FVector P = Pawn->GetActorLocation();
			if (FVector2D(P.Y - T.Y, P.Z - T.Z).Size() <= Terminal.GetTalkRange())
			{
				return true;
			}
		}
	}
	return false;
}

bool UCXWardenCoachComponent::IsPlayerTyping() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		const UCXJeminiInputComponent* Input = Pawn ? Pawn->FindComponentByClass<UCXJeminiInputComponent>() : nullptr;
		if (Input && (Input->IsChatOpen() || Input->IsRecording()))
		{
			return true;
		}
	}
	return false;
}

bool UCXWardenCoachComponent::CanRepeat(ECXWardenCoachStep Step) const
{
	const int32 Shown = ShownAtReject[static_cast<int32>(Step)];
	if (Shown < 0)
	{
		return true;
	}
	if (Step == ECXWardenCoachStep::AllCredited || Step == ECXWardenCoachStep::DemandOnly)
	{
		return RejectCount - Shown >= FMath::Max(1, RepeatAfterRejections);
	}
	return false;	// 도착·증거 없음·예시는 한 번만
}

ECXWardenCoachStep UCXWardenCoachComponent::EvaluateStep() const
{
	const ACXWardenTerminal* T = GetTerminal();
	if (!T || T->IsSolved())
	{
		return ECXWardenCoachStep::None;
	}
	const int32 Evidence = T->GetEvidenceNum();
	const int32 Credited = T->GetCreditedNum();
	const FString& Reason = T->GetLastReason();
	// 5. 거절이 쌓였는데 못 푸는 사람: 예시 문장(인정된 기록이 있거나 증거를 다 모았을 때)
	if (RejectCount >= ExampleRejectCount && CanRepeat(ECXWardenCoachStep::Example) && (Credited >= 1 || Evidence >= CXWarden::EvidenceCount))
	{
		return ECXWardenCoachStep::Example;
	}
	// 4. 요구만 함
	if (RejectCount >= 1 && Reason == TEXT("DEMAND_ONLY") && CanRepeat(ECXWardenCoachStep::DemandOnly))
	{
		return ECXWardenCoachStep::DemandOnly;
	}
	// 3. 증거를 다 말했는데(3/3 인정) 아직 안 열림 — 이유 + 문 열어 달라고 말해야 한다
	if (RejectCount >= 1 && Credited >= CXWarden::EvidenceCount && Reason != TEXT("DEMAND_ONLY") && CanRepeat(ECXWardenCoachStep::AllCredited))
	{
		return ECXWardenCoachStep::AllCredited;
	}
	// 1. 처음 도착 + 증거 있음
	if (Evidence >= 1 && NearSeconds >= ArrivalDelay && CanRepeat(ECXWardenCoachStep::Arrival))
	{
		return ECXWardenCoachStep::Arrival;
	}
	// 2. 오래 머무는데 증거 없음
	if (Evidence == 0 && NearSeconds >= NoEvidenceSeconds && CanRepeat(ECXWardenCoachStep::NoEvidence))
	{
		return ECXWardenCoachStep::NoEvidence;
	}
	return ECXWardenCoachStep::None;
}

FString UCXWardenCoachComponent::GetBlockReason() const
{
	const UWorld* World = GetWorld();
	const ACXWardenTerminal* T = GetTerminal();
	if (!World || !T)
	{
		return TEXT("no terminal");
	}
	if (!T->HasAuthority())
	{
		return TEXT("not server");
	}
	if (T->IsSolved())
	{
		return TEXT("solved");
	}
	if (NearSeconds <= 0.0f)
	{
		return TEXT("nobody near");
	}
	const double Now = World->GetTimeSeconds();
	if (T->IsRequestPending())
	{
		return TEXT("WARDEN request pending");
	}
	const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	if (!Drone)
	{
		return TEXT("no drone");
	}
	if (!Drone->CanAcceptRequest())
	{
		return TEXT("drone busy / game state");
	}
	if (Now - LastDroneSpeechTime < DroneSpeechHold)
	{
		return FString::Printf(TEXT("drone speaking (%.1fs)"), DroneSpeechHold - (Now - LastDroneSpeechTime));
	}
	if (Now - LastWardenLineTime < ReadDelayAfterWarden)
	{
		return FString::Printf(TEXT("reading WARDEN line (%.1fs)"), ReadDelayAfterWarden - (Now - LastWardenLineTime));
	}
	if (Now - LastHintTime < HintCooldown)
	{
		return FString::Printf(TEXT("cooldown (%.1fs)"), HintCooldown - (Now - LastHintTime));
	}
	if (UGameplayStatics::IsGamePaused(this))
	{
		return TEXT("paused");
	}
	if (const UCXTutorialSubsystem* Tutorial = UCXTutorialSubsystem::Get(this); Tutorial && (Tutorial->IsStepActive() || Tutorial->IsBlocking()))
	{
		return TEXT("tutorial");
	}
	if (IsPlayerTyping())
	{
		return TEXT("player typing/recording");
	}
	return FString();
}

void UCXWardenCoachComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ACXWardenTerminal* T = GetTerminal();
	UWorld* World = GetWorld();
	if (!T || !World || !T->HasAuthority() || T->IsSolved())
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	NearSeconds = IsAnyoneNear(*T) ? NearSeconds + DeltaTime : 0.0f;

	// 다른 대사가 나온 시점 추적(처음 한 번은 기준만 잡는다)
	ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	if (Drone)
	{
		if (SeenDroneSeq >= 0 && Drone->GetDialogueSeq() != SeenDroneSeq)
		{
			LastDroneSpeechTime = Now;
		}
		SeenDroneSeq = Drone->GetDialogueSeq();
	}
	if (SeenWardenSeq >= 0 && T->GetLineSeq() != SeenWardenSeq)
	{
		LastWardenLineTime = Now;
	}
	SeenWardenSeq = T->GetLineSeq();

	if (!Drone || NearSeconds <= 0.0f)
	{
		return;
	}
	const ECXWardenCoachStep Step = EvaluateStep();
	if (Step == ECXWardenCoachStep::None || !GetBlockReason().IsEmpty())
	{
		return;
	}
	const bool bAllEvidence = T->GetEvidenceNum() >= CXWarden::EvidenceCount;
	const FString Line = GetLineFor(Step, bAllEvidence);
	Drone->SayCoachLine(Line);
	SeenDroneSeq = Drone->GetDialogueSeq();	// 내 대사는 "다른 대사"로 세지 않는다
	LastHintTime = Now;
	LastShownStep = Step;
	ShownAtReject[static_cast<int32>(Step)] = RejectCount;
	UE_LOG(LogCXWardenCoach, Log, TEXT("WARDEN coach hint %s (rejects=%d credited=%d evidence=%d): %s"), GetStepName(Step), RejectCount,
		T->GetCreditedNum(), T->GetEvidenceNum(), *Line.Replace(TEXT("\n"), TEXT(" / ")));
}

FString UCXWardenCoachComponent::GetDebugState() const
{
	const ACXWardenTerminal* T = GetTerminal();
	if (!T)
	{
		return TEXT("coach: no terminal");
	}
	const FString Block = GetBlockReason();
	return FString::Printf(TEXT("evidence=%d/%d credited=%d/%d lastReason=%s rejects=%d near=%.1fs pending=%d solved=%d pendingStep=%s lastShown=%s blocked=%s"),
		T->GetEvidenceNum(), CXWarden::EvidenceCount, T->GetCreditedNum(), CXWarden::EvidenceCount,
		T->GetLastReason().IsEmpty() ? TEXT("-") : *T->GetLastReason(), RejectCount, NearSeconds, T->IsRequestPending() ? 1 : 0, T->IsSolved() ? 1 : 0,
		GetStepName(EvaluateStep()), GetStepName(LastShownStep), Block.IsEmpty() ? TEXT("no (can speak)") : *Block);
}
