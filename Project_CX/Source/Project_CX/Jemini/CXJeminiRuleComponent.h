// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXJeminiRuleTypes.h"
#include "CXJeminiRuleComponent.generated.h"

class ACXJeminiDrone;
struct FCXJeminiState;
struct FCXJeminiResponse;

/**
 *  제미니 조건부 지시(규칙) — .claude/Plans/제미니 고도화 기획 ver2.md 2장(기능 A). 드론(ACXJeminiDrone)에 붙는다.
 *
 *  - 서버 권위: 규칙 목록은 서버(리슨 호스트)가 갖고 복제한다(HUD 한 줄 목록·"규칙 보류" 표시용). 최대 MaxRules(2)개.
 *  - 등록·취소: 플레이어 발화 → Jemini 서버 RegisterRule / ManageRules → 응답 status "rule"을 드론이 HandleRuleResponse로 넘긴다.
 *    등록은 배터리를 쓰지 않는다. 테스트는 콘솔 CXJeminiRuleAdd / CXJeminiRuleClear / CXJeminiRuleList.
 *  - 감시: CheckInterval(0.2초)마다 드론의 GatherState(서버로 보내는 상태와 같은 값)로 조건을 평가(AND).
 *  - 발동: 조건 충족 → POST /validate(LLM 없음, 기존 검증기 + 예비 배터리) → ok면 드론의 기존 ExecuteCommand 경로로 실행
 *    (배터리는 효과가 실제로 실행된 순간 차감 — 기존 규칙 그대로). 기본 1회 발동 후 삭제, repeat면 RepeatInterval 뒤 다시.
 *  - 실패(BUSY·LOW_BATTERY·RESERVE_BATTERY·연결 실패·기타 거절): 규칙 유지, RetryCooldown(2초)마다 재시도.
 *    조건이 맞은 채 HoldAfter초 동안 계속 실패하면 bHeld(HUD "규칙 보류") + 대사 1회. 조건이 풀리면 보류 해제.
 *  - 규칙 발동끼리 연속으로 터지지 않게 전역 쿨다운 RetryCooldown(2초).
 *  - 맵 이동·이어하기로는 이어지지 않는다(드론과 함께 새로 시작).
 *  - 기능 C(작전 제안): 서버 Advise 응답의 proposal(검증된 규칙 0~2개)을 등록하지 않고 Proposal(대답 대기)로 보관·복제한다.
 *    state.pending_proposal로 서버에 다시 보내고, "응" → rule_op accept면 빈 자리만큼 AddRule, "아니" → decline / ProposalTimeout 경과 → 버림.
 */
UCLASS(ClassGroup = (Custom))
class UCXJeminiRuleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXJeminiRuleComponent();

	static constexpr int32 MaxRules = 2;

	/** 복제된 규칙 목록(HUD). */
	const TArray<FCXJeminiRule>& GetRules() const { return Rules; }

	/** 서버: 규칙 추가. 가득 찼거나 형식이 틀리면 false(OutReason = RULE_LIMIT / INVALID_RULE). 성공하면 붙인 Id. */
	int32 AddRule(const FCXJeminiRule& Rule, FString& OutReason);

	/** 서버: 규칙 삭제(Id 0 = 전부). 지운 개수. */
	int32 RemoveRule(int32 RuleId);

	/**
	 *  서버: 드론이 받은 status "rule" 응답(register / cancel / list). UE 쪽에서 거절(가득 참·지울 규칙 없음)하면 false.
	 *  OutLine = 응답 대사 대신 띄울 대사(비우면 응답 대사 그대로).
	 */
	bool HandleRuleResponse(const FCXJeminiResponse& Response, FString& OutLine);

	/** 기능 C. 대답 대기 중인 작전 제안(복제, HUD "PLAN?"). */
	const TArray<FCXJeminiRule>& GetProposal() const { return Proposal; }

	/** 서버: 작전 제안 보관(빈 배열이면 지운다). 이전 제안은 바꾼다. */
	void SetProposal(const TArray<FCXJeminiRule>& NewProposal);

	/** 서버: 제안 수락 → 빈 자리만큼 등록. 등록한 개수. */
	int32 AcceptProposal();

	/** 서버: 제안 버림(거절·시간 초과·테스트). */
	void ClearProposal(const TCHAR* Why);

	/** 서버로 보내는 상태에 붙일 규칙 목록. */
	void FillState(FCXJeminiState& State) const;

	/** 조건 1개 평가(서버 상태 기준). */
	static bool EvaluateCondition(const FCXJeminiRuleCondition& Condition, const FCXJeminiState& State);

	/** 테스트: 한 줄 상태. */
	FString GetDebugState() const;

	/** 조건 검사 주기(초). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Rule")
	float CheckInterval = 0.2f;

	/** 발동 쿨다운 = 재시도 간격(초). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Rule")
	float RetryCooldown = 2.0f;

	/** 조건이 맞은 채 이만큼(초) 계속 실패하면 "규칙 보류". */
	UPROPERTY(EditAnywhere, Category = "Jemini|Rule")
	float HoldAfter = 6.0f;

	/** repeat 규칙이 발동 후 다시 발동할 수 있기까지(초). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Rule")
	float RepeatInterval = 6.0f;

	/** 작전 제안에 대답할 수 있는 시간(초). 조언 대사(여러 줄)를 읽을 시간 포함. */
	UPROPERTY(EditAnywhere, Category = "Jemini|Rule")
	float ProposalTimeout = 20.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	ACXJeminiDrone* GetDrone() const;
	void CheckRules();
	void OnValidated(int32 RuleId, const FCXJeminiResponse& Response);
	/** 시도 실패 기록(재시도 쿨다운, HoldAfter 넘으면 보류 + 대사 1회). */
	void NoteFailure(FCXJeminiRule& Rule, const FString& Reason);
	FCXJeminiRule* FindRule(int32 RuleId);
	float Now() const;

	UPROPERTY(Replicated)
	TArray<FCXJeminiRule> Rules;

	/** 기능 C: 대답 대기 중인 작전 제안(최대 MaxRules개). */
	UPROPERTY(Replicated)
	TArray<FCXJeminiRule> Proposal;

	/** 제안이 사라지는 시각(서버). */
	float ProposalExpireTime = 0.0f;

	int32 NextRuleId = 1;
	/** /validate 응답 대기 중인 규칙 Id(0 = 없음). 한 번에 하나만. */
	int32 ValidatingRuleId = 0;
	/** 마지막 발동 시각(전역 쿨다운). */
	float LastFireTime = -100.0f;
	FTimerHandle CheckTimer;
	FString LastDebug;
};
