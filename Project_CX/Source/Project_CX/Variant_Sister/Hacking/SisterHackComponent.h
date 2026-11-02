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
	Granted
};

/**
 *  여동생 전용 해킹 로직(.claude/Plans/해킹 시스템 기획.md).
 *
 *  - 0.2초마다 가장 가까운 해킹 가능 단말기(AHackTerminal::InteractRange 이내)를 FocusTerminal로 잡는다.
 *  - 입력은 에셋 없이 런타임에 만든 Input Action/Mapping Context로 바인딩한다(게임패드 D-pad·B + 키보드 대응).
 *    해킹 시작/제출 키(F/X)는 두 캐릭터 공용 상호작용 키라 UCoopInteractComponent가 받아 Interact()로 넘긴다.
 *  - 해킹 중에는 이동/점프를 잠그고, 화살표 퍼즐 또는 4자리 비밀번호 퍼즐을 진행한다.
 *  - 실패해도 무제한 재시도, 언제든 취소 가능(소프트락 없음).
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

	UFUNCTION(BlueprintPure, Category = "Sister|Hack")
	bool IsHacking() const { return ActiveTerminal != nullptr; }

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
