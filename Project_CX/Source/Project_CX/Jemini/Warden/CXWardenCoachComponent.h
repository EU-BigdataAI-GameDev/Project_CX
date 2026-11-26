// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXWardenCoachComponent.generated.h"

class ACXWardenTerminal;

/** 코치 힌트 단계(번호 = 말하는 우선순위의 반대: 5가 가장 먼저 검사). */
enum class ECXWardenCoachStep : uint8
{
	None = 0,
	/** 1. 처음 도착 + 증거 1개 이상: 증거를 말로 읽으라는 안내. */
	Arrival = 1,
	/** 2. 오래 머무는데 증거 0개: 더 찾아보라는 안내. */
	NoEvidence = 2,
	/** 3. 3/3 인정 + 아직 안 열림: 이유 + 문 열어 달라고 말하라는 안내. */
	AllCredited = 3,
	/** 4. 요구만 함(DEMAND_ONLY): 이유를 들라는 안내. */
	DemandOnly = 4,
	/** 5. 거절 3회 이상: 예시 문장. */
	Example = 5,
};

/**
 *  WARDEN 코치: 단말기 근처에서 막힌 듯한 플레이어에게 제미니(드론)가 고정 대사로 먼저 말을 건다. LLM·서버 호출 없음, 배터리 소모 없음.
 *
 *  - ACXWardenTerminal의 기본 서브오브젝트. 서버(리슨 호스트)에서만 돈다. 힌트는 팀 전체에 한 번(플레이어별 아님).
 *  - 입력: 획득·인정 증거 수, 마지막 거절 사유(ACXWardenTerminal::GetLastReason), 거절 횟수(NotifyResponse), 근처에 머문 시간, 요청 중 여부.
 *  - 말하지 않는 때: 해결됨, WARDEN 요청 중, 드론이 바쁨/다른 대사 중, WARDEN 답을 읽는 중, 채팅·녹음 중, 튜토리얼·일시정지, 쿨다운 안.
 *  - 기능 문서: Docs/Jemini/WARDEN_설명.md "플레이어 가이드(코치)". 콘솔 CXWardenCoachState로 현재 입력·단계를 본다.
 */
UCLASS(Config = Game)
class UCXWardenCoachComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXWardenCoachComponent();

	/** 서버: WARDEN 판정 응답이 왔다. 거절이면 횟수를 올린다. */
	void NotifyResponse(bool bRejected);

	int32 GetRejectCount() const { return RejectCount; }
	/** 지금 조건이 맞는 단계(말할 수 있는 상황인지는 따지지 않는다). 디버그·콘솔용. */
	ECXWardenCoachStep EvaluateStep() const;
	/** 지금 말을 못 하는 이유(말할 수 있으면 빈 값). */
	FString GetBlockReason() const;
	FString GetDebugState() const;

	/** 단계별 대사(고정 한국어). Example은 모은 증거에 따라 두 가지. */
	static FString GetLineFor(ECXWardenCoachStep Step, bool bAllEvidence);
	static const TCHAR* GetStepName(ECXWardenCoachStep Step);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 점검 간격(초). */
	UPROPERTY(Config, EditAnywhere, Category = "Coach")
	float CheckInterval = 0.5f;

	/** 단말기 범위(TalkRange)에 이만큼 머문 뒤에야 도착 안내(초) — WARDEN 첫 인사를 먼저 읽게. */
	UPROPERTY(Config, EditAnywhere, Category = "Coach")
	float ArrivalDelay = 2.5f;

	/** 증거 0개로 이만큼 머물면 더 찾아보라는 안내(초). */
	UPROPERTY(Config, EditAnywhere, Category = "Coach")
	float NoEvidenceSeconds = 25.0f;

	/** 코치 대사 사이 최소 간격(초). */
	UPROPERTY(Config, EditAnywhere, Category = "Coach")
	float HintCooldown = 14.0f;

	/** WARDEN이 새 대사를 띄운 뒤 이만큼은 말하지 않는다(답을 읽을 시간, 초). */
	UPROPERTY(Config, EditAnywhere, Category = "Coach")
	float ReadDelayAfterWarden = 4.0f;

	/** 제미니가 다른 대사를 한 뒤 이만큼은 말하지 않는다(초). HUD 말풍선 5초 + 여유. */
	UPROPERTY(Config, EditAnywhere, Category = "Coach")
	float DroneSpeechHold = 6.5f;

	/** 이 횟수 이상 거절당하면 예시 문장(1회). */
	UPROPERTY(Config, EditAnywhere, Category = "Coach")
	int32 ExampleRejectCount = 3;

	/** 3·4단계는 같은 안내를 이 횟수만큼 더 거절당한 뒤에야 다시 한다. */
	UPROPERTY(Config, EditAnywhere, Category = "Coach")
	int32 RepeatAfterRejections = 3;

private:
	ACXWardenTerminal* GetTerminal() const;
	bool IsAnyoneNear(const ACXWardenTerminal& Terminal) const;
	bool IsPlayerTyping() const;
	/** 단계가 이번 거절 횟수에서 이미 말한 것인지(다시 말해도 되는지). */
	bool CanRepeat(ECXWardenCoachStep Step) const;

	int32 RejectCount = 0;
	/** 마지막 거절 시각(월드 초). */
	double LastRejectTime = -1000.0;
	/** 근처(TalkRange)에 머문 누적 시간 — 아무도 없으면 0으로. */
	float NearSeconds = 0.0f;
	double LastHintTime = -1000.0;
	ECXWardenCoachStep LastShownStep = ECXWardenCoachStep::None;
	/** 단계별로 마지막에 말한 시점의 거절 횟수(-1 = 아직 안 함). 인덱스 = 단계 번호. */
	int32 ShownAtReject[6] = { -1, -1, -1, -1, -1, -1 };
	int32 SeenDroneSeq = -1;
	int32 SeenWardenSeq = -1;
	double LastDroneSpeechTime = -1000.0;
	double LastWardenLineTime = -1000.0;
};
