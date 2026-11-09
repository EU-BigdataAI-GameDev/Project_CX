// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HackTerminal.h"
#include "CXHackPuzzles.h"
#include "SisterHackComponent.generated.h"

class UInputAction;
class UInputMappingContext;
class UAudioComponent;
class USoundBase;

/* EHackFeedback는 HackTerminal.h로 옮겼다(메모리 패턴 단말기도 판정 이벤트를 복제한다). */

/**
 *  여동생 전용 해킹 로직(.claude/Plans/해킹 시스템 기획.md).
 *
 *  - 0.2초마다 가장 가까운 해킹 가능 단말기(AHackTerminal::InteractRange 이내)를 FocusTerminal로 잡는다.
 *  - 입력은 에셋 없이 런타임에 만든 Input Action/Mapping Context로 바인딩한다(게임패드 D-pad·B + 키보드 대응).
 *    해킹 시작/제출 키(F/X)는 두 캐릭터 공용 상호작용 키라 UCoopInteractComponent가 받아 Interact()로 넘긴다.
 *  - 해킹 중에는 이동/점프를 잠그고, 단말기 퍼즐(4자리 비밀번호 / 메모리 패턴 11장 / 주파수 동조 12장)을 진행한다.
 *    퍼즐 규칙(판정·상태)은 CXHackPuzzles.h의 구조체 — 이 컴포넌트는 세션·입력·소리·네트워크만 맡는다.
 *  - 메모리 패턴은 서버 권위: 패턴 생성·판정은 단말기(서버)가 하고, 여동생은 커서(3x3)를 움직여 노드를 보낸다
 *    (권위 측이면 직접, 원격이면 Server_MemoryPress → 결과는 Client_MemoryResult).
 *  - 실패해도 무제한 재시도, 언제든 취소 가능(소프트락 없음).
 *  - 적 해킹(해킹 시스템 기획 10장): 범위 안 ICXHackTarget(거미로봇)에 F/X → 화살표 빠른 입력(1라운드 5칸, 4초 제한, 틀리면 같은 배열 처음부터).
 *    퍼즐은 여동생을 조작하는 머신에서 로컬로 진행하고, 성공하면 서버(Server RPC 또는 권위 측 직접 호출)가 대상을 재검증해
 *    그로기(ApplyHackSuccess)를 건다. 세션이 끝나면(성공/실패/취소) EnemyHackCooldown초 동안 적 해킹 불가.
 *
 *  ASisterCharacter에만 붙는다 → 오빠는 단말기에 다가가도 해킹이 시작되지 않고, HUD도 해킹 UI를 그리지 않는다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class USisterHackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USisterHackComponent();

	// ------------------------------------------------------------ 조작(입력 바인딩 + 테스트용 BlueprintCallable)

	/** FocusTerminal 해킹 시작. 해킹 중이면 비밀번호 퍼즐에서는 제출로 동작한다. */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack")
	void Interact();

	/**
	 *  공용 상호작용 키(F/X)를 해킹이 가져갈지 — 해킹 중이거나, 지금 범위 안에 해킹 가능한 단말기가 있으면 true.
	 *  F/X는 UCoopInteractComponent가 바인딩하고 이 값이 참이면 Interact로 넘긴다(게임 클리어 화면 기획 2.1 — 해킹 우선, 그다음 문 버튼).
	 */
	bool WantsInteract();

	UFUNCTION(BlueprintCallable, Category = "Sister|Hack")
	void CancelHack();

	/**
	 *  범위 안 적 해킹 대상(ICXHackTarget — 거미로봇 등) 중 가장 가까운 것. 없으면 nullptr.
	 *  HUD 마커와 같은 거리 기준(대상의 GetHackInteractRange).
	 */
	AActor* FindEnemyHackTarget() const;

	/**
	 *  F/X 마지막 순위: 적 해킹 시작(UCoopInteractComponent가 해킹 세션·단말기·문 버튼이 모두 없을 때 호출).
	 *  범위 안 해킹 가능한 적이 있고 쿨타임이 아니면 화살표 시퀀스 세션을 연다. 시작했으면 true.
	 */
	bool TryHackEnemy();

	/** 적 해킹(화살표): 방향 입력. 비밀번호: 상하 = 숫자 변경, 좌우 = 자리 이동. 메모리 패턴: 3x3 커서. 주파수 동조: 무시. */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack")
	void InputDirection(EHackArrow Direction);

	UFUNCTION(BlueprintCallable, Category = "Sister|Hack")
	void SubmitPasscode();

	// ------------------------------------------------------------ 메모리 패턴(해킹 시스템 기획 11장)

	/** 3x3 커서 위치(노드 0~8, 화면 숫자 = 노드 + 1 — 전화 키패드 배치 1 2 3 / 4 5 6 / 7 8 9). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Memory")
	int32 GetMemoryCursor() const { return MemoryCursor; }

	/** 마지막으로 보낸 노드(HUD 입력 플래시). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Memory")
	int32 GetMemoryLastPressed() const { return MemoryLastPressed; }

	/** 테스트: 커서를 Node(0~8)로 옮기고 확인 입력(F/X)을 누른 것처럼 처리. 처리 후 단말기 상태 문자열. */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack|Debug")
	FString DebugMemoryPress(int32 Node);

	/** 테스트: 정답(true) 또는 오답(false) 노드를 누른다(정답은 단말기 복제 패턴에서 읽음). */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack|Debug")
	FString DebugMemoryPressAnswer(bool bCorrect);

	// ------------------------------------------------------------ HUD 조회

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	AHackTerminal* GetFocusTerminal() const { return FocusTerminal; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	AHackTerminal* GetActiveTerminal() const { return ActiveTerminal; }

	/** 단말기 또는 적 해킹 세션 중. */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	bool IsHacking() const { return ActiveTerminal != nullptr || ActiveEnemy != nullptr; }

	// ------------------------------------------------------------ 적 해킹(화살표 시퀀스) 조회 — HUD / 테스트

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	bool IsEnemyHacking() const { return ActiveEnemy != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	AActor* GetActiveEnemy() const { return ActiveEnemy; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	float GetEnemyHackTimeRemaining() const { return FMath::Max(0.0f, EnemyArrowTimeLimit - EnemyElapsed); }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	float GetEnemyHackTimeLimit() const { return EnemyArrowTimeLimit; }

	// ------------------------------------------------------------ 단말기 주파수 동조(해킹 시스템 기획 12장) 조회 — HUD / 테스트

	/** 커서 위치 0~1(막대 왼쪽 → 오른쪽). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Frequency")
	float GetFreqCursor() const { return FreqLock.Cursor; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Frequency")
	float GetFreqZoneCenter() const { return FreqLock.ZoneCenter; }

	/** 지금 단계의 목표 구간 폭(0~1). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Frequency")
	float GetFreqZoneWidth() const { return FreqLock.GetZoneWidth(); }

	/** 지금까지 연속 적중 수(0 ~ GetFreqStepCount). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Frequency")
	int32 GetFreqStep() const { return FreqLock.Step; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Frequency")
	int32 GetFreqStepCount() const { return FreqLock.GetStepCount(); }

	/** 남은 시간(제한 없음이면 0 — GetFreqTimeLimit이 0이면 표시하지 않는다). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Frequency")
	float GetFreqTimeRemaining() const { return FreqLock.GetTimeRemaining(); }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Frequency")
	float GetFreqTimeLimit() const { return FreqLock.HasTimeLimit() ? FreqLock.TimeLimit : 0.0f; }

	/** 주파수 동조 단말기 세션 중. */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Frequency")
	bool IsFreqLockSession() const;

	/** 결과(성공/실패)가 나서 패널만 잠깐 보여 주는 중. */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	bool IsEnemyHackResolved() const { return bEnemyResolved; }

	/** 적 해킹 쿨타임 남은 시간(초). 0이면 가능. */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	float GetEnemyHackCooldownRemaining() const { return EnemyCooldownRemaining; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	float GetEnemyHackCooldown() const { return EnemyHackCooldown; }

	// ------------------------------------------------------------ 테스트(Python)

	/** 테스트: 범위 안 적 해킹 시작(TryHackEnemy). */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack|Debug")
	bool DebugStartEnemyHack() { return TryHackEnemy(); }

	/** 테스트: 적 해킹 중 정답(true)/오답(false) 방향을 입력한 것처럼 처리(InputDirection 경로). 처리 후 상태 문자열. */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack|Debug")
	FString DebugEnemyArrowPress(bool bCorrect);

	/** 테스트(주파수 동조 단말기 세션): 커서를 목표 구간 안(true)/밖(false)으로 옮긴 뒤 확인 입력(F/X)을 누른 것처럼 처리. 처리 후 상태 문자열. */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack|Debug")
	FString DebugFreqLockPress(bool bInsideZone);

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Debug")
	FString GetFreqLockDebugState() const;

	/** 테스트: 적 해킹 쿨타임 즉시 해제. */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack|Debug")
	void DebugClearEnemyHackCooldown() { EnemyCooldownRemaining = 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Debug")
	FString GetEnemyHackDebugState() const;

	/** 적 해킹 화살표 배열(세션이 없으면 비어 있음). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	const TArray<EHackArrow>& GetArrowSequence() const { return EnemyArrows.Sequence; }

	/** 적 해킹: 다음에 입력할 칸(= 지금까지 맞힌 수). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	int32 GetArrowIndex() const { return EnemyArrows.Index; }

	/** 메모리 패턴 라운드 수(다른 퍼즐은 1). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	int32 GetRoundCount() const;

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	const TArray<int32>& GetPasscodeInput() const { return PasscodeInput; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	int32 GetPasscodeCursor() const { return PasscodeCursor; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	EHackFeedback GetFeedback() const { return Feedback; }

	/** 마지막 판정 이후 경과 시간(초). HUD 플래시/흔들림 연출용. */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	float GetFeedbackAge() const;

	/** 0~1 전체 진행도. */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	float GetProgress() const;

	// ------------------------------------------------------------ 제미니 해킹 대행(.claude/Plans/제미니 AI봇 기획.md B4)

	/**
	 *  제미니 요청 상태: 지금 대행할 수 있는 퍼즐이 진행 중이면 true + "terminal"/"enemy"와 진행도.
	 *  결과가 이미 났거나(성공 표시 중) 세션이 없으면 false → 서버로 null.
	 */
	bool GetJeminiPuzzleState(FString& OutType, float& OutProgress) const;

	/**
	 *  서버: 제미니가 Duration초 뒤 지금 퍼즐을 대신 성공시킨다. 그동안 적 해킹 제한 시간은 멈춘다.
	 *  여동생이 먼저 풀거나 취소하면 대행도 끝난다(배터리는 이미 차감됨). 원격 여동생이면 그 머신에서 진행.
	 */
	void BeginJeminiAssist(float Duration);

	bool IsJeminiAssisting() const { return JeminiAssistRemaining > 0.0f; }

	/** HUD: 대행 진행도 0~1. */
	float GetJeminiAssistProgress() const { return JeminiAssistDuration > 0.0f ? FMath::Clamp(1.0f - JeminiAssistRemaining / JeminiAssistDuration, 0.0f, 1.0f) : 0.0f; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 이 거리(cm) 밖으로 나가면 FocusTerminal을 놓는다(단말기 InteractRange와 함께 사용, 더 작은 값 적용). */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack")
	float MaxDetectRange = 300.0f;

	/** 해킹 성공 표시를 보여준 뒤 세션을 닫기까지의 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack")
	float GrantedHoldTime = 1.2f;

	// ------------------------------------------------------------ 적 해킹: 화살표 빠른 입력(해킹 시스템 기획 10장)

	/** 화살표 칸 수(1라운드, 1~7). */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy", meta = (ClampMin = "1", ClampMax = "7"))
	int32 EnemyArrowCount = 5;

	/** 제한 시간(초). 넘으면 실패 — 그로기 없음. 틀려도 시간은 계속 흐른다(같은 배열 처음부터). */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy", meta = (ClampMin = "0.5"))
	float EnemyArrowTimeLimit = 4.0f;

	/** 적 해킹 세션이 끝난 뒤(성공/실패/취소) 다시 적을 해킹할 수 있을 때까지(초). 단말기 해킹에는 적용하지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy")
	float EnemyHackCooldown = 3.0f;

	/** 성공/실패 표시를 보여 준 뒤 패널을 닫기까지(초) — 전투 중이라 짧게. 이동 잠금은 결과 즉시 풀린다. */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy")
	float EnemyResultHoldTime = 0.6f;

	/** 서버 재검증 거리 여유(cm): 대상 해킹 거리 + 이 값 안이어야 그로기 승인(퍼즐 중 대상이 조금 움직여도 됨). */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy")
	float EnemyHackServerRangeSlack = 300.0f;

	// ------------------------------------------------------------ 사운드(효과음 적용 기획 2.3 — 비어 있으면 무음)
	// 해킹 UI 소리는 여동생을 조작하는 로컬 머신에서만 2D로 재생한다. 사이버/신스 계열로 통일해 총·발소리와 구분.
	// 원샷은 동시에 1개(새 소리가 나면 이전 것 정지 — 기획 5.4 해킹 UI).

	/** 해킹 중 루프. 세션 종료 시 HackLoopFadeOut초 페이드아웃. */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	TObjectPtr<USoundBase> HackLoopSound;

	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	float HackLoopFadeOut = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	TObjectPtr<USoundBase> HackStartSound;

	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	TObjectPtr<USoundBase> HackCancelSound;

	/** 화살표 정답 입력. 순서마다 피치를 CorrectPitchStep씩 올린다. */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	TObjectPtr<USoundBase> CorrectSound;

	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	float CorrectPitchStep = 0.06f;

	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	TObjectPtr<USoundBase> RoundClearSound;

	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	TObjectPtr<USoundBase> DeniedSound;

	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	TObjectPtr<USoundBase> GrantedSound;

	/** 비밀번호 퍼즐 숫자 변경 / 자리 이동. */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	TObjectPtr<USoundBase> DigitTickSound;

	/** 해킹 가능한 단말기가 범위 안에 들어온 순간(TERMINAL ONLINE). */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Sound")
	TObjectPtr<USoundBase> DetectSound;

private:
	void UpdateFocus();
	void TryBindInput();
	void BuildInputAssets();

	void SetFeedback(EHackFeedback NewFeedback);
	void FinishSuccess();
	void EndSession();
	void SetMovementLocked(bool bLocked);

	// 적 해킹(화살표 시퀀스)
	void StartEnemyHack(AActor* Target);
	void TickEnemyHack(float DeltaTime);
	void InputEnemyArrow(EHackArrow Direction);
	void ResolveEnemyHack(bool bSuccess);
	void EndEnemySession();
	/** 서버(권위 측): 대상 재검증 후 그로기 적용. */
	bool ApplyEnemyHackOnServer(AActor* Target);

	UFUNCTION(Server, Reliable)
	void Server_ApplyEnemyHack(AActor* Target);

	// 단말기 주파수 동조
	void TickFreqLock(float DeltaTime);
	void ConfirmFreqLock();

	bool IsLocallyControlledOwner() const;
	void PlayHackSound(USoundBase* Sound, float Pitch = 1.0f);
	void StartHackLoop();
	void StopHackLoop();

	// 메모리 패턴
	bool IsMemorySession() const;
	void PressMemoryNode(int32 Node);
	void HandleMemoryResult(EHackFeedback Result);
	void SendMemorySession(bool bBegin);

	UFUNCTION(Server, Reliable)
	void Server_MemorySession(AHackTerminal* Terminal, bool bBegin);

	UFUNCTION(Server, Reliable)
	void Server_MemoryPress(AHackTerminal* Terminal, int32 Node);

	UFUNCTION(Client, Reliable)
	void Client_MemoryResult(EHackFeedback Result);

	// 제미니 해킹 대행
	void StartJeminiAssistLocal(float Duration);
	void TickJeminiAssist(float DeltaTime);
	void CompleteJeminiAssist();

	UFUNCTION(Client, Reliable)
	void Client_BeginJeminiAssist(float Duration);

	float JeminiAssistRemaining = 0.0f;
	float JeminiAssistDuration = 0.0f;

	void OnInputUp() { InputDirection(EHackArrow::Up); }
	void OnInputDown() { InputDirection(EHackArrow::Down); }
	void OnInputLeft() { InputDirection(EHackArrow::Left); }
	void OnInputRight() { InputDirection(EHackArrow::Right); }

	UPROPERTY(Transient)
	TObjectPtr<AHackTerminal> FocusTerminal;

	UPROPERTY(Transient)
	TObjectPtr<AHackTerminal> ActiveTerminal;

	/** 적 해킹 세션 대상(ICXHackTarget). */
	UPROPERTY(Transient)
	TObjectPtr<AActor> ActiveEnemy;

	/** 적 해킹 화살표 퍼즐 + 경과 시간. */
	FCXArrowSequencePuzzle EnemyArrows;
	float EnemyElapsed = 0.0f;
	/** 단말기 주파수 동조 퍼즐(설정은 세션 시작 때 단말기에서 복사). */
	FCXFreqLockPuzzle FreqLock;
	bool bEnemyResolved = false;
	float EnemyCooldownRemaining = 0.0f;
	/** 서버: 마지막으로 승인한 적 해킹 시각(쿨타임 재검증). */
	float LastServerEnemyHackTime = -1000.0f;
	bool bMovementLocked = false;
	FTimerHandle EnemyResultTimer;

	int32 MemoryCursor = 4;
	int32 MemoryLastPressed = INDEX_NONE;
	/** 이번 라운드에서 맞힌 수(로컬 — 정답음 피치용). */
	int32 MemoryLocalCorrect = 0;

	TArray<int32> PasscodeInput;
	int32 PasscodeCursor = 0;

	EHackFeedback Feedback = EHackFeedback::None;
	float FeedbackTime = -100.0f;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> HackMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CancelAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> UpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DownAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LeftAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RightAction;

	bool bInputBound = false;
	float FocusTimer = 0.0f;
	FTimerHandle GrantedTimer;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> HackLoopAudio;

	TWeakObjectPtr<UAudioComponent> LastHackOneShot;

	/** 감지음 중복 방지 — 마지막으로 감지음을 낸 단말기(범위를 벗어나면 비움). */
	TWeakObjectPtr<AHackTerminal> LastDetectedTerminal;
};
