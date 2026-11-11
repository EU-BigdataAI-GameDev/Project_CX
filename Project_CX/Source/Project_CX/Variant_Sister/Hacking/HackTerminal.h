// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXCircuitLinkPuzzle.h"
#include "HackTerminal.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UBoxComponent;
class UAudioComponent;
class USoundBase;

/**
 *  단말기가 요구하는 퍼즐 종류(.claude/Plans/해킹 시스템 기획.md 0-1장 퍼즐 배정).
 *  값 순서는 맵에 저장된 숫자와 맞물려 있으므로 바꾸지 말고 뒤에만 추가한다.
 */
UENUM(BlueprintType)
enum class EHackPuzzleType : uint8
{
	/**
	 *  (단말기에서 폐기) 화살표 시퀀스는 이제 적 해킹 전용(해킹 시스템 기획 3장·10장).
	 *  호환용으로 값만 남기고 에디터 목록에서는 숨긴다. 이 값으로 남은 단말기는 BeginPlay에서 경고 후 FrequencyLock으로 동작한다.
	 */
	ArrowSequence UMETA(Hidden, DisplayName = "Arrow Sequence (legacy - enemy hack only)"),
	/** 4자리 비밀번호 입력. 숫자 단서(AHackCodeClue)는 오빠 화면에만 보인다. */
	Passcode,
	/**
	 *  메모리 패턴(Ghost Echo, 해킹 시스템 기획 11장): 3x3 노드(1~9) 점멸 순서가 오빠 화면에만 재생되고,
	 *  여동생이 같은 순서로 노드를 고른다. 오빠가 단말기 중계 거리(MemoryRelayRange) 안에 있어야 재생된다.
	 */
	MemoryPattern,
	/**
	 *  주파수 동조(Frequency Lock, 해킹 시스템 기획 12장): 왕복 커서가 목표 구간 안일 때 F/X — 단계마다 구간이 좁아지고 빨라진다.
	 *  (원래 적 해킹 퍼즐 → 단말기로 이동. 단말기는 전투 압박이 없어 기본 제한 시간 없음.)
	 */
	FrequencyLock,
	/**
	 *  회로 연결(Circuit Link, 맵2 개선 기획 3장): 격자 위 시작점(파랑)과 짝 도착점(초록)을 교차 없이 상하좌우 회로로 잇는다.
	 *  여동생 화면은 짝을 모른다(시작점 모두 파랑, 도착점 모두 초록). 오빠가 중계 거리(CircuitRelayRange) 안이면 오빠 칸에 짝 색·번호가 보인다.
	 *  틀린 짝은 MISMATCH(빨강, 성공 아님). 제한 시간·실패 없음. 서버 권위(메모리 패턴과 같은 세션 방식).
	 */
	CircuitLink
};

/** 회로 연결 레이아웃 하나(단말기 설정). Starts[i]와 Goals[i]가 짝 — 칸 좌표(X 0..Width-1 왼쪽→오른쪽, Y 0..Height-1 위→아래). */
USTRUCT(BlueprintType)
struct FCXCircuitLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack|Circuit", meta = (ClampMin = "2", ClampMax = "12"))
	int32 Width = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack|Circuit", meta = (ClampMin = "2", ClampMax = "12"))
	int32 Height = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack|Circuit")
	TArray<FIntPoint> Starts;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hack|Circuit")
	TArray<FIntPoint> Goals;
};

/** 회로 하나의 복제 사본(칸 번호 = Y * Width + X). Origin/End = 끝점 번호(짝 = 번호 / 2, 짝수 = 시작점), 255 = 없음. */
USTRUCT()
struct FCXCircuitPathRep
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<uint8> Cells;

	UPROPERTY()
	uint8 Origin = 255;

	UPROPERTY()
	uint8 End = 255;

	bool IsOpen() const { return End == 255; }
	bool IsLinked() const { return End != 255 && Origin / 2 == End / 2; }
	bool IsMismatch() const { return End != 255 && Origin / 2 != End / 2; }
};

/** HUD 연출용 최근 판정 결과(여동생 해킹 세션 / 메모리 패턴 단말기 이벤트 공용). */
UENUM(BlueprintType)
enum class EHackFeedback : uint8
{
	None,
	Correct,
	RoundClear,
	Denied,
	Granted,
	/** 적 해킹(화살표 시퀀스) 시간 초과 / 대상 상실 — 세션 실패. */
	Failed
};

UENUM(BlueprintType)
enum class EHackArrow : uint8
{
	Up,
	Down,
	Left,
	Right
};

UENUM(BlueprintType)
enum class EHackTerminalState : uint8
{
	Locked,
	InProgress,
	Hacked
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackTerminalHacked, class AHackTerminal*, Terminal);

/**
 *  여동생만 해킹할 수 있는 월드 단말기.
 *
 *  퍼즐 진행(입력 판정)은 여동생의 USisterHackComponent가 하고, 단말기는 퍼즐 설정과 상태(복제)만 가진다.
 *  해킹에 성공하면 UnlockActors를 숨기고 충돌을 끄며(레이저 게이트 등), OnHacked를 브로드캐스트한다.
 */
UCLASS()
class AHackTerminal : public AActor
{
	GENERATED_BODY()

public:
	AHackTerminal();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** HUD/로그에 표시되는 이름. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack")
	FString TerminalName = TEXT("TERMINAL_01");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack")
	EHackPuzzleType PuzzleType = EHackPuzzleType::FrequencyLock;

	// ------------------------------------------------------------ 주파수 동조(해킹 시스템 기획 12장) — 숫자는 원래 적 해킹 값 그대로

	/** 단계별 목표 구간 폭(막대 대비 0~1). 원소 수 = 필요한 연속 적중 수. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Frequency", meta = (EditCondition = "PuzzleType == EHackPuzzleType::FrequencyLock"))
	TArray<float> FreqZoneWidths = { 0.34f, 0.28f, 0.22f };

	/** 단계별 커서 속도(초당 막대 길이). 왕복하므로 한 번 건너는 시간 = 1 / 속도. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Frequency", meta = (EditCondition = "PuzzleType == EHackPuzzleType::FrequencyLock"))
	TArray<float> FreqCursorSpeeds = { 0.9f, 1.1f, 1.35f };

	/**
	 *  제한 시간(초). 0이면 없음(기본 — 단말기는 전투 압박이 없다). 0보다 크면 시간이 다 됐을 때 실패가 아니라
	 *  1단계부터 다시(단말기 퍼즐은 무제한 재시도 원칙).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Frequency", meta = (EditCondition = "PuzzleType == EHackPuzzleType::FrequencyLock", ClampMin = "0.0"))
	float FreqTimeLimit = 0.0f;

	/** 빗나간 뒤 입력 무시(초) — 연타 방지. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Frequency", meta = (EditCondition = "PuzzleType == EHackPuzzleType::FrequencyLock", ClampMin = "0.0"))
	float FreqMissLockout = 0.25f;

	/** 켜져 있으면 BeginPlay에서 4자리 비밀번호를 랜덤으로 만든다(매 플레이 다른 답). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Passcode", meta = (EditCondition = "PuzzleType == EHackPuzzleType::Passcode"))
	bool bRandomizePasscode = true;

	/** 비밀번호(숫자 4자리). bRandomizePasscode가 켜져 있으면 런타임에 덮어쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Hack|Passcode", meta = (EditCondition = "PuzzleType == EHackPuzzleType::Passcode"))
	FString Passcode = TEXT("0000");

	// ------------------------------------------------------------ 메모리 패턴(해킹 시스템 기획 11장)

	/** 라운드별 패턴 길이(각 1~9). 다음 라운드는 이전 패턴 뒤에 노드를 덧붙인다(사이먼식). 바로 앞과 같은 노드는 연속으로 나오지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Memory", meta = (EditCondition = "PuzzleType == EHackPuzzleType::MemoryPattern"))
	TArray<int32> MemoryRoundLengths = { 3, 4, 5 };

	/** 오빠가 단말기에서 이 거리(cm) 안에 살아 있어야 오빠 화면에 패턴이 재생된다(밖이면 MOVE CLOSER, 패턴 대기). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Memory", meta = (EditCondition = "PuzzleType == EHackPuzzleType::MemoryPattern"))
	float MemoryRelayRange = 900.0f;

	/** 노드 하나가 켜져 있는 시간(초). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Memory", meta = (EditCondition = "PuzzleType == EHackPuzzleType::MemoryPattern"))
	float MemoryFlashOnTime = 0.6f;

	/** 노드 사이 꺼진 시간(초). 같은 노드가 연속으로 나오지 않으므로 짧아도 구분된다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Memory", meta = (EditCondition = "PuzzleType == EHackPuzzleType::MemoryPattern"))
	float MemoryFlashGapTime = 0.3f;

	/** 패턴 한 바퀴가 끝난 뒤 다시 처음부터 재생하기까지 쉬는 시간(초). 라운드가 끝날 때까지 반복 재생한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Memory", meta = (EditCondition = "PuzzleType == EHackPuzzleType::MemoryPattern"))
	float MemoryLoopPause = 1.6f;

	/** 재생 시작 전 준비 시간(초) — 중계 연결 / 새 라운드 직후 오빠가 패널을 볼 시간. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Memory", meta = (EditCondition = "PuzzleType == EHackPuzzleType::MemoryPattern"))
	float MemoryLeadIn = 0.8f;

	/** 오답 뒤 같은 패턴을 처음부터 다시 재생하기까지(초). 긴 벌칙 없음. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Memory", meta = (EditCondition = "PuzzleType == EHackPuzzleType::MemoryPattern"))
	float MemoryDeniedReplayDelay = 1.0f;

	/** 서버 검증: 입력한 여동생이 InteractRange + 이 값(cm) 안이어야 입력을 받는다. */
	UPROPERTY(EditAnywhere, Category = "Hack|Memory", meta = (EditCondition = "PuzzleType == EHackPuzzleType::MemoryPattern"))
	float MemoryServerRangeSlack = 200.0f;

	// ------------------------------------------------------------ 회로 연결(맵2 개선 기획 3장)

	/**
	 *  레이아웃 후보. 단말기를 열 때마다 하나를 고른다(직전과 다른 것 우선). 비어 있으면 코드 기본 레이아웃(6x5, 4종).
	 *  열 때 답이 있는지 검사(Solve)하고, 틀린 레이아웃은 경고 로그 후 빼며, 남는 게 없으면 기본 레이아웃 0번을 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Circuit", meta = (EditCondition = "PuzzleType == EHackPuzzleType::CircuitLink"))
	TArray<FCXCircuitLayout> CircuitLayouts;

	/** 오빠가 단말기에서 이 거리(cm) 안에 살아 있으면 오빠 칸에 짝 정보(색·번호) 격자가 뜬다. 밖이어도 여동생 입력은 막지 않는다(시행착오 가능). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Circuit", meta = (EditCondition = "PuzzleType == EHackPuzzleType::CircuitLink"))
	float CircuitRelayRange = 900.0f;

	/** 서버 검증: 입력한 여동생이 InteractRange + 이 값(cm) 안이어야 입력을 받는다. */
	UPROPERTY(EditAnywhere, Category = "Hack|Circuit", meta = (EditCondition = "PuzzleType == EHackPuzzleType::CircuitLink"))
	float CircuitServerRangeSlack = 200.0f;

	/** 여동생이 이 거리(cm) 안에 있어야 해킹을 시작할 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack")
	float InteractRange = 300.0f;

	/** 해킹 성공 시 숨기고 충돌을 끌 액터들(레이저 게이트, 문 등). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack")
	TArray<TObjectPtr<AActor>> UnlockActors;

	/** 기능 B(WARDEN 설득): 해킹 성공 보상 증거 id("E2"/"E3", 비우면 없음) — ACXWardenTerminal::AwardEvidence. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Evidence")
	FName RewardEvidenceId;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackTerminalHacked OnHacked;

	UFUNCTION(BlueprintPure, Category = "Hack")
	EHackTerminalState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Hack")
	bool CanBeHacked() const { return State == EHackTerminalState::Locked; }

	/** 비밀번호 i번째(0~3) 숫자. 단서 액터가 표시용으로 읽는다. */
	int32 GetPasscodeDigit(int32 Index) const;

	/** 해킹 세션 시작/취소/성공 — 권위 측에서 호출된다. */
	void SetInProgress(bool bInProgress);
	void MarkHacked();

	// ------------------------------------------------------------ 메모리 패턴: 서버 권위 API(USisterHackComponent가 직접/Server RPC로 호출)

	/** 서버: 세션 시작 — 1라운드 패턴 생성, InProgress. 오빠가 중계 거리 안에 들어오면 Tick이 재생을 시작한다. */
	void ServerMemoryBegin(APawn* Hacker);

	/** 서버: 노드(0~8) 입력 판정. Correct / RoundClear / Denied / Granted, 받지 않은 입력은 None. */
	EHackFeedback ServerMemoryPress(APawn* Hacker, int32 Node);

	/** 서버: 세션 취소(해킹되지 않았으면 Locked 복귀, 패턴 삭제). */
	void ServerMemoryEnd(APawn* Hacker);

	// ------------------------------------------------------------ 회로 연결: 서버 권위 API(USisterHackComponent가 직접/Server RPC로 호출)

	/** 서버: 세션 시작 — 레이아웃 고르기(답 검사), 회로 비우기, InProgress. */
	void ServerCircuitBegin(APawn* Hacker);

	/** 서버: 입력 하나 판정(커서 이동 / 확인). 마지막 짝까지 맞으면 Complete + MarkHacked. 받지 않은 입력은 Ignored. */
	ECXCircuitStep ServerCircuitInput(APawn* Hacker, ECXCircuitOp Op);

	/** 서버: 세션 취소(해킹되지 않았으면 Locked 복귀, 회로 삭제). */
	void ServerCircuitEnd(APawn* Hacker);

	/** 서버: 정답 회로로 채우고 해킹 완료(CXSolveHack / 제미니 대행). 세션이 없으면 MarkHacked만. */
	void ServerCircuitSolve();

	/** 서버: 세션을 연 여동생(제미니 대행 — 원격 여동생 세션 찾기). */
	APawn* GetCircuitHacker() const { return CircuitHacker.Get(); }

	// ------------------------------------------------------------ 회로 연결: 조회(HUD — 복제된 값만 사용)

	bool IsCircuitLink() const { return PuzzleType == EHackPuzzleType::CircuitLink; }

	/** 회로 연결 세션 진행 중(InProgress + 레이아웃 있음). */
	UFUNCTION(BlueprintPure, Category = "Hack|Circuit")
	bool IsCircuitSessionActive() const;

	int32 GetCircuitWidth() const { return CircuitWidth; }
	int32 GetCircuitHeight() const { return CircuitHeight; }
	/** 끝점 칸 번호(짝마다 시작점, 도착점 순). */
	const TArray<uint8>& GetCircuitEndpoints() const { return CircuitEndpoints; }
	const TArray<FCXCircuitPathRep>& GetCircuitPaths() const { return CircuitPaths; }
	/** 지금 그리는 회로(GetCircuitPaths 인덱스), 없으면 -1. */
	int32 GetCircuitActivePath() const { return CircuitActivePath; }
	int32 GetCircuitCursor() const { return CircuitCursor; }

	UFUNCTION(BlueprintPure, Category = "Hack|Circuit")
	int32 GetCircuitPairCount() const { return CircuitEndpoints.Num() / 2; }

	/** 맞는 짝으로 연결된 수(제미니 progress = 이 값 / 짝 수). */
	UFUNCTION(BlueprintPure, Category = "Hack|Circuit")
	int32 GetCircuitLinkedCount() const;

	/** 그 칸의 끝점 번호 / 지나는 회로 인덱스(없으면 -1) — 복제값 기준. */
	int32 GetCircuitEndpointAt(int32 Cell) const;
	int32 GetCircuitPathAt(int32 Cell) const;

	/** 오빠가 중계 거리 안에 살아 있음(서버 판정, 복제) → 오빠 칸에 짝 색·번호. */
	UFUNCTION(BlueprintPure, Category = "Hack|Circuit")
	bool IsCircuitRelayActive() const { return bCircuitRelayActive; }

	/** 마지막 판정 이벤트(Correct = 짝 연결, Denied = MISMATCH, Granted = 완료 — 복제)와 경과 시간. */
	EHackFeedback GetCircuitLastEvent() const { return CircuitLastEvent; }
	float GetCircuitEventAge() const { return GetSyncedTime() - CircuitEventTime; }

	/** 이번 세션 레이아웃 번호(기본 레이아웃 기준이면 0~, 단말기 설정이면 100~). 테스트·로그용. */
	UFUNCTION(BlueprintPure, Category = "Hack|Circuit|Debug")
	int32 GetCircuitLayoutId() const { return CircuitLayoutId; }

	/** 테스트: 서버 퍼즐 상태 한 줄(끝점·회로·커서·중계). */
	UFUNCTION(BlueprintPure, Category = "Hack|Circuit|Debug")
	FString GetCircuitDebugState() const;

	/** 테스트(서버): 지금 판에서 From → To 칸 최단 경로(다른 회로·끝점 피함). 없으면 빈 배열. */
	TArray<int32> DebugCircuitRoute(int32 FromCell, int32 ToCell) const;

	/** 테스트(서버): 빈 판 기준 정답 회로(짝 순서, 칸 번호를 이어 붙이고 짝 사이는 -1). */
	TArray<int32> DebugCircuitSolution() const;

	// ------------------------------------------------------------ 메모리 패턴: 조회(HUD — 복제된 값만 사용)

	bool IsMemoryPattern() const { return PuzzleType == EHackPuzzleType::MemoryPattern; }

	/** 메모리 패턴 세션 진행 중(InProgress + 패턴 있음). */
	UFUNCTION(BlueprintPure, Category = "Hack|Memory")
	bool IsMemorySessionActive() const;

	/** 지금 라운드 패턴(노드 0~8, 화면 숫자 = 노드 + 1). */
	const TArray<uint8>& GetMemorySequence() const { return MemorySequence; }

	UFUNCTION(BlueprintPure, Category = "Hack|Memory")
	int32 GetMemoryRound() const { return MemoryRound; }

	UFUNCTION(BlueprintPure, Category = "Hack|Memory")
	int32 GetMemoryRoundCount() const { return FMath::Max(1, MemoryRoundLengths.Num()); }

	/** 여동생이 이번 라운드에서 맞힌 노드 수. */
	UFUNCTION(BlueprintPure, Category = "Hack|Memory")
	int32 GetMemoryInputIndex() const { return MemoryInputIndex; }

	/** 오빠가 중계 거리 안에 살아 있어 패턴이 재생되는 중(서버 판정, 복제). */
	UFUNCTION(BlueprintPure, Category = "Hack|Memory")
	bool IsMemoryRelayActive() const { return bMemoryRelayActive; }

	/**
	 *  지금 켜져 있는 노드(0~8, 없으면 -1). 서버 월드 시각 기준이라 모든 머신이 같은 순간에 같은 노드를 본다.
	 *  OutStep = 지금 보여 주는 패턴 순번(0부터, 대기·쉼 중이면 -1), OutAlpha = 켜짐 세기(0~1),
	 *  OutWait = 다음 재생(준비 / 반복 쉼)까지 남은 초(재생 중이면 0).
	 */
	int32 GetMemoryFlashNode(int32& OutStep, float& OutAlpha, float& OutWait) const;

	/** 마지막 판정 이벤트(오빠 칸 연출용 — 복제)와 그 뒤 경과 시간. */
	EHackFeedback GetMemoryLastEvent() const { return MemoryLastEvent; }
	float GetMemoryEventAge() const { return GetSyncedTime() - MemoryEventTime; }

	/** 중계 역할(오빠) 폰 — 플레이어 폰 중 UCoopInteractComponent::GetPawnRole == Brother. 없으면 nullptr. */
	APawn* FindRelayPawn() const;

	/** 서버 월드 시각(GameState 동기화 — 클라이언트도 서버와 같은 시계). */
	float GetSyncedTime() const;

	/** 테스트: 다음에 눌러야 할 노드(0~8, 세션이 없으면 -1). */
	UFUNCTION(BlueprintPure, Category = "Hack|Memory|Debug")
	int32 GetMemoryExpectedNode() const;

	UFUNCTION(BlueprintPure, Category = "Hack|Memory|Debug")
	FString GetMemoryDebugState() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 메시가 바뀌거나 크기가 조절될 때마다 충돌 박스를 메시 바운드에 맞춘다. */
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION()
	void OnRep_State();

	void ApplyStateVisuals();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hack")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hack")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 캐릭터가 단말기를 뚫고 지나가지 않게 하는 충돌. 메시 자체 충돌은 에셋마다 품질이 달라 쓰지 않는다(프로젝트 COL 박스 방식과 동일). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hack")
	TObjectPtr<UBoxComponent> Collision;

	/** 상태등: Locked 빨강 / InProgress 노랑 깜빡임 / Hacked 초록. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hack")
	TObjectPtr<UPointLightComponent> StatusLight;

	/** 대기 웅웅거림 루프(SW_Terminal_Hum, 3D·ATT_Small). 해킹 중엔 피치를 조금 올리고, 해킹되면 페이드아웃. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hack|Sound")
	TObjectPtr<UAudioComponent> HumAudio;

	/** 해킹 중 웅웅거림 피치(진행 중임을 소리로도). */
	UPROPERTY(EditAnywhere, Category = "Hack|Sound")
	float HumPitchInProgress = 1.15f;

	/**
	 *  레이저 게이트 꺼짐 — 상태가 InProgress → Hacked로 바뀌는 순간 UnlockActors 중심에서 1회(복제되는 State라 모든 머신).
	 *  레벨 시작/접속 시 이미 Hacked로 받은 경우는 재생하지 않는다(효과음 적용 기획 6.1). 비어 있으면 무음.
	 */
	UPROPERTY(EditAnywhere, Category = "Hack|Sound")
	TObjectPtr<USoundBase> GateOffSound;

	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "Hack")
	EHackTerminalState State = EHackTerminalState::Locked;

	// ------------------------------------------------------------ 메모리 패턴 복제 상태(서버가 쓰고 오빠 HUD가 읽는다)

	UPROPERTY(Replicated)
	TArray<uint8> MemorySequence;

	UPROPERTY(Replicated)
	int32 MemoryRound = 0;

	UPROPERTY(Replicated)
	int32 MemoryInputIndex = 0;

	UPROPERTY(Replicated)
	bool bMemoryRelayActive = false;

	/** 패턴 1회차 재생 시작 시각(서버 월드 시각). 이후 (켜짐+꺼짐)×길이 + 쉼 주기로 반복. */
	UPROPERTY(Replicated)
	float MemoryPlaybackStart = 0.0f;

	UPROPERTY(Replicated)
	EHackFeedback MemoryLastEvent = EHackFeedback::None;

	UPROPERTY(Replicated)
	float MemoryEventTime = -100.0f;

	// ------------------------------------------------------------ 회로 연결 복제 상태(서버가 쓰고 두 HUD가 읽는다)

	UPROPERTY(Replicated)
	uint8 CircuitWidth = 0;

	UPROPERTY(Replicated)
	uint8 CircuitHeight = 0;

	UPROPERTY(Replicated)
	TArray<uint8> CircuitEndpoints;

	UPROPERTY(Replicated)
	TArray<FCXCircuitPathRep> CircuitPaths;

	UPROPERTY(Replicated)
	int32 CircuitActivePath = INDEX_NONE;

	UPROPERTY(Replicated)
	int32 CircuitCursor = 0;

	UPROPERTY(Replicated)
	bool bCircuitRelayActive = false;

	UPROPERTY(Replicated)
	EHackFeedback CircuitLastEvent = EHackFeedback::None;

	UPROPERTY(Replicated)
	float CircuitEventTime = -100.0f;

	UPROPERTY(Replicated)
	int32 CircuitLayoutId = INDEX_NONE;

private:
	void UpdateStateSounds();

	/** 서버: 회로 퍼즐 → 복제 사본. */
	void SyncCircuitRep();
	void SetCircuitEvent(EHackFeedback Event);
	/** 서버: 중계 상태(오빠 거리·생존) 갱신. */
	void UpdateCircuitRelay();
	/** 서버: 레이아웃 후보 중 하나를 골라 Circuit에 깐다(답 검사, 실패 시 기본 0번). */
	void PickCircuitLayout();

	/** 서버 전용 퍼즐 상태(복제는 Circuit* 사본으로). */
	FCXCircuitLinkPuzzle Circuit;
	TWeakObjectPtr<APawn> CircuitHacker;
	/** 직전 세션 레이아웃(연속으로 같은 판이 나오지 않게). */
	int32 LastCircuitLayoutId = INDEX_NONE;

	/** 서버: 중계 상태(오빠 거리·생존) 갱신. 새로 연결되면 준비 시간 뒤 처음부터 재생. */
	void UpdateMemoryRelay();
	/** 서버: 패턴을 Length까지 늘린다(이미 길면 새로 만든다). 바로 앞 노드와 같은 노드는 피한다. */
	void ExtendMemorySequence(int32 Length);
	void SetMemoryEvent(EHackFeedback Event);
	/** 서버: Delay초 뒤 처음부터 재생(중계 중일 때만 의미 있음). */
	void RestartMemoryPlayback(float Delay);

	/** 서버: 세션을 연 여동생(입력 검증). */
	TWeakObjectPtr<APawn> MemoryHacker;

	/** 마지막으로 비주얼/소리에 반영한 상태(전환 감지용). */
	EHackTerminalState AppliedState = EHackTerminalState::Locked;
	bool bStateApplied = false;
};
