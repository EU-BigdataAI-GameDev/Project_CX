// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CXMenuStyle.h"
#include "CXConfirmDialogWidget.generated.h"

class UTextBlock;
class UBorder;
class USizeBox;
class UHorizontalBox;
class UCXMenuItemWidget;

/** 확인창 결과. */
enum class ECXDialogResult : uint8
{
	Confirmed,
	Cancelled
};

DECLARE_DELEGATE_OneParam(FCXDialogResultEvent, ECXDialogResult);
DECLARE_DELEGATE(FCXDialogInteractEvent);

/**
 *  화면 중앙 모달 확인창(설정 메뉴 기획 0.4).
 *
 *  - 버튼 2개 CONFIRM / CANCEL, 기본 포커스는 항상 안전한 쪽 CANCEL. ESC/B = 취소.
 *  - 단일 버튼 모드(알림창): BACK 하나 — 설정 자리표시 패널 등.
 *  - 뒤 화면은 검정 55% 딤으로 덮고 마우스 입력을 막는다.
 *  - 열기: 0.15초 페이드 + 패널 세로 스케일 0.96→1.0(CRT 켜짐 느낌).
 */
UCLASS()
class UCXConfirmDialogWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** bSingleButton이면 ConfirmLabel 하나만 표시하고 결과는 항상 Cancelled. */
	void Open(const FText& Title, const FText& Body, const FText& ConfirmLabel, const FText& CancelLabel, bool bSingleButton, int32 PlayerIndex);

	/** 메뉴 입력 처리. 항상 소비한다. */
	void HandleAction(ECXMenuAction Action, int32 PlayerIndex);

	FCXDialogResultEvent OnResult;
	/** 버튼 포커스가 바뀜(사운드). */
	FCXDialogInteractEvent OnFocusMoved;
	/** 마우스 입력 시 메뉴에 알린다(입력 잠금 확인). 반환 false면 무시. */
	TDelegate<bool()> CanAcceptPointer;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void SetFocusIndex(int32 NewIndex, int32 PlayerIndex);
	void Finish(ECXDialogResult Result);
	void HandleButtonHovered(UCXMenuItemWidget* Item);
	void HandleButtonClicked(UCXMenuItemWidget* Item);

	UPROPERTY(Transient) TObjectPtr<UBorder> DimBorder;
	UPROPERTY(Transient) TObjectPtr<USizeBox> PanelBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BodyText;
	UPROPERTY(Transient) TObjectPtr<UCXMenuItemWidget> ConfirmButton;
	UPROPERTY(Transient) TObjectPtr<UCXMenuItemWidget> CancelButton;

	/** 0 = CONFIRM, 1 = CANCEL. 단일 버튼이면 0만. */
	int32 FocusIndex = 1;
	int32 FocusPlayer = 0;
	bool bSingle = false;
	float OpenAlpha = 1.0f;
};
