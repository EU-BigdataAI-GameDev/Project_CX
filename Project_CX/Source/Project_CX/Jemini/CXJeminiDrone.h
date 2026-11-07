// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXJeminiClient.h"
#include "CXJeminiDrone.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USoundBase;
class AHackTerminal;

/** 드론 표정(Docs/SupportDrone_UE_Prompt.md 6장 E_DroneEmotion). 값 = 머티리얼 Emotion 파라미터 = 아틀라스 열. */
UENUM(BlueprintType)
enum class ECXDroneEmotion : uint8
{
	Neutral = 0,
	Happy = 1,
	Sad = 2,
	Angry = 3,
};

UENUM()
enum class ECXJeminiAction : uint8
{
	Idle,
	/** 재장전: 오빠에게 날아가는 중 → 도착하면 재장전 시작. */
	FlyToReload,
	/** 단말기 해킹 A: 여동생이 연 단말기를 여동생 곁에서 대행(대행 타이머는 여동생 해킹 컴포넌트가 돈다). */
	HackAssist,
	/** 단말기 해킹 B: 진행 순서상 다음 잠긴 단말기로 비행. */
	TerminalHack,
	/** 단말기 해킹 B: 단말기 위에서 AssistTimeTerminal초 해킹(상태등 깜빡임 + 빔). */
	TerminalHacking,
	/** 적 해킹: 대상으로 대시 + 해킹 빔(효과는 응답 즉시). */
	EnemyHack,
	/** 적 공격: 대상 머리 위로 비행 → 펄스 → 잠깐 머문 뒤 복귀. */
	PulseAttack,
	/** 공격력 강화: 오빠에게 비행(도착 시 강화 시작/연장). */
	BoostFly,
	/** 체력 회복: 두 사람 중간 위(효과는 응답 즉시). */
	Heal,
	/** 미끼: 적 너머 공중으로 비행. */
	Decoy,
	/** 미끼: DecoyDuration초 점멸·경고음, 거미들이 드론 쪽으로. */
	DecoyActive,
	/** 빠른 재부팅: 살아 있는 파트너 곁으로 비행. */
	Reboot,
	/** 빠른 재부팅: 빔 → 부활. */
	RebootBeam,
	/** 보호막: 대상에게 비행(도착 시 보호막). */
	ShieldFly,
	/** 작업 후 플레이어에게 복귀(아직 작업 중 — 새 요청 불가). */
	Returning,
};

/** 적 조회 결과(상태 수집과 실행 대상 결정이 같은 함수 — 서버 판단과 UE 판단 일치, 5-2절). */
struct FCXJeminiEnemyQuery
{
	int32 AliveCount = 0;
	int32 HackableCount = 0;
	int32 ActiveCount = 0;
	/** 가장 가까운 살아 있는 적까지(cm). 없으면 음수. */
	float NearestDistCm = -1.0f;
	AActor* NearestAlive = nullptr;
	AActor* NearestHackable = nullptr;
	/** 범위 안 움직이는(그로기 아님) 적 전부(미끼 대상). */
	TArray<AActor*> ActiveEnemies;
};

/**
 *  드론형 AI 동료 '제미니'(.claude/Plans/제미니 AI봇 기획.md, Docs/Jemini/).
 *
 *  서버 권위: 배터리, 요청 진행 여부, 명령 실행은 서버(리슨 호스트)가 갖고 복제한다.
 *  - 플레이어 입력(UCXJeminiInputComponent) → 서버 SubmitText/SubmitVoice → 게임 상태를 모아 Jemini 서버(FastAPI)로 HTTP
 *    → 응답 status ok면 명령 9종 실행(.claude/Plans/제미니 기능 기획.md). 배터리는 효과가 실제로 실행된 순간에만 battery_after로
 *    (그 사이 대상이 사라지면 차감 없이 TARGET_LOST 대사). 대사는 두 칸 HUD에 말풍선(줄바꿈마다 다음 말풍선).
 *  - 요청 처리 중이거나 드론 작업 중(Action != Idle)이면 새 요청을 받지 않는다(HUD JEMINI BUSY).
 *  - 쓰러진(부활 대기) 플레이어도 말할 수 있다(speaker = 말한 사람).
 *  - 배터리: 맵 이동은 게임 인스턴스, 이어하기는 세이브(CX_Slot0)로 이어진다. 세이브 복도에서 저장하면 100으로 충전.
 *  - 메시는 SM_SupportDrone(블렌더 제작, Docs/SupportDrone_UE_Prompt.md). 없으면 엔진 구체로 대신한다.
 *  - 표정: Emotion(Neutral/Happy/Sad/Angry) → 스크린(M_Drone_Screen)·발광(M_Drone_EmotionGlow)·추진기·라이트 색을 한 번에.
 *    응답에 따라 잠깐 반응(ok = 기쁨, 거절·배터리 부족·연결 실패 = 슬픔) 후 기본 표정(배터리 20% 이하 = 슬픔, 플레이어 다운 = 화남)으로 돌아온다.
 *    (기획 문서의 BP_SupportDrone 기능은 프로젝트 규칙(C++ 우선)에 따라 이 클래스에 C++로 구현)
 *
 *  서버 주소·토큰은 DefaultGame.ini [/Script/Project_CX.CXJeminiDrone]. 게임모드 BeginPlay에서 한 대 스폰된다.
 */
UCLASS(Config = Game)
class ACXJeminiDrone : public AActor
{
	GENERATED_BODY()

public:
	ACXJeminiDrone();

	/** 월드의 드론(없으면 nullptr). */
	static ACXJeminiDrone* Get(const UObject* WorldContextObject);

	/** 서버: 없으면 스폰. */
	static ACXJeminiDrone* EnsureSpawned(UWorld* World);

	/** DefaultGame.ini의 서버 주소(따옴표·잘림 보정, 끝 '/' 없음). 서버 자동 실행(UCXJeminiServerSubsystem)도 쓴다. */
	static FString GetConfiguredServerUrl();

	// ---------------------------------------------------------------- 요청(서버)

	/** 지금 새 요청을 받을 수 있는지(요청 중 아님 + 드론 작업 중 아님 + 게임 오버·클리어·이동 중 아님). */
	bool CanAcceptRequest() const;

	/** 요청 대기 중이거나 드론 행동 중(Action != Idle) — 복제값만 보므로 클라이언트도 같다(HUD JEMINI BUSY). */
	bool IsBusy() const { return bRequestPending || Action != ECXJeminiAction::Idle; }

	/** 기능 비용(서버 config.py와 같은 값, 디버그 명령용). 모르는 명령은 -1. */
	static int32 GetCommandCost(const FString& CommandName);

	/** 테스트: LLM 없이 ok 응답을 받은 것처럼 실행(배터리 부족이면 LOW_BATTERY 거절). 시작했으면 true. */
	bool DebugExecuteCommand(APawn* Speaker, const FString& CommandName, const FString& Target);

	/** 서버: 채팅 요청. From = 말한 플레이어 폰. 받았으면 true. */
	bool SubmitText(APawn* From, const FString& Text);

	/** 서버: 음성 요청(16kHz 모노 WAV). */
	bool SubmitVoice(APawn* From, const TArray<uint8>& Wav);

	/** 서버: 세이브포인트가 저장하기 직전. 충전 맵(세이브 복도)이면 100으로 충전. */
	void NotifyProgressSaving();

	/** 서버: 배터리 직접 설정(디버그·복원). 게임 인스턴스에도 반영. */
	void SetBattery(int32 NewBattery);

	// ---------------------------------------------------------------- HUD 조회

	int32 GetBattery() const { return Battery; }
	bool IsRequestPending() const { return bRequestPending; }
	const FString& GetDialogue() const { return Dialogue; }
	const FString& GetHeard() const { return Heard; }
	/** 대사가 바뀔 때마다 증가(HUD가 표시 시작 시각을 잡는다). */
	int32 GetDialogueSeq() const { return DialogueSeq; }
	/** 마지막 응답 status(ok / rejected / chat / fallback). */
	const FString& GetDialogueStatus() const { return DialogueStatus; }
	/** 서버가 LLM 대신 mock 키워드 판별로 돌고 있음(HUD 표시용). */
	bool IsServerMock() const { return bServerMock; }
	ECXJeminiAction GetAction() const { return Action; }
	/** 지금 작업 대상(오빠·여동생·적·단말기, 없으면 nullptr) — HUD JEMINI REBOOTING 등. */
	AActor* GetActionTarget() const { return ActionTarget; }
	/** 미끼 점멸 남은 시간(초, 서버 값 — 로컬 협동 HUD). 미끼 중이 아니면 0. */
	float GetDecoyRemaining() const;

	/** 테스트: 마지막 요청/응답 한 줄. */
	FString GetDebugState() const;

	// ---------------------------------------------------------------- 표정(SupportDrone 6장)

	/** 표정을 바꾼다(스크린·발광·추진기·라이트). 서버에서 부르면 복제된다. */
	UFUNCTION(BlueprintCallable, Category = "Jemini|Emotion")
	void SetEmotion(ECXDroneEmotion NewEmotion);

	/** 서버: 잠깐(HoldSeconds) 이 표정을 보인 뒤 기본 표정으로 돌아간다. */
	void ReactEmotion(ECXDroneEmotion NewEmotion, float HoldSeconds = -1.0f);

	UFUNCTION(BlueprintPure, Category = "Jemini|Emotion")
	ECXDroneEmotion GetEmotion() const { return Emotion; }

	/** 감정 색 표(하늘색·초록·파랑·빨강) — 스크린 외 발광·추진기·라이트가 이 색을 쓴다. */
	UFUNCTION(BlueprintPure, Category = "Jemini|Emotion")
	FLinearColor GetEmotionColor(ECXDroneEmotion InEmotion) const;

	/** 단말기 해킹 대행 시간(초) — 여동생이 연 단말기(A)·드론이 날아가 해킹(B) 공통. 적 해킹은 즉시. */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini")
	float AssistTimeTerminal = 3.0f;

	/** 적 대상 범위(cm) — 살아 있는 플레이어 중 누군가로부터. 거미 DetectRadius와 같은 15m. */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini")
	float EnemySearchRange = 1500.0f;

	/** 회복량(살아 있는 사람마다, MaxHealth 클램프). */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini")
	float HealAmount = 50.0f;

	/** 공격력 강화 시간(초, 강화 중이면 이만큼 연장)과 배율. */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini")
	float DamageBoostDuration = 30.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Jemini")
	float DamageBoostMultiplier = 2.0f;

	/** 미끼 지속(초). */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini")
	float DecoyDuration = 5.0f;

	/** 보호막 지속(초). */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini")
	float ShieldDuration = 5.0f;

	/** 빠른 재부팅 빔 연출 시간(초) — 끝나면 부활. */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini")
	float RebootBeamTime = 1.0f;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 지금 표정. 레벨에 놓고 디테일 패널에서 바꾸면 바로 반영(OnConstruction). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Emotion, Category = "Jemini|Emotion", meta = (ExposeOnSpawn = "true"))
	ECXDroneEmotion Emotion = ECXDroneEmotion::Neutral;

	/** Neutral·Happy·Sad·Angry 순서의 감정 색(선형 RGB, 기획 4장 표). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Emotion")
	TArray<FLinearColor> EmotionColors;

	/** 응답 반응 표정을 유지하는 시간(초). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Emotion")
	float EmotionHoldTime = 4.0f;

	/** 배터리가 이 값 이하이면 기본 표정이 슬픔. */
	UPROPERTY(EditAnywhere, Category = "Jemini|Emotion")
	int32 SadBatteryThreshold = 20;

	/** 호버: 위아래 ±HoverAmplitude cm, 주기 HoverPeriod초, 기울기 최대 HoverTiltDeg도. */
	UPROPERTY(EditAnywhere, Category = "Jemini|Visual")
	float HoverAmplitude = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Jemini|Visual")
	float HoverPeriod = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Jemini|Visual")
	float HoverTiltDeg = 3.0f;

	/** 메시 스크린이 액터 +X를 보도록 돌리는 각도(SM_SupportDrone은 스크린이 +Y — 임포트 후 확인). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Visual")
	float MeshYawOffset = -90.0f;

	/** 추진기 빛 기둥 길이(cm)·아래쪽 지름(cm). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Visual")
	float ThrusterLength = 20.0f;

	UPROPERTY(EditAnywhere, Category = "Jemini|Visual")
	float ThrusterWidth = 32.0f;

	UPROPERTY(VisibleAnywhere, Category = "Jemini|Visual")
	TObjectPtr<UStaticMeshComponent> ThrusterBeam;

	UPROPERTY(VisibleAnywhere, Category = "Jemini|Visual")
	TArray<TObjectPtr<UStaticMeshComponent>> ThrusterSparks;

	UFUNCTION()
	void OnRep_Emotion();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Jemini 서버 주소(로컬 / 허깅페이스 Space). */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini|Server")
	FString ServerUrl = TEXT("http://127.0.0.1:8000");

	/** 서버 API_TOKEN과 같은 값(비우면 헤더 없음). */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini|Server")
	FString ApiToken;

	/** HTTP 타임아웃(초) — 서버 LLM 타임아웃보다 길게(허깅페이스 슬립 대비). */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini|Server")
	float RequestTimeout = 12.0f;

	/** 이 맵에서 저장하면 배터리 100(세이브 복도). */
	UPROPERTY(Config, EditAnywhere, Category = "Jemini")
	FString RechargeMapName = TEXT("Map_SaveCorridor");

	/** 오빠에게 날아가는 최대 시간(초) — 넘으면 그 자리에서 재장전 시작. */
	UPROPERTY(EditAnywhere, Category = "Jemini|Flight")
	float MaxFlyTime = 2.0f;

	/** 공격력 강화·보호막 비행 최대 시간(초). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Flight")
	float MaxQuickFlyTime = 1.0f;

	/** 단말기·적·미끼·파트너 위치까지 비행 최대 시간(초) — 넘으면 그 자리에서 실행. */
	UPROPERTY(EditAnywhere, Category = "Jemini|Flight")
	float MaxTravelTime = 3.0f;

	/** 펄스: 적 머리 위 높이(cm, 적 중심 기준). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Flight")
	float PulseHoverHeight = 260.0f;

	/** 미끼: 적 무리 너머 거리(cm)와 높이(cm, 적 중심 기준 — 거미 손이 닿지 않게). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Flight")
	float DecoyOffset = 300.0f;

	UPROPERTY(EditAnywhere, Category = "Jemini|Flight")
	float DecoyHeight = 220.0f;

	/** 미끼 경고음(기존 SFX 재사용, 비면 무음). */
	UPROPERTY(EditDefaultsOnly, Category = "Jemini|Visual")
	TObjectPtr<USoundBase> DecoyBeepSound;

	UPROPERTY(EditAnywhere, Category = "Jemini|Flight")
	float FollowSpeed = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Jemini|Flight")
	float DashSpeed = 1400.0f;

	/** 따라다닐 때 폰 기준 위치(머리 위 약간 뒤). */
	UPROPERTY(EditAnywhere, Category = "Jemini|Flight")
	FVector HoverOffset = FVector(0.0f, -70.0f, 150.0f);

	UPROPERTY(EditAnywhere, Category = "Jemini|Visual")
	TSoftObjectPtr<UStaticMesh> DroneMesh;

	UPROPERTY(VisibleAnywhere, Category = "Jemini|Visual")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Jemini|Visual")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Jemini|Visual")
	TObjectPtr<UPointLightComponent> Light;

private:
	FCXJeminiState GatherState(const APawn* Speaker) const;
	CXJemini::FEndpoint GetEndpoint() const;
	bool BeginRequest(APawn* From, bool bVoice);
	void HandleResponse(const FCXJeminiResponse& Response);
	/** 명령 시작. 대상이 이미 없으면 false(차감 없이 TARGET_LOST). */
	bool ExecuteCommand(const FCXJeminiResponse& Response);
	void SetDialogue(const FString& Text, const FString& InHeard, const FString& Status);
	void FinishReloadFlight();

	/** 서버: 작업 진행(목표 위치 결정 + 효과 시점). 목표가 없으면 bHasGoal = false(따라다니기). */
	void TickAction(float DeltaSeconds, FVector& OutGoal, bool& bHasGoal, bool& bDash);
	/** 효과가 실제로 실행된 순간: 응답의 battery_after 반영(한 번만). */
	void ChargePending();
	/** 실행 직전 대상이 사라짐 → 차감 없이 TARGET_LOST 대사, 플레이어에게 복귀. */
	void FailTargetLost(const FString& Line);
	/** 작업 끝(남은 대기 배터리는 버린다). bReturn이면 플레이어에게 복귀 상태로. */
	void EndAction(bool bReturn = false);
	void SetAction(ECXJeminiAction NewAction);

	FCXJeminiEnemyQuery QueryEnemies() const;
	static bool IsEnemyAlive(const AActor* Enemy);
	static bool IsEnemyStunned(const AActor* Enemy);
	TArray<APawn*> GetLivingPlayers() const;
	/** 진행 순서상 가장 앞의 잠긴 단말기(TerminalName 번호 → 없으면 +Y 좌표가 작은 순). */
	AHackTerminal* FindNextLockedTerminal() const;
	int32 CountLockedTerminals() const;
	APawn* GetFollowTarget() const;

	/** 메시·추진기 배치(스크린 방향 보정, 소켓 위치) + 다이나믹 머티리얼 준비. */
	void SetupVisuals();
	void ApplyEmotionVisuals();
	void TickVisuals(float DeltaSeconds);
	void TickEmotion(float DeltaSeconds);
	ECXDroneEmotion ComputeBaseEmotion() const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScreenMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ThrusterMID;

	/** 서버: 반응 표정 남은 시간(0이면 기본 표정). */
	float EmotionHoldRemaining = 0.0f;
	/** 서버: 디버그로 고정한 표정(true면 기본 표정 계산 안 함). */
	bool bEmotionLocked = false;
	FVector BodyBaseLocation = FVector::ZeroVector;
	FRotator BodyBaseRotation = FRotator::ZeroRotator;
	FTransform ThrusterBase;

public:
	/** 테스트: 표정 고정(Locked) 또는 해제. */
	void DebugLockEmotion(ECXDroneEmotion NewEmotion, bool bLock);

private:
	APawn* FindBrother() const;
	APawn* FindSister() const;
	FVector GetHoverTargetFor(const APawn* Pawn) const;

	UPROPERTY(Replicated)
	int32 Battery = 100;

	UPROPERTY(Replicated)
	bool bRequestPending = false;

	UPROPERTY(Replicated)
	FString Dialogue;

	UPROPERTY(Replicated)
	FString Heard;

	UPROPERTY(Replicated)
	FString DialogueStatus;

	UPROPERTY(Replicated)
	int32 DialogueSeq = 0;

	/** 마지막 응답이 서버 mock 판별로 처리됨(LLM 한도 초과 등). fallback 응답은 값 유지. HUD에 작게 표시. */
	UPROPERTY(Replicated)
	bool bServerMock = false;

	UPROPERTY(Replicated)
	ECXJeminiAction Action = ECXJeminiAction::Idle;

	/** 작업 대상(복제 — HUD). 재장전·강화 = 오빠, 보호막 = 대상, 재부팅 = 쓰러진 사람, 적 해킹·펄스 = 적, 단말기 = 단말기. */
	UPROPERTY(Replicated)
	TObjectPtr<AActor> ActionTarget;

	/** 따라갈 폰(마지막으로 말한 사람, 없으면 오빠). */
	TWeakObjectPtr<APawn> FollowPawn;
	/** 보조 폰(단말기 대행 A = 여동생, 재부팅 = 살아 있는 파트너). */
	TWeakObjectPtr<APawn> ActionPawn;
	/** 이번 요청을 보낸 사람(보호막 기본 대상). */
	TWeakObjectPtr<APawn> RequestSpeaker;
	float ActionElapsed = 0.0f;
	/** 작업 안 단계(펄스: 0 비행 / 1 처치 후 머묾, 적 해킹: 1 = 빔 스폰함). */
	int32 ActionPhase = 0;
	/** 적 해킹 A(여동생 퍼즐 대행): 그로기 확인 → 차감했는지. */
	bool bActionEffectDone = false;
	/** 미끼 위치(공중). */
	FVector ActionLocation = FVector::ZeroVector;
	/** 응답 battery_after — 효과가 실행되면 반영. 음수 = 없음. */
	int32 PendingBatteryAfter = -1;
	/** 지금 작업의 명령 이름(로그). */
	FString ActionCommand;
	/** 마지막 로컬 거절 사유(TARGET_LOST) — 디버그. */
	FString LastLocalReason;
	bool bSnapped = false;
	float BobTime = 0.0f;

	/** 로컬 연출: 미끼 점멸 중이었는지 / 경고음 간격. */
	bool bDecoyVisual = false;
	float DecoyBeepTimer = 0.0f;

	FString LastRequestDebug;
};
