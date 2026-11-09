// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CXMenuItemWidget.generated.h"

class UTextBlock;
class UImage;
class UBorder;
class USizeBox;
class UHorizontalBox;
class UCXMenuItemWidget;

DECLARE_DELEGATE_OneParam(FCXMenuItemEvent, UCXMenuItemWidget* /*Item*/);

/**
 *  메뉴 항목 한 줄(포커스 바 + 라벨 + 오른쪽 정보줄). WBP 없이 C++로 위젯 트리를 만든다.
 *
 *  포커스/선택 로직은 갖지 않는다 — 마우스 호버/클릭만 소유 메뉴(UCXMenuWidgetBase)에 알리고,
 *  메뉴가 SetFocused로 하이라이트(왼쪽 2px 네온 바 + 밝은 텍스트 + 옅은 글로우)를 켠다.
 *  확인창 버튼(CONFIRM/CANCEL)도 이 위젯을 가운데 정렬로 재사용한다.
 */
UCLASS()
class UCXMenuItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 생성 직후 한 번 호출. bCentered = 확인창 버튼 모양. */
	void Setup(FName InItemId, const FText& InLabel, bool bCentered = false, float Width = 0.0f, int32 FontSize = 30);

	void SetLabel(const FText& InLabel);

	/**
	 *  라벨 오른쪽에 값 위젯(설정 메뉴 슬라이더/선택값 등)을 끼운다. LabelWidth > 0이면 라벨 칸 폭 고정.
	 *  값 위젯은 마우스를 직접 받을 수 있다(바 클릭/드래그) — 나머지 자식은 계속 히트 테스트 안 함.
	 */
	void SetValueWidget(UWidget* ValueWidget, float LabelWidth = 0.0f);
	void SetInfo(const FText& InInfo);
	void SetFocused(bool bInFocused, const FLinearColor& PlayerColor);

	/** 진입 연출용: 가로 오프셋/투명도(메뉴 슬라이드인). */
	void SetIntroOffset(float OffsetX, float Opacity);

	FName GetItemId() const { return ItemId; }
	bool IsFocused() const { return bFocused; }

	FCXMenuItemEvent OnHovered;
	FCXMenuItemEvent OnClicked;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void ApplyVisuals();

	UPROPERTY(Transient) TObjectPtr<USizeBox> RootBox;
	UPROPERTY(Transient) TObjectPtr<UBorder> Background;
	UPROPERTY(Transient) TObjectPtr<UImage> FocusBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LabelText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InfoText;
	UPROPERTY(Transient) TObjectPtr<UHorizontalBox> RowBox;
	UPROPERTY(Transient) TObjectPtr<USizeBox> LabelBox;

	FName ItemId;
	bool bCenteredStyle = false;
	bool bFocused = false;
	bool bPressed = false;
	FLinearColor FocusColor = FLinearColor::White;
	/** 0 → 1: 포커스 바 슬라이드 진행도. */
	float FocusAlpha = 0.0f;
};
