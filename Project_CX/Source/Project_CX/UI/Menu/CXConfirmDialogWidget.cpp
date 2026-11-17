// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXConfirmDialogWidget.h"
#include "CXMenuItemWidget.h"
#include "CXNeonPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace CXDialogLayout
{
	constexpr float PanelWidth = 760.0f;
	constexpr float ButtonWidth = 240.0f;
}

void UCXConfirmDialogWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (DimBorder)
	{
		return;
	}

	// 전체 화면 딤(뒤 메뉴로 마우스가 새지 않게 Visible)
	DimBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	DimBorder->SetBrushColor(CXMenuStyle::Dim);
	DimBorder->SetHorizontalAlignment(HAlign_Center);
	DimBorder->SetVerticalAlignment(VAlign_Center);
	DimBorder->SetVisibility(ESlateVisibility::Visible);
	WidgetTree->RootWidget = DimBorder;

	PanelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelBox"));
	PanelBox->SetWidthOverride(CXDialogLayout::PanelWidth);
	PanelBox->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	DimBorder->SetContent(PanelBox);

	UCXNeonPanelWidget* Panel = WidgetTree->ConstructWidget<UCXNeonPanelWidget>(UCXNeonPanelWidget::StaticClass(), TEXT("Panel"));
	Panel->SetPanelPadding(FMargin(48.0f, 40.0f, 48.0f, 36.0f));
	Panel->SetFrameColor(CXMenuStyle::Brother);
	PanelBox->AddChild(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetPanelContent(Column);

	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	TitleText->SetFont(CXMenuStyle::Font(32, TEXT("Bold"), 120));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	TitleText->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(TitleText))
	{
		S->SetHorizontalAlignment(HAlign_Center);
	}

	BodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Body"));
	BodyText->SetFont(CXMenuStyle::Font(18, TEXT("Regular"), 60));
	BodyText->SetColorAndOpacity(FSlateColor(CXMenuStyle::Warning));
	BodyText->SetJustification(ETextJustify::Center);
	BodyText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(BodyText))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetPadding(FMargin(0.0f, 20.0f, 0.0f, 0.0f));
	}

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Buttons))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetPadding(FMargin(0.0f, 36.0f, 0.0f, 0.0f));
	}

	ConfirmButton = WidgetTree->ConstructWidget<UCXMenuItemWidget>(UCXMenuItemWidget::StaticClass(), TEXT("ConfirmButton"));
	ConfirmButton->Setup(TEXT("Confirm"), FText::GetEmpty(), true, CXDialogLayout::ButtonWidth, 22);
	ConfirmButton->OnHovered.BindUObject(this, &UCXConfirmDialogWidget::HandleButtonHovered);
	ConfirmButton->OnClicked.BindUObject(this, &UCXConfirmDialogWidget::HandleButtonClicked);
	Buttons->AddChildToHorizontalBox(ConfirmButton);

	USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	Gap->SetSize(FVector2D(32.0f, 1.0f));
	Buttons->AddChildToHorizontalBox(Gap);

	CancelButton = WidgetTree->ConstructWidget<UCXMenuItemWidget>(UCXMenuItemWidget::StaticClass(), TEXT("CancelButton"));
	CancelButton->Setup(TEXT("Cancel"), FText::GetEmpty(), true, CXDialogLayout::ButtonWidth, 22);
	CancelButton->OnHovered.BindUObject(this, &UCXConfirmDialogWidget::HandleButtonHovered);
	CancelButton->OnClicked.BindUObject(this, &UCXConfirmDialogWidget::HandleButtonClicked);
	Buttons->AddChildToHorizontalBox(CancelButton);
}

void UCXConfirmDialogWidget::Open(const FText& Title, const FText& Body, const FText& ConfirmLabel, const FText& CancelLabel, bool bSingleButton, int32 PlayerIndex)
{
	bSingle = bSingleButton;
	FocusPlayer = PlayerIndex;
	TitleText->SetText(Title);
	BodyText->SetText(Body);
	BodyText->SetVisibility(Body.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);

	ConfirmButton->SetLabel(ConfirmLabel);
	CancelButton->SetLabel(CancelLabel);
	CancelButton->SetVisibility(bSingle ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);

	// 기본 포커스는 안전한 쪽(취소). 단일 버튼이면 그 버튼.
	SetFocusIndex(bSingle ? 0 : 1, FocusPlayer);

	OpenAlpha = 0.0f;
	SetRenderOpacity(0.0f);
	SetVisibility(ESlateVisibility::Visible);
}

void UCXConfirmDialogWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (OpenAlpha < 1.0f)
	{
		OpenAlpha = FMath::Min(1.0f, OpenAlpha + InDeltaTime / CXMenuStyle::OpenTime);
		SetRenderOpacity(OpenAlpha);
		FWidgetTransform Transform;
		Transform.Scale = FVector2D(1.0f, FMath::Lerp(0.96f, 1.0f, OpenAlpha));
		PanelBox->SetRenderTransform(Transform);
	}
}

void UCXConfirmDialogWidget::HandleAction(ECXMenuAction Action, int32 PlayerIndex)
{
	FocusPlayer = PlayerIndex;
	switch (Action)
	{
	case ECXMenuAction::Left:
	case ECXMenuAction::Right:
	case ECXMenuAction::Up:
	case ECXMenuAction::Down:
		if (!bSingle)
		{
			SetFocusIndex(1 - FocusIndex, PlayerIndex);
			OnFocusMoved.ExecuteIfBound();
		}
		break;
	case ECXMenuAction::Accept:
		Finish((!bSingle && FocusIndex == 0) ? ECXDialogResult::Confirmed : ECXDialogResult::Cancelled);
		break;
	case ECXMenuAction::Back:
		Finish(ECXDialogResult::Cancelled);
		break;
	default:
		break;
	}
}

void UCXConfirmDialogWidget::SetFocusIndex(int32 NewIndex, int32 PlayerIndex)
{
	FocusIndex = NewIndex;
	const FLinearColor Color = CXMenuStyle::PlayerColor(PlayerIndex);
	ConfirmButton->SetFocused(FocusIndex == 0, Color);
	CancelButton->SetFocused(FocusIndex == 1, Color);
}

void UCXConfirmDialogWidget::Finish(ECXDialogResult Result)
{
	SetVisibility(ESlateVisibility::Collapsed);
	OnResult.ExecuteIfBound(Result);
}

void UCXConfirmDialogWidget::HandleButtonHovered(UCXMenuItemWidget* Item)
{
	if (CanAcceptPointer.IsBound() && !CanAcceptPointer.Execute())
	{
		return;
	}
	const int32 NewIndex = (Item == ConfirmButton) ? 0 : 1;
	if (NewIndex != FocusIndex)
	{
		// 마우스 = 키보드/마우스 플레이어 입력(P1 색). 메뉴 쪽 FocusPlayer는 열 때 전달된 값 유지.
		SetFocusIndex(NewIndex, FocusPlayer);
		OnFocusMoved.ExecuteIfBound();
	}
}

void UCXConfirmDialogWidget::HandleButtonClicked(UCXMenuItemWidget* Item)
{
	if (CanAcceptPointer.IsBound() && !CanAcceptPointer.Execute())
	{
		return;
	}
	Finish((!bSingle && Item == ConfirmButton) ? ECXDialogResult::Confirmed : ECXDialogResult::Cancelled);
}
