// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HackTerminal.h"
#include "SisterHackComponent.generated.h"

class UInputAction;
class UInputMappingContext;

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
 *  - 입력은 에셋 없이 런타임에 만든 Input Action/Mapping Context로 바인딩한다(게임패드 D-pad·X·B + 키보드 대응).
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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 이 거리(cm) 밖으로 나가면 FocusTerminal을 놓는다(단말기 InteractRange와 함께 사용, 더 작은 값 적용). */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack")
	float MaxDetectRange = 300.0f;

	/** 해킹 성공 표시를 보여준 뒤 세션을 닫기까지의 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Sister|Hack")
	float GrantedHoldTime = 1.2f;

private:
	void UpdateFocus();
	void TryBindInput();
	void BuildInputAssets();

	void StartRound(int32 NewRoundIndex);
	void SetFeedback(EHackFeedback NewFeedback);
	void FinishSuccess();
	void EndSession();
	void SetMovementLocked(bool bLocked);

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
};
