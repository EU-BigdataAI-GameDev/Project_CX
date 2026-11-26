// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXJeminiProactiveComponent.generated.h"

class ACXJeminiDrone;

/** 선제 제안 종류(복제 — HUD 키 안내용). */
UENUM()
enum class ECXJeminiOfferType : uint8
{
	None = 0,
	/** 오빠 탄이 얼마 안 남음 + 가까운 적 → ReloadSupport. */
	Reload,
	/** 오빠 체력이 많이 닳음 → HealSupport. */
	Heal,
	/** 여동생이 같은 단말기를 오래 해킹 중 → HackTerminal(대행). */
	HackAssist,
	/** 해킹 중인 여동생에게 적이 다가옴 → Decoy. */
	Decoy,
};

/**
 *  제미니 선제 제안 + 한 단어 명령. 드론(ACXJeminiDrone)에 붙는다. 서버(리슨 호스트)만 판단한다 — LLM·Jemini 서버 호출 없음.
 *
 *  1) 선제 제안: 플레이어가 전투·해킹 중이라 말을 못 하니 제미니가 먼저 고정 대사로 묻는다(말풍선, 상태 "coach").
 *     "응" = 채팅/음성 짧은 대답 또는 한 버튼(키보드 R / 패드 LB), "아니" = 채팅/음성 또는 한 버튼(키보드 T / 패드 RB).
 *     수락하면 같은 실행 경로(ACXJeminiDrone::ExecuteLocalCommand → HandleResponse → ExecuteCommand: 배터리·대상 검증·BUSY 규칙 그대로).
 *     LLM 작전 제안(UCXJeminiRuleComponent::Proposal)과는 별개 — 그쪽이 대기 중이면 선제 제안은 하지 않는다.
 *  2) 한 단어 명령: 채팅 한 단어(엄호·힐·장전…)를 서버 왕복 없이 바로 실행(ACXJeminiDrone::SubmitText가 먼저 부른다).
 *  - 말하지 않는 때: 드론 바쁨/말하는 중/요청 중, 제안 대기 중, 채팅·녹음 중, 튜토리얼·일시정지, WARDEN 단말기 근처(코치가 담당),
 *    게임 오버·이동, 배터리 부족, 쿨다운(종류별·전체).
 *  - 기능 문서: Docs/Jemini/선제_제안.md. 콘솔 CXJeminiOfferState / CXJeminiOffer <ammo|hp|stuck|decoy>.
 */
UCLASS(Config = Game)
class UCXJeminiProactiveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXJeminiProactiveComponent();

	/** 점검에 쓰는 지금 상태(디버그 출력용으로 공개). 거리 음수 = 적 없음. */
	struct FInputs
	{
		bool bBrotherAlive = false;
		int32 Ammo = 0;
		int32 AmmoMax = 0;
		bool bReloading = false;
		float BrotherEnemyCm = -1.0f;
		int32 BrotherHp = 0;
		int32 BrotherHpMax = 0;
		bool bSisterAlive = false;
		bool bSisterHackingTerminal = false;
		float StuckSeconds = 0.0f;
		float SisterEnemyCm = -1.0f;
		int32 Battery = 0;
	};

	// ---------------------------------------------------------------- 제안 상태

	ECXJeminiOfferType GetOfferType() const { return OfferType; }
	bool HasOffer() const { return OfferType != ECXJeminiOfferType::None; }
	/** 서버: 대답 대기 남은 시간(초). */
	float GetOfferRemaining() const;

	/** HUD 요약(영문 짧게). */
	static const TCHAR* GetOfferLabel(ECXJeminiOfferType Type);
	/** 제안 대사(고정 한국어, "[응/아니]" 포함). */
	static FString GetOfferLine(ECXJeminiOfferType Type);
	/** 제안이 실행하는 드론 명령 이름. */
	static const TCHAR* GetOfferCommand(ECXJeminiOfferType Type);
	static const TCHAR* GetOfferTypeName(ECXJeminiOfferType Type);

	/** 서버: 응/아니 한 버튼 또는 짧은 대답. 대기 중인 제안이 있어 처리했으면 true. From = 누른 플레이어 폰. */
	bool RespondToOffer(APawn* From, bool bAccept);

	/**
	 *  서버: 채팅 한 줄을 로컬에서 처리해 봄(제안 대답 → 한 단어 명령). 처리했으면 true(서버로 보내지 않음).
	 *  일부러 짧은 문장(정리 후 8자 이하)만 본다 — 그 밖은 기존대로 서버(LLM)로.
	 */
	bool HandleLocalText(APawn* From, const FString& Text);

	/** 서버: 음성 요청의 받아 적은 글(Heard)이 대기 중인 제안에 대한 짧은 대답이면 처리(서버 응답은 버린다). */
	bool HandleHeard(APawn* From, const FString& Heard);

	/** 테스트: 조건·쿨다운 무시하고 지금 제안(대기 중인 제안이 있으면 거절). 성공하면 true. */
	bool ForceOffer(ECXJeminiOfferType Type, FString& OutWhy);
	/** 테스트: "ammo|hp|stuck|decoy" → 종류. */
	static ECXJeminiOfferType ParseOfferName(const FString& Name);

	/** 한 줄 상태(입력·쿨다운·막힌 이유). 콘솔 CXJeminiOfferState. */
	FString GetDebugState() const;

	/** 지금 제안할 수 없는 이유(제안할 수 있으면 빈 값). */
	FString GetBlockReason() const;

	/** 짧은 대답 판별: 1 = 응, -1 = 아니, 0 = 둘 다 아님. */
	static int32 ParseShortAnswer(const FString& Text);
	/** 공백·문장부호를 걷어내고 소문자로. */
	static FString NormalizeUtterance(const FString& Text);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 점검 간격(초). */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float CheckInterval = 0.25f;

	/** 제안에 대답할 수 있는 시간(초). 지나면 조용히 버린다. */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float OfferTimeout = 15.0f;

	/** 같은 종류 제안 사이 최소 간격(초). */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float TypeCooldown = 30.0f;

	/** 어떤 제안이든 두 제안 사이 최소 간격(초). */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float GlobalCooldown = 8.0f;

	/** 제미니가 다른 대사를 한 뒤 이만큼은 제안하지 않는다(초). HUD 말풍선 5초 + 여유. */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float DroneSpeechHold = 6.5f;

	/** 재장전 제안: 탄이 탄창의 이 비율 이하. */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float AmmoFraction = 0.3334f;

	/** 재장전 제안: 오빠 주변 이 거리(cm) 안에 살아 있는 적. */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float AmmoEnemyRange = 1500.0f;

	/** 회복 제안: 오빠 체력이 최대의 이 비율 미만. */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float HealHpFraction = 0.4f;

	/** 퍼즐 도움 제안: 같은 단말기를 이 시간(초) 넘게 해킹 중. */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float StuckSeconds = 20.0f;

	/** 미끼 제안: 해킹 중인 여동생 주변 이 거리(cm) 안에 움직이는 적. */
	UPROPERTY(Config, EditAnywhere, Category = "Proactive")
	float DecoyEnemyRange = 500.0f;

private:
	ACXJeminiDrone* GetDrone() const;
	float Now() const;
	void GatherInputs(FInputs& Out) const;
	bool Meets(ECXJeminiOfferType Type, const FInputs& In) const;
	/** 배터리가 이 제안의 명령 비용 이상인지. */
	bool CanAfford(ECXJeminiOfferType Type, const FInputs& In) const;
	float CooldownRemaining(ECXJeminiOfferType Type) const;
	void ShowOffer(ECXJeminiOfferType Type, bool bForced);
	void ClearOffer(const TCHAR* Why);
	bool IsAnyoneTyping() const;
	bool IsNearWarden() const;
	bool TryKeywordCommand(APawn* From, const FString& Normalized);

	/** 대기 중인 제안(복제 — HUD 키 안내). */
	UPROPERTY(Replicated)
	ECXJeminiOfferType OfferType = ECXJeminiOfferType::None;

	/** 서버: 제안이 사라지는 시각(월드 초). */
	double OfferExpireTime = 0.0;
	bool bOfferForced = false;

	double LastAnyOfferTime = -1000.0;
	double LastOfferTime[5] = { -1000.0, -1000.0, -1000.0, -1000.0, -1000.0 };

	/** 같은 단말기 해킹 연속 시간(서버). */
	TWeakObjectPtr<AActor> StuckTerminal;
	float StuckAccum = 0.0f;

	int32 SeenDroneSeq = -1;
	double LastDroneSpeechTime = -1000.0;
	FString LastDebug;
};
