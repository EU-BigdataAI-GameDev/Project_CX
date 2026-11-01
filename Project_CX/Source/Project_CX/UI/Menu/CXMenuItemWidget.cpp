// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXMenuItemWidget.h"
#include "CXMenuStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"

namespace CXMenuItemLayout
{
	constexpr float Height = 52.0f;
	constexpr float BarWidth = 3.0f;
	constexpr float BarSlide = 10.0f;	// 포커스 바가 왼쪽에서 미끄러져 들어오는 거리
}

void UCXMenuItemWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (RootBox)
	{
		return;
	}

	RootBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootBox"));
	RootBox->SetHeightOverride(CXMenuItemLayout::Height);
	WidgetTree->RootWidget = RootBox;

	Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
	Background->SetPadding(FMargin(0.0f));
	Background->SetBrushColor(FLinearColor::Transparent);
	RootBox->AddChild(Background);

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Row"));
	Background->SetContent(Row);

	FocusBar = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FocusBar"));
	{
		// SetDesiredSizeOverride는 Slate 위젯 생성 전엔 무시되므로 브러시 크기로 지정
		FSlateBrush BarBrush;
		BarBrush.ImageSize = FVector2D(CXMenuItemLayout::BarWidth, CXMenuItemLayout::Height);
		FocusBar->SetBrush(BarBrush);
	}
	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(FocusBar))
	{
		S->SetVerticalAlignment(VAlign_Fill);
	}

	USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	Gap->SetSize(FVector2D(18.0f, 1.0f));
	Row->AddChildToHorizontalBox(Gap);

	LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(LabelText))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	InfoText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Info"));
	InfoText->SetFont(CXMenuStyle::Font(16, TEXT("Regular"), 80));
	InfoText->SetColorAndOpacity(FSlateColor(CXMenuStyle::TextDim));
	InfoText->SetVisibility(ESlateVisibility::Collapsed);
	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(InfoText))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(FMargin(24.0f, 0.0f, 16.0f, 0.0f));
	}

	// 자식은 히트 테스트 안 함 → 이 위젯 하나가 호버/클릭을 받는다.
	Background->SetVisibility(ESlateVisibility::HitTestInvisible);
	SetVisibility(ESlateVisibility::Visible);
}

void UCXMenuItemWidget::Setup(FName InItemId, const FText& InLabel, bool bCentered, float Width, int32 FontSize)
{
	ItemId = InItemId;
	bCenteredStyle = bCentered;
	LabelText->SetText(InLabel);
	LabelText->SetFont(CXMenuStyle::Font(FontSize, TEXT("Bold"), 160));
	if (Width > 0.0f)
	{
		RootBox->SetWidthOverride(Width);
	}
	if (bCentered)
	{
		LabelText->SetJustification(ETextJustify::Center);
		RootBox->SetHeightOverride(46.0f);
	}
	ApplyVisuals();
}

void UCXMenuItemWidget::SetLabel(const FText& InLabel)
{
	LabelText->SetText(InLabel);
}

void UCXMenuItemWidget::SetInfo(const FText& InInfo)
{
	InfoText->SetText(InInfo);
	InfoText->SetVisibility(InInfo.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UCXMenuItemWidget::SetFocused(bool bInFocused, const FLinearColor& PlayerColor)
{
	if (bFocused != bInFocused)
	{
		bFocused = bInFocused;
		FocusAlpha = 0.0f;
	}
	FocusColor = PlayerColor;
	ApplyVisuals();
}

void UCXMenuItemWidget::SetIntroOffset(float OffsetX, float Opacity)
{
	SetRenderTranslation(FVector2D(OffsetX, 0.0f));
	SetRenderOpacity(Opacity);
}

void UCXMenuItemWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bFocused && FocusAlpha < 1.0f)
	{
		FocusAlpha = FMath::Min(1.0f, FocusAlpha + InDeltaTime / CXMenuStyle::FocusSlideTime);
		ApplyVisuals();
	}
}

void UCXMenuItemWidget::ApplyVisuals()
{
	if (!LabelText)
	{
		return;
	}
	const float A = bFocused ? FMath::InterpEaseOut(0.0f, 1.0f, FocusAlpha, 2.0f) : 0.0f;

	LabelText->SetColorAndOpacity(FSlateColor(bFocused ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.55f)));
	// 옅은 글로우: 플레이어 색 그림자
	LabelText->SetShadowOffset(FVector2D(1.5f, 1.5f));
	LabelText->SetShadowColorAndOpacity(FocusColor.CopyWithNewOpacity(bFocused ? 0.55f : 0.0f));

	FocusBar->SetColorAndOpacity(FocusColor.CopyWithNewOpacity(A));
	FocusBar->SetRenderTranslation(FVector2D(-(1.0f - A) * CXMenuItemLayout::BarSlide, 0.0f));

	if (bCenteredStyle)
	{
		// 확인창 버튼: 포커스면 플레이어 색 반투명 바탕, 아니면 어두운 패널
		Background->SetBrushColor(bFocused ? FocusColor.CopyWithNewOpacity(0.22f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.05f));
	}
	else
	{
		// 메뉴 항목: 포커스 줄에 왼쪽에서 오른쪽으로 옅은 색
		Background->SetBrushColor(bFocused ? FocusColor.CopyWithNewOpacity(0.10f * A) : FLinearColor::Transparent);
	}
}

void UCXMenuItemWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	OnHovered.ExecuteIfBound(this);
}

FReply UCXMenuItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bPressed = true;
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UCXMenuItemWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bPressed)
	{
		bPressed = false;
		// 클릭 = 호버(포커스 이동) + 선택
		OnHovered.ExecuteIfBound(this);
		OnClicked.ExecuteIfBound(this);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}
