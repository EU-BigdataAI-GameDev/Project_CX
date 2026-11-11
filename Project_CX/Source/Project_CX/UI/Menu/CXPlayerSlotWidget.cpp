// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXPlayerSlotWidget.h"
#include "CXMenuStyle.h"
#include "CXNeonPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace CXPlayerSlotLayout
{
	constexpr float Width = 440.0f;
	constexpr float BlinkPeriod = 1.2f;	// PRESS START 페이드 깜박임 주기
	constexpr float PulseTime = 1.5f;
}

void UCXPlayerSlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (Panel)
	{
		return;
	}

	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Box"));
	Box->SetWidthOverride(CXPlayerSlotLayout::Width);
	WidgetTree->RootWidget = Box;

	Panel = WidgetTree->ConstructWidget<UCXNeonPanelWidget>(UCXNeonPanelWidget::StaticClass(), TEXT("Panel"));
	Panel->SetPanelPadding(FMargin(22.0f, 14.0f, 22.0f, 14.0f));
	Box->AddChild(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetPanelContent(Column);

	UHorizontalBox* Top = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Column->AddChildToVerticalBox(Top);

	NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Name"));
	NameText->SetFont(CXMenuStyle::Font(20, TEXT("Bold"), 120));
	if (UHorizontalBoxSlot* S = Top->AddChildToHorizontalBox(NameText))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		S->SetVerticalAlignment(VAlign_Center);
	}

	StateSquare = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("StateSquare"));
	{
		FSlateBrush SquareBrush;
		SquareBrush.ImageSize = FVector2D(10.0f, 10.0f);
		StateSquare->SetBrush(SquareBrush);
	}
	if (UHorizontalBoxSlot* S = Top->AddChildToHorizontalBox(StateSquare))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
	}

	StateText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("State"));
	StateText->SetFont(CXMenuStyle::Font(18, TEXT("Bold"), 140));
	if (UHorizontalBoxSlot* S = Top->AddChildToHorizontalBox(StateText))
	{
		S->SetVerticalAlignment(VAlign_Center);
	}

	DeviceText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Device"));
	DeviceText->SetFont(CXMenuStyle::MonoFont(13, TEXT("Regular"), 60));	// 장치 상태 — 고정폭
	DeviceText->SetColorAndOpacity(FSlateColor(CXMenuStyle::TextDim));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(DeviceText))
	{
		S->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCXPlayerSlotWidget::Setup(int32 InPlayerIndex, const FText& InName)
{
	PlayerIndex = InPlayerIndex;
	const FLinearColor Color = CXMenuStyle::PlayerColor(PlayerIndex);
	NameText->SetText(InName);
	NameText->SetColorAndOpacity(FSlateColor(Color));
	Panel->SetFrameColor(Color);
}

void UCXPlayerSlotWidget::SetState(ECXInputDeviceType InDevice, bool bInReady, bool bGamepadAttached)
{
	Device = InDevice;
	bReady = bInReady;
	const FLinearColor Color = CXMenuStyle::PlayerColor(PlayerIndex);

	StateText->SetText(bReady ? FText::FromString(TEXT("READY")) : FText::FromString(TEXT("PRESS START")));
	StateText->SetColorAndOpacity(FSlateColor(bReady ? Color : FLinearColor::White));
	StateSquare->SetVisibility(bReady ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	StateSquare->SetColorAndOpacity(Color);

	if (Device == ECXInputDeviceType::KeyboardMouse)
	{
		DeviceText->SetText(FText::FromString(TEXT("KEYBOARD & MOUSE")));
		DeviceText->SetColorAndOpacity(FSlateColor(CXMenuStyle::TextDim));
	}
	else if (!bGamepadAttached && !bReady)
	{
		// 게임패드가 하나도 없음 → 경고색
		DeviceText->SetText(FText::FromString(TEXT("GAMEPAD  ·  NOT CONNECTED")));
		DeviceText->SetColorAndOpacity(FSlateColor(CXMenuStyle::Warning));
	}
	else
	{
		DeviceText->SetText(FText::FromString(TEXT("GAMEPAD")));
		DeviceText->SetColorAndOpacity(FSlateColor(CXMenuStyle::TextDim));
	}
	if (bReady)
	{
		StateText->SetRenderOpacity(1.0f);
	}
}

void UCXPlayerSlotWidget::Pulse()
{
	PulseRemaining = CXPlayerSlotLayout::PulseTime;
}

void UCXPlayerSlotWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Time += InDeltaTime;

	if (!bReady)
	{
		// PRESS START: 1.2초 주기 페이드(0.25 ~ 1.0)
		const float Wave = 0.5f + 0.5f * FMath::Cos(Time * UE_TWO_PI / CXPlayerSlotLayout::BlinkPeriod);
		StateText->SetRenderOpacity(FMath::Lerp(0.25f, 1.0f, Wave));
	}

	float LineAlpha = 0.35f;
	if (PulseRemaining > 0.0f)
	{
		PulseRemaining = FMath::Max(0.0f, PulseRemaining - InDeltaTime);
		// 빠른 맥박 3회 → 테두리 진하게
		const float Beat = 0.5f + 0.5f * FMath::Sin((CXPlayerSlotLayout::PulseTime - PulseRemaining) * UE_TWO_PI * 2.0f - UE_HALF_PI);
		LineAlpha = FMath::Lerp(0.35f, 1.0f, Beat * (PulseRemaining / CXPlayerSlotLayout::PulseTime + 0.3f));
	}
	Panel->SetFrameLineAlpha(FMath::Clamp(LineAlpha, 0.0f, 1.0f));
}
