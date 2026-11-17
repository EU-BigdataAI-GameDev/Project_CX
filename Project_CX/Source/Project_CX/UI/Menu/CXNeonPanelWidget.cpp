// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXNeonPanelWidget.h"
#include "CXMenuStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"

void UCXNeonPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (PanelBorder)
	{
		return;
	}
	PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBorder"));
	PanelBorder->SetBrushColor(CXMenuStyle::Panel);
	PanelBorder->SetPadding(FMargin(24.0f, 18.0f));
	WidgetTree->RootWidget = PanelBorder;
}

void UCXNeonPanelWidget::SetPanelContent(UWidget* Content)
{
	PanelBorder->SetContent(Content);
}

void UCXNeonPanelWidget::SetPanelPadding(const FMargin& InPadding)
{
	PanelBorder->SetPadding(InPadding);
}

int32 UCXNeonPanelWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	// 자식(패널 내용)을 그린 뒤 그 위에 테두리
	int32 MaxLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FLinearColor Color = FrameColor * FLinearColor(1, 1, 1, InWidgetStyle.GetColorAndOpacityTint().A);
	CXMenuStyle::DrawNeonFrame(OutDrawElements, MaxLayer + 1, AllottedGeometry, Color, FrameLineAlpha);
	return MaxLayer + 1;
}
