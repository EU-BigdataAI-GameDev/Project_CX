// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CXGameInstance.h"
#include "CXPlayerSlotWidget.generated.h"

class UTextBlock;
class UImage;
class UCXNeonPanelWidget;

/**
 *  시작화면 하단 플레이어 슬롯(게임 시작화면 기획 3·5장).
 *
 *    ┌──────────────────────────┐
 *    │ P2 SISTER    PRESS START │  ← 게임패드 슬롯은 1.2초 주기 페이드 깜박임
 *    │ GAMEPAD                  │
 *    └──────────────────────────┘
 *
 *  표시만 담당한다. 준비 여부는 UCXTitleMenuWidget이 UCXGameInstance 기준으로 넣어 준다.
 */
UCLASS()
class UCXPlayerSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(int32 InPlayerIndex, const FText& InName);
	void SetState(ECXInputDeviceType InDevice, bool bInReady, bool bGamepadAttached);

	/** 시작 불가 안내: 테두리 펄스(약 1.5초). */
	void Pulse();

	bool IsReady() const { return bReady; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY(Transient) TObjectPtr<UCXNeonPanelWidget> Panel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(Transient) TObjectPtr<UImage> StateSquare;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StateText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DeviceText;

	int32 PlayerIndex = 0;
	ECXInputDeviceType Device = ECXInputDeviceType::KeyboardMouse;
	bool bReady = false;
	float Time = 0.0f;
	float PulseRemaining = 0.0f;
};
