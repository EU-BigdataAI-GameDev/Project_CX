// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoopInteractable.h"
#include "CXWardenTypes.h"
#include "CXWardenTerminal.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;
class UStaticMesh;
class UMaterialInterface;
class USoundBase;
struct FCXWardenResponse;

/**
 *  기능 B. 관리 AI WARDEN 단말기 + 그것이 잠근 보안 문(.claude/Plans/제미니 고도화 기획 ver2.md 3장).
 *
 *  - 서버 권위(listen server): 증거 획득 상태(EvidenceMask), 해결 여부, WARDEN 대사, 요청 중 여부를 복제한다.
 *    증거는 이 액터가 보관한다(맵에 하나). 획득 경로 = 오빠의 증거 픽업(ACXEvidencePickup), 단말기 해킹 보상(AHackTerminal::RewardEvidenceId), 콘솔.
 *  - 살아 있는 플레이어(오빠·여동생)가 TalkRange 안에 있으면 그 사람의 채팅(Enter)·음성(Y)은 제미니 대신 WARDEN으로 간다(UCXJeminiInputComponent가 FindTalkTarget으로 고른다).
 *    서버가 POST /persuade(/voice)에 획득 증거 id만 보내고, 서버의 판정(solved)만 믿는다. 시도 간격 3초(게임에서도 막는다).
 *  - solved → 문이 말려 올라가며(ACXCoopExitDoor 셔터 방식) 충돌 상자(COL 역할, 보이지 않음)를 끈다. 이후 진행.
 *  - 비주얼은 엔진 큐브 + M_NeonEmissive(CXShapeKit). 로컬 -X = 카메라 쪽. 원점 = 단말기 바닥 가운데.
 *    문은 GateOffset(로컬)에 놓이고 GateExtent(반 크기, 바닥부터)만큼 레인을 막는다 — 레벨에서 맞춘다.
 */
UCLASS()
class ACXWardenTerminal : public AActor, public ICoopInteractable
{
	GENERATED_BODY()

public:
	ACXWardenTerminal();

	/** 월드의 첫 WARDEN 단말기(없으면 nullptr). */
	static ACXWardenTerminal* FindFirst(const UObject* WorldContextObject);

	/** 서버: 증거 획득(Id "E1"~"E3"). 월드의 모든 WARDEN 단말기에 기록. 새로 얻었으면 true. */
	static bool AwardEvidence(const UObject* WorldContextObject, FName EvidenceId, const APawn* By);

	/** 이 폰의 발화를 받을 WARDEN(여동생 + 범위 안 + 아직 안 열림). 없으면 nullptr. 클라이언트에서도 복제값으로 같다. */
	static ACXWardenTerminal* FindTalkTarget(const APawn* Pawn);

	// ICoopInteractable(F/X): 여동생이 누르면 WARDEN이 상황을 다시 말한다
	virtual bool CanInteract(const APawn* Pawn) const override;
	virtual void Interact(APawn* Pawn) override;
	virtual FVector GetInteractLocation(const APawn* Pawn) const override;
	virtual FString GetInteractPrompt(const APawn* Pawn) const override { return TEXT("WARDEN"); }

	// ---------------------------------------------------------------- 서버
	/** 증거 비트 설정. 새로 얻었으면 true. */
	bool GrantEvidence(int32 Index);
	/** 설득 요청(서버). 텍스트는 요청 중·간격 3초 안이면 마지막 한 개를 보관했다가 이어서 보내고 안내 대사를 띄운다(true). 음성은 그때는 false + 안내 대사. 해결됨이면 false. */
	bool SubmitText(APawn* From, const FString& Text);
	bool SubmitVoice(APawn* From, const TArray<uint8>& Wav);
	/** 테스트·시연: 강제로 해결(문 열기). */
	void ForceSolve(const FString& Why);

	// ---------------------------------------------------------------- 조회(HUD — 복제값)
	bool HasEvidence(int32 Index) const { return Index >= 0 && Index < CXWarden::EvidenceCount && (EvidenceMask & (1 << Index)) != 0; }
	uint8 GetEvidenceMask() const { return EvidenceMask; }
	/** 누적 인정된 증거(WARDEN이 이미 인정한 것). HUD 진행 표시와 다음 요청에 쓴다. */
	uint8 GetCreditedMask() const { return CreditedMask; }
	int32 GetCreditedNum() const;
	int32 GetEvidenceNum() const;
	bool IsSolved() const { return bSolved; }
	bool IsRequestPending() const { return bRequestPending; }
	bool CanAcceptTalk() const { return !bSolved && !bRequestPending; }
	const FString& GetWardenLine() const { return WardenLine; }
	const FString& GetPlayerLine() const { return PlayerLine; }
	const FString& GetLineStatus() const { return LineStatus; }
	int32 GetLineSeq() const { return LineSeq; }
	bool WasLastMock() const { return bLastMock; }
	/** 마지막 설득 거절 사유(서버 전용 — 제미니 조언 힌트용, 기능 C). 해결·거절 전이면 빈 값. */
	const FString& GetLastReason() const { return LastReason; }
	/** 대사 말풍선을 보여 줄 거리(cm) — 두 플레이어 모두. */
	float GetShowRange() const { return ShowRange; }
	FVector GetScreenAnchor() const;
	float GetTalkRange() const { return TalkRange; }
	/** 테스트 이동(CXGotoWarden): 문 앞 레인 위 지점(단말기 기준 -Y로 BackY만큼, 레인 깊이는 문 중심). */
	FVector GetApproachLocation(float BackY) const;
	FString GetDebugState() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 여동생이 이 거리(cm, 단말기 기준 Y·Z 평면) 안이면 말이 WARDEN으로 간다. */
	UPROPERTY(EditAnywhere, Category = "Warden")
	float TalkRange = 550.0f;

	/** 두 플레이어 HUD에 WARDEN 대사를 보여 주는 거리(cm). */
	UPROPERTY(EditAnywhere, Category = "Warden")
	float ShowRange = 1600.0f;

	/** 같은 단말기에 다시 말할 수 있는 간격(초). 서버(PERSUADE_COOLDOWN_SEC)도 따로 막는다. */
	UPROPERTY(EditAnywhere, Category = "Warden")
	float AttemptCooldown = 3.0f;

	/** 문(보안 셔터) 위치(로컬, 바닥 가운데). */
	UPROPERTY(EditAnywhere, Category = "Warden|Gate", meta = (MakeEditWidget = "true"))
	FVector GateOffset = FVector(-170.0f, 260.0f, 0.0f);

	/** 문 충돌 반 크기(cm). X = 레인 깊이(앞뒤), Y = 두께, Z = 높이의 절반(바닥부터 2*Z까지 막는다). */
	UPROPERTY(EditAnywhere, Category = "Warden|Gate")
	FVector GateExtent = FVector(240.0f, 40.0f, 330.0f);

	/** 문 셔터 보이는 높이(cm). */
	UPROPERTY(EditAnywhere, Category = "Warden|Gate")
	float GateVisualHeight = 380.0f;

	UPROPERTY(EditAnywhere, Category = "Warden|Gate")
	float OpenDuration = 1.6f;

	UPROPERTY(EditAnywhere, Category = "Warden|Sound") TObjectPtr<USoundBase> ReplySound;
	UPROPERTY(EditAnywhere, Category = "Warden|Sound") TObjectPtr<USoundBase> DeniedSound;
	UPROPERTY(EditAnywhere, Category = "Warden|Sound") TObjectPtr<USoundBase> OpenSound;

	// ---------------------------------------------------------------- 비주얼
	UPROPERTY(VisibleAnywhere, Category = "Warden") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "Warden") TArray<TObjectPtr<UStaticMeshComponent>> BodyPieces;
	/** 화면(붉은 눈 — 해결되면 청록). */
	UPROPERTY(VisibleAnywhere, Category = "Warden") TObjectPtr<UStaticMeshComponent> Screen;
	UPROPERTY(VisibleAnywhere, Category = "Warden") TObjectPtr<UStaticMeshComponent> Eye;
	UPROPERTY(VisibleAnywhere, Category = "Warden") TObjectPtr<UPointLightComponent> EyeLight;
	UPROPERTY(VisibleAnywhere, Category = "Warden") TObjectPtr<UTextRenderComponent> NameLabel;
	UPROPERTY(VisibleAnywhere, Category = "Warden") TObjectPtr<UTextRenderComponent> StatusLabel;

	UPROPERTY(VisibleAnywhere, Category = "Warden|Gate") TObjectPtr<USceneComponent> GateRoot;
	/** 레인을 막는 보이지 않는 충돌(COL_ 규칙: 아트는 NoCollision, 막는 건 이 상자). */
	UPROPERTY(VisibleAnywhere, Category = "Warden|Gate") TObjectPtr<UBoxComponent> GateBlocker;
	UPROPERTY(VisibleAnywhere, Category = "Warden|Gate") TObjectPtr<USceneComponent> ShutterPivot;
	UPROPERTY(VisibleAnywhere, Category = "Warden|Gate") TObjectPtr<UStaticMeshComponent> ShutterPanel;
	UPROPERTY(VisibleAnywhere, Category = "Warden|Gate") TArray<TObjectPtr<UStaticMeshComponent>> ShutterStripes;
	UPROPERTY(VisibleAnywhere, Category = "Warden|Gate") TArray<TObjectPtr<UStaticMeshComponent>> GateFrame;
	UPROPERTY(VisibleAnywhere, Category = "Warden|Gate") TObjectPtr<UStaticMeshComponent> GateLamp;

private:
	void ApplyGateLayout();
	void ApplyVisualState();
	void ApplyOpenAlpha();
	bool BeginAttempt(APawn* From, const TCHAR* Kind, int32 InputLen);
	/** 요청 중·간격 안에 들어온 텍스트를 마지막 한 개만 보관했다가 풀리면 보낸다. */
	void QueueText(const FString& Text, const TCHAR* Why);
	void ScheduleQueuedFlush(float MinDelay);
	void FlushQueued();
	float GetCooldownRemaining() const;
	void HandleResponse(const FCXWardenResponse& Response);
	void SetLine(const FString& Warden, const FString& Player, const FString& Status);
	void Solve(const FString& Why);
	void CheckApproach();
	TArray<FString> GetEvidenceIds() const;
	TArray<FString> GetCreditedIds() const;
	FString GetSessionId() const;

	UFUNCTION() void OnRep_Solved();
	UFUNCTION() void OnRep_Line();

	UPROPERTY(Replicated)
	uint8 EvidenceMask = 0;

	/** 누적 인정(2026-11-11): 이전 시도에서 맞게 말한 증거. 서버(/persuade) 응답 credited로 갱신, 인젝션이면 비워진다. */
	UPROPERTY(Replicated)
	uint8 CreditedMask = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Solved)
	bool bSolved = false;

	UPROPERTY(Replicated)
	bool bRequestPending = false;

	UPROPERTY(ReplicatedUsing = OnRep_Line)
	FString WardenLine;

	/** 마지막 플레이어 발화(텍스트 또는 들은 문장). */
	UPROPERTY(Replicated)
	FString PlayerLine;

	/** intro / solved / rejected / cooldown / fallback / busy */
	UPROPERTY(Replicated)
	FString LineStatus;

	UPROPERTY(ReplicatedUsing = OnRep_Line)
	int32 LineSeq = 0;

	UPROPERTY(Replicated)
	bool bLastMock = false;

	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> NeonMaterial;

	float OpenAlpha = 0.0f;
	double LastAttemptTime = -100.0;
	bool bIntroduced = false;
	FTimerHandle ApproachTimer;
	int32 LocalSoundSeq = 0;
	/** 요청 보낸 사람(타이핑 문장 표시). */
	FString PendingPlayerText;
	/** 마지막 rejected 사유(NEED_RULE 등, 서버 전용). */
	FString LastReason;
	/** 분석 중·간격 안에 받은 최신 텍스트(서버 전용). 응답이 오면 이어서 보낸다. */
	FString QueuedText;
	FTimerHandle QueueTimer;
};
