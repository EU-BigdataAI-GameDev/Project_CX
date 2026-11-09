// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CXNeonPanelWidget.generated.h"

class UBorder;

/**
 *  어두운 반투명 패널 + 얇은 네온 테두리/모서리 브래킷(설정 메뉴 기획 0.5).
 *  확인창, 플레이어 슬롯, 이후 설정/ESC 메뉴 패널이 공용으로 쓴다. 내용은 SetPanelContent로 넣는다.
 */
UCLASS()
class UCXNeonPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPanelContent(UWidget* Content);
	void SetPanelPadding(const FMargin& InPadding);
	void SetFrameColor(const FLinearColor& Color) { FrameColor = Color; }

	/** 테두리 진하기(0~1). 펄스 연출 시 올린다. */
	void SetFrameLineAlpha(float Alpha) { FrameLineAlpha = Alpha; }

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY(Transient) TObjectPtr<UBorder> PanelBorder;

	FLinearColor FrameColor = FLinearColor(0.0f, 0.85f, 1.0f, 1.0f);
	float FrameLineAlpha = 0.35f;
};
