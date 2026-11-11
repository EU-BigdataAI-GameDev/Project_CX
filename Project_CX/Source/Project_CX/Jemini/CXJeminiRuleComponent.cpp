// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXJeminiRuleComponent.h"
#include "CXJeminiDrone.h"
#include "CXJeminiClient.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXJeminiRule, Log, All);

namespace CXJeminiRuleLines
{
	const TCHAR* Limit = TEXT("규칙은 2개까지만 기억할 수 있어요. 하나 취소할까요?");
	const TCHAR* Invalid = TEXT("규칙을 잘 못 알아들었어요. 다시 말해 줄래요?");
	const TCHAR* NoRule = TEXT("지울 규칙이 없어요.");
	const TCHAR* HeldBattery = TEXT("배터리가 모자라서 규칙은 잠시 보류할게요.");
	const TCHAR* HeldOther = TEXT("지금은 규칙을 지킬 수 없어서 잠시 보류할게요.");
	const TCHAR* NoProposal = TEXT("기다리던 작전 제안이 없어요. 다시 물어봐 줄래요?");
	const TCHAR* ProposalFull = TEXT("규칙 자리가 꽉 차서 작전을 걸 수 없어요. 하나 취소할까요?");

	const TArray<FString>& ConditionTypes()
	{
		static const TArray<FString> Types = { TEXT("enemy_within"), TEXT("sister_hacking"), TEXT("hp_below"),
			TEXT("ammo_below"), TEXT("player_downed"), TEXT("battery_above") };
		return Types;
	}
}

UCXJeminiRuleComponent::UCXJeminiRuleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCXJeminiRuleComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCXJeminiRuleComponent, Rules);
	DOREPLIFETIME(UCXJeminiRuleComponent, Proposal);
}

void UCXJeminiRuleComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		GetWorld()->GetTimerManager().SetTimer(CheckTimer, this, &UCXJeminiRuleComponent::CheckRules, FMath::Max(0.05f, CheckInterval), true);
	}
}

void UCXJeminiRuleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckTimer);
	}
	Super::EndPlay(EndPlayReason);
}

ACXJeminiDrone* UCXJeminiRuleComponent::GetDrone() const
{
	return Cast<ACXJeminiDrone>(GetOwner());
}

float UCXJeminiRuleComponent::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
}

FCXJeminiRule* UCXJeminiRuleComponent::FindRule(int32 RuleId)
{
	return Rules.FindByPredicate([RuleId](const FCXJeminiRule& Rule) { return Rule.Id == RuleId; });
}

// ---------------------------------------------------------------- 등록·취소

int32 UCXJeminiRuleComponent::AddRule(const FCXJeminiRule& Rule, FString& OutReason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutReason = TEXT("NOT_AUTHORITY");
		return 0;
	}
	if (Rules.Num() >= MaxRules)
	{
		OutReason = TEXT("RULE_LIMIT");
		return 0;
	}
	// 서버가 이미 검증했지만 콘솔 명령 등 다른 경로도 같은 화이트리스트로 막는다
	bool bValid = Rule.When.Num() >= 1 && Rule.When.Num() <= 3 && ACXJeminiDrone::GetCommandCost(Rule.Action) >= 0;
	for (const FCXJeminiRuleCondition& Cond : Rule.When)
	{
		bValid &= CXJeminiRuleLines::ConditionTypes().Contains(Cond.Type);
	}
	if (!bValid)
	{
		OutReason = TEXT("INVALID_RULE");
		return 0;
	}
	FCXJeminiRule& Added = Rules.Add_GetRef(Rule);
	Added.Id = NextRuleId++;
	Added.bHeld = false;
	Added.HeldReason.Reset();
	Added.FailSince = -1.0f;
	Added.NextTryTime = 0.0f;
	if (Added.Label.IsEmpty())
	{
		Added.Label = Added.Action;
	}
	UE_LOG(LogCXJeminiRule, Log, TEXT("Jemini rule #%d added: %s"), Added.Id, *Added.Label);
	return Added.Id;
}

int32 UCXJeminiRuleComponent::RemoveRule(int32 RuleId)
{
	const int32 Removed = RuleId == 0 ? Rules.Num() : Rules.RemoveAll([RuleId](const FCXJeminiRule& Rule) { return Rule.Id == RuleId; });
	if (RuleId == 0)
	{
		Rules.Reset();
	}
	if (Removed > 0)
	{
		UE_LOG(LogCXJeminiRule, Log, TEXT("Jemini rule %s removed (%d)"), RuleId == 0 ? TEXT("all") : *FString::FromInt(RuleId), Removed);
	}
	return Removed;
}

bool UCXJeminiRuleComponent::HandleRuleResponse(const FCXJeminiResponse& Response, FString& OutLine)
{
	OutLine.Reset();
	if (Response.RuleOp == TEXT("register"))
	{
		FString Reason = TEXT("INVALID_RULE");
		if (Response.bHasRule && AddRule(Response.Rule, Reason) > 0)
		{
			return true;
		}
		OutLine = Reason == TEXT("RULE_LIMIT") ? CXJeminiRuleLines::Limit : CXJeminiRuleLines::Invalid;
		LastDebug = FString::Printf(TEXT("register rejected locally: %s"), *Reason);
		return false;
	}
	if (Response.RuleOp == TEXT("cancel"))
	{
		if (RemoveRule(Response.RuleId) > 0)
		{
			return true;
		}
		OutLine = CXJeminiRuleLines::NoRule;
		return false;
	}
	// 기능 C: 작전 제안 수락·거절
	if (Response.RuleOp == TEXT("accept"))
	{
		if (Proposal.Num() == 0)
		{
			OutLine = CXJeminiRuleLines::NoProposal;	// 그 사이 시간 초과
			return false;
		}
		if (AcceptProposal() > 0)
		{
			return true;
		}
		OutLine = CXJeminiRuleLines::ProposalFull;
		return false;
	}
	if (Response.RuleOp == TEXT("decline"))
	{
		ClearProposal(TEXT("declined"));
		return true;
	}
	// list: 서버가 요청 시점 목록으로 대사를 만들었다
	return true;
}

void UCXJeminiRuleComponent::FillState(FCXJeminiState& State) const
{
	State.ActiveRules = Rules;
	State.PendingProposal = Proposal;
}

// ---------------------------------------------------------------- 작전 제안(기능 C)

void UCXJeminiRuleComponent::SetProposal(const TArray<FCXJeminiRule>& NewProposal)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Proposal.Reset();
	for (const FCXJeminiRule& Rule : NewProposal)
	{
		if (Proposal.Num() < MaxRules && Rule.When.Num() > 0 && !Rule.Action.IsEmpty())
		{
			Proposal.Add(Rule);
		}
	}
	ProposalExpireTime = Now() + FMath::Max(1.0f, ProposalTimeout);
	if (Proposal.Num() > 0)
	{
		LastDebug = FString::Printf(TEXT("proposal x%d (%.0fs)"), Proposal.Num(), ProposalTimeout);
		UE_LOG(LogCXJeminiRule, Log, TEXT("Jemini proposal: %d rule(s) waiting %.0fs — %s"), Proposal.Num(), ProposalTimeout, *Proposal[0].Label);
	}
}

int32 UCXJeminiRuleComponent::AcceptProposal()
{
	int32 Added = 0;
	for (const FCXJeminiRule& Rule : Proposal)
	{
		FString Reason;
		if (AddRule(Rule, Reason) > 0)
		{
			++Added;
		}
	}
	UE_LOG(LogCXJeminiRule, Log, TEXT("Jemini proposal accepted: %d/%d registered"), Added, Proposal.Num());
	ClearProposal(TEXT("accepted"));
	LastDebug = FString::Printf(TEXT("proposal accepted +%d"), Added);
	return Added;
}

void UCXJeminiRuleComponent::ClearProposal(const TCHAR* Why)
{
	if (Proposal.Num() > 0)
	{
		UE_LOG(LogCXJeminiRule, Log, TEXT("Jemini proposal cleared (%s)"), Why);
		LastDebug = FString::Printf(TEXT("proposal %s"), Why);
	}
	Proposal.Reset();
}

// ---------------------------------------------------------------- 감시·발동(서버)

bool UCXJeminiRuleComponent::EvaluateCondition(const FCXJeminiRuleCondition& Condition, const FCXJeminiState& State)
{
	auto HpBelow = [&Condition](bool bAlive, int32 Hp, int32 HpMax)
	{
		return bAlive && HpMax > 0 && Hp * 100 < Condition.Value * HpMax;
	};
	const FString& Type = Condition.Type;
	if (Type == TEXT("enemy_within"))
	{
		// 상태의 가장 가까운 적 거리는 m(살아 있는 플레이어 기준, 15m 범위 안)
		return State.EnemyNearestDist >= 0.0f && State.EnemyNearestDist * 100.0f <= Condition.Value;
	}
	if (Type == TEXT("sister_hacking"))
	{
		return State.bHasPuzzle;
	}
	if (Type == TEXT("hp_below"))
	{
		const bool bBrother = HpBelow(State.bBrotherAlive, State.BrotherHp, State.BrotherHpMax);
		const bool bSister = HpBelow(State.bSisterAlive, State.SisterHp, State.SisterHpMax);
		return Condition.Who == TEXT("brother") ? bBrother : (Condition.Who == TEXT("sister") ? bSister : (bBrother || bSister));
	}
	if (Type == TEXT("ammo_below"))
	{
		return State.bBrotherAlive && State.BrotherAmmo < Condition.Value;
	}
	if (Type == TEXT("player_downed"))
	{
		return Condition.Who == TEXT("brother") ? !State.bBrotherAlive
			: (Condition.Who == TEXT("sister") ? !State.bSisterAlive : (!State.bBrotherAlive || !State.bSisterAlive));
	}
	if (Type == TEXT("battery_above"))
	{
		return State.Battery > Condition.Value;
	}
	return false;
}

void UCXJeminiRuleComponent::CheckRules()
{
	// 기능 C: 대답 없이 시간이 지난 작전 제안은 조용히 버린다(HUD 줄이 사라짐)
	if (Proposal.Num() > 0 && Now() >= ProposalExpireTime)
	{
		ClearProposal(TEXT("timeout"));
	}
	ACXJeminiDrone* Drone = GetDrone();
	if (!Drone || Rules.Num() == 0 || ValidatingRuleId != 0)
	{
		return;
	}
	const float T = Now();
	const FCXJeminiState State = Drone->GatherStateFor(nullptr);

	for (FCXJeminiRule& Rule : Rules)
	{
		bool bMet = Rule.When.Num() > 0;
		for (const FCXJeminiRuleCondition& Cond : Rule.When)
		{
			bMet = bMet && EvaluateCondition(Cond, State);
		}
		if (!bMet)
		{
			// 조건이 풀리면 실패 기록·보류 해제
			if (Rule.FailSince >= 0.0f || Rule.bHeld)
			{
				Rule.FailSince = -1.0f;
				Rule.bHeld = false;
				Rule.HeldReason.Reset();
			}
			continue;
		}
		if (T < Rule.NextTryTime || T - LastFireTime < RetryCooldown)
		{
			continue;
		}
		// 드론이 일하는 중·요청 대기 중·게임 오버 등 → HTTP 없이 BUSY로 기록
		if (!Drone->CanAcceptRequest())
		{
			NoteFailure(Rule, TEXT("BUSY"));
			continue;
		}
		// 발동 직전 재검증(LLM 없음). 한 번에 하나만.
		ValidatingRuleId = Rule.Id;
		Rule.NextTryTime = T + RetryCooldown;
		LastDebug = FString::Printf(TEXT("validating #%d %s"), Rule.Id, *Rule.Action);
		UE_LOG(LogCXJeminiRule, Log, TEXT("Jemini rule #%d conditions met -> /validate %s"), Rule.Id, *Rule.Action);
		TWeakObjectPtr<UCXJeminiRuleComponent> WeakThis(this);
		const int32 RuleId = Rule.Id;
		CXJemini::SendValidate(Drone->GetEndpoint(), Rule.Action, Rule.ActionWho, Rule.ReserveBattery, State,
			FOnCXJeminiResponse::CreateLambda([WeakThis, RuleId](const FCXJeminiResponse& Response)
			{
				if (UCXJeminiRuleComponent* Self = WeakThis.Get())
				{
					Self->OnValidated(RuleId, Response);
				}
			}));
		return;
	}
}

void UCXJeminiRuleComponent::OnValidated(int32 RuleId, const FCXJeminiResponse& Response)
{
	ValidatingRuleId = 0;
	FCXJeminiRule* Rule = FindRule(RuleId);
	ACXJeminiDrone* Drone = GetDrone();
	if (!Rule || !Drone)
	{
		return;	// 그 사이 취소됨
	}
	if (!Response.IsOk())
	{
		NoteFailure(*Rule, Response.Reason.IsEmpty() ? Response.Status : Response.Reason);
		return;
	}
	FString Reason;
	if (!Drone->ExecuteRuleCommand(Response, Rule->ReserveBattery, Reason))
	{
		NoteFailure(*Rule, Reason);
		return;
	}
	LastFireTime = Now();
	LastDebug = FString::Printf(TEXT("fired #%d %s"), RuleId, *Rule->Action);
	UE_LOG(LogCXJeminiRule, Log, TEXT("Jemini rule #%d fired: %s"), RuleId, *Rule->Label);
	if (Rule->bRepeat)
	{
		Rule->NextTryTime = LastFireTime + RepeatInterval;
		Rule->FailSince = -1.0f;
		Rule->bHeld = false;
		Rule->HeldReason.Reset();
	}
	else
	{
		RemoveRule(RuleId);
	}
}

void UCXJeminiRuleComponent::NoteFailure(FCXJeminiRule& Rule, const FString& Reason)
{
	const float T = Now();
	Rule.NextTryTime = FMath::Max(Rule.NextTryTime, T + RetryCooldown);
	if (Rule.FailSince < 0.0f)
	{
		Rule.FailSince = T;
	}
	Rule.HeldReason = Reason;
	LastDebug = FString::Printf(TEXT("#%d failed: %s"), Rule.Id, *Reason);
	UE_LOG(LogCXJeminiRule, Verbose, TEXT("Jemini rule #%d attempt failed: %s"), Rule.Id, *Reason);
	if (!Rule.bHeld && T - Rule.FailSince >= HoldAfter)
	{
		Rule.bHeld = true;
		UE_LOG(LogCXJeminiRule, Log, TEXT("Jemini rule #%d held (%s)"), Rule.Id, *Reason);
		// 드론이 바쁜 것은 금방 풀리므로 말하지 않는다(HUD만). 배터리·그 외 거절은 한 번 말한다.
		if (Reason != TEXT("BUSY"))
		{
			if (ACXJeminiDrone* Drone = GetDrone())
			{
				const bool bBattery = Reason == TEXT("LOW_BATTERY") || Reason == TEXT("RESERVE_BATTERY");
				Drone->SayRuleLine(bBattery ? CXJeminiRuleLines::HeldBattery : CXJeminiRuleLines::HeldOther, TEXT("rejected"));
			}
		}
	}
}

FString UCXJeminiRuleComponent::GetDebugState() const
{
	FString Out = FString::Printf(TEXT("rules=%d validating=%d proposal=%d last=\"%s\""), Rules.Num(), ValidatingRuleId, Proposal.Num(), *LastDebug);
	for (const FCXJeminiRule& Rule : Proposal)
	{
		Out += FString::Printf(TEXT(" | PLAN? %s"), *Rule.Label);
	}
	for (const FCXJeminiRule& Rule : Rules)
	{
		Out += FString::Printf(TEXT(" | #%d %s held=%d(%s) reserve=%d repeat=%d"), Rule.Id, *Rule.Label, Rule.bHeld ? 1 : 0, *Rule.HeldReason,
			Rule.ReserveBattery, Rule.bRepeat ? 1 : 0);
	}
	return Out;
}
