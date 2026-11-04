// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HackTerminal.h"
#include "SisterHackComponent.generated.h"

class UInputAction;
class UInputMappingContext;
class UAudioComponent;
class USoundBase;

/** HUD 연출용 최근 판정 결과. */
UENUM(BlueprintType)
enum class EHackFeedback : uint8
{
	None,
	Correct,
	RoundClear,
	Denied,
	Granted,
	/** 적 해킹(주파수 동조) 시간 초과 / 대상 상실 — 세션 실패. */
	Failed
};

/**
 *  여동생 전용 해킹 로직(.claude/Plans/해킹 시스템 기획.md).
 *
 *  - 0.2초마다 가장 가까운 해킹 가능 단말기(AHackTerminal::InteractRange 이내)를 FocusTerminal로 잡는다.
 *  - 입력은 에셋 없이 런타임에 만든 Input Action/Mapping Context로 바인딩한다(게임패드 D-pad·B + 키보드 대응).
 *    해킹 시작/제출 키(F/X)는 두 캐릭터 공용 상호작용 키라 UCoopInteractComponent가 받아 Interact()로 넘긴다.
 *  - 해킹 중에는 이동/점프를 잠그고, 화살표 퍼즐 또는 4자리 비밀번호 퍼즐을 진행한다.
 *  - 실패해도 무제한 재시도, 언제든 취소 가능(소프트락 없음).
 *  - 적 해킹(해킹 시스템 기획 10장): 범위 안 ICXHackTarget(거미로봇)에 F/X → "주파수 동조" 반사 퍼즐(3연속 적중, 4초 제한).
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
	 *  범위 안 해킹 가능한 적이 있고 쿨타임이 아니면 주파수 동조 세션을 연다. 시작했으면 true.
	 */
	bool TryHackEnemy();

	/** 화살표 퍼즐: 방향 입력. 비밀번호 퍼즐: 상하 = 숫자 변경, 좌우 = 자리 이동. */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack")
	void InputDirection(EHackArrow Direction);

	UFUNCTION(BlueprintCallable, Category = "Sister|Hack")
	void SubmitPasscode();

	// ------------------------------------------------------------ HUD 조회

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	AHackTerminal* GetFocusTerminal() const { return FocusTerminal; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	AHackTerminal* GetActiveTerminal() const { return ActiveTerminal; }

	/** 단말기 또는 적 해킹 세션 중. */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	bool IsHacking() const { return ActiveTerminal != nullptr || ActiveEnemy != nullptr; }

	// ------------------------------------------------------------ 적 해킹(주파수 동조) 조회 — HUD / 테스트

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	bool IsEnemyHacking() const { return ActiveEnemy != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	AActor* GetActiveEnemy() const { return ActiveEnemy; }

	/** 커서 위치 0~1(막대 왼쪽 → 오른쪽). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	float GetFreqCursor() const { return FreqCursor; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	float GetFreqZoneCenter() const { return FreqZoneCenter; }

	/** 지금 단계의 목표 구간 폭(0~1). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	float GetFreqZoneWidth() const;

	/** 지금까지 연속 적중 수(0 ~ GetFreqStepCount). */
	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	int32 GetFreqStep() const { return FreqStep; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	int32 GetFreqStepCount() const { return FMath::Max(1, FreqZoneWidths.Num()); }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	float GetFreqTimeRemaining() const { return FMath::Max(0.0f, FreqTimeLimit - FreqElapsed); }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Enemy")
	float GetFreqTimeLimit() const { return FreqTimeLimit; }

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

	/** 테스트: 커서를 목표 구간 안(true)/밖(false)으로 옮긴 뒤 확인 입력(F/X)을 누른 것처럼 처리. 처리 후 상태 문자열. */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack|Debug")
	FString DebugFreqLockPress(bool bInsideZone);

	/** 테스트: 적 해킹 쿨타임 즉시 해제. */
	UFUNCTION(BlueprintCallable, Category = "Sister|Hack|Debug")
	void DebugClearEnemyHackCooldown() { EnemyCooldownRemaining = 0.0f; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack|Debug")
	FString GetEnemyHackDebugState() const;

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	const TArray<EHackArrow>& GetArrowSequence() const { return ArrowSequence; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	int32 GetArrowIndex() const { return ArrowIndex; }

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	int32 GetRoundIndex() const { return RoundIndex; }

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

	// ------------------------------------------------------------ 적 해킹: 주파수 동조(해킹 시스템 기획 10장)

	/** 단계별 목표 구간 폭(막대 대비 0~1). 원소 수 = 필요한 연속 적중 수(3). */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy")
	TArray<float> FreqZoneWidths = { 0.24f, 0.18f, 0.13f };

	/** 단계별 커서 속도(초당 막대 길이). 왕복하므로 한 번 건너는 시간 = 1 / 속도. */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy")
	TArray<float> FreqCursorSpeeds = { 1.5f, 2.0f, 2.6f };

	/** 제한 시간(초). 넘으면 실패 — 그로기 없음. */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy")
	float FreqTimeLimit = 4.0f;

	/** 빗나간 뒤 입력 무시(초) — 연타 방지. 단계는 1부터 다시(새 구간). */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy")
	float FreqMissLockout = 0.25f;

	/** 새 목표 구간은 커서에서 최소 이만큼(0~1) 떨어진 곳에 생긴다(바로 눌러 맞히는 일 방지). */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack|Enemy")
	float FreqZoneMinCursorGap = 0.3f;

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

	void StartRound(int32 NewRoundIndex);
	void SetFeedback(EHackFeedback NewFeedback);
	void FinishSuccess();
	void EndSession();
	void SetMovementLocked(bool bLocked);

	// 적 해킹(주파수 동조)
	void StartEnemyHack(AActor* Target);
	void TickEnemyHack(float DeltaTime);
	void ConfirmFreqLock();
	void PlaceFreqZone();
	float GetFreqCursorSpeed() const;
	void ResolveEnemyHack(bool bSuccess);
	void EndEnemySession();
	/** 서버(권위 측): 대상 재검증 후 그로기 적용. */
	bool ApplyEnemyHackOnServer(AActor* Target);

	UFUNCTION(Server, Reliable)
	void Server_ApplyEnemyHack(AActor* Target);

	bool IsLocallyControlledOwner() const;
	void PlayHackSound(USoundBase* Sound, float Pitch = 1.0f);
	void StartHackLoop();
	void StopHackLoop();

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

	int32 FreqStep = 0;
	float FreqCursor = 0.0f;
	float FreqDirection = 1.0f;
	float FreqZoneCenter = 0.5f;
	float FreqElapsed = 0.0f;
	float FreqLockoutUntil = 0.0f;
	bool bEnemyResolved = false;
	float EnemyCooldownRemaining = 0.0f;
	/** 서버: 마지막으로 승인한 적 해킹 시각(쿨타임 재검증). */
	float LastServerEnemyHackTime = -1000.0f;
	bool bMovementLocked = false;
	FTimerHandle EnemyResultTimer;

	TArray<EHackArrow> ArrowSequence;
	int32 ArrowIndex = 0;
	int32 RoundIndex = 0;

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
