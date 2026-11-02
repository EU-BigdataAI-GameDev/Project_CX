// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXSettingsWidget.h"
#include "CXMenuItemWidget.h"
#include "CXMenuSubsystem.h"
#include "CXNeonPanelWidget.h"
#include "CXPauseMenuWidget.h"
#include "CXGameInstance.h"
#include "CXAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Rendering/DrawElements.h"
#include "CXSfx.h"
#include "Sound/SoundBase.h"

UCXSettingsWidget::UCXSettingsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 음량 바 한 칸(Kenney switch13). SFX 바는 같은 소리를 미리듣기로 — SCl_SFX_UI라 바꾼 효과음 음량이 바로 들린다.
	SliderStepSound = CXSfx::Find<USoundBase>(TEXT("/Game/Project_CX/Audio/SFX/UI/SW_UI_Slider.SW_UI_Slider"));
	SfxPreviewSound = SliderStepSound;
}

namespace CXSettings
{
	// 탭 순서
	constexpr int32 TabAudio = 0;
	constexpr int32 TabSplit = 1;
	constexpr int32 TabDevices = 2;
	constexpr int32 TabControls = 3;
	constexpr int32 NumTabs = 4;

	// 항목 ID
	const FName Master = TEXT("Master");
	const FName Music = TEXT("Music");
	const FName Sfx = TEXT("Sfx");
	const FName ResetAudio = TEXT("ResetAudio");
	const FName Layout = TEXT("Layout");
	const FName ResetSplit = TEXT("ResetSplit");
	const FName DevBrother = TEXT("DevBrother");
	const FName DevSister = TEXT("DevSister");
	const FName ResetDevices = TEXT("ResetDevices");
	const FName CtrlCharacter = TEXT("CtrlCharacter");
	const FName CtrlDevice = TEXT("CtrlDevice");

	// 레이아웃(1920x1080 기준, 중앙 1200x720 패널 — 3장)
	constexpr float PanelWidth = 1200.0f;
	constexpr float PanelHeight = 720.0f;
	constexpr float TabWidth = 250.0f;
	constexpr float LabelWidth = 300.0f;
	constexpr float BarWidth = 320.0f;
	constexpr float BarHeight = 18.0f;
	constexpr float ValueWidth = 320.0f;
	constexpr int32 RowFontSize = 24;
	constexpr int32 NumSegments = 10;
	constexpr float StatusPollInterval = 0.5f;
	constexpr float FlashTime = 2.6f;

	const TCHAR* TabLabels[NumTabs] = { TEXT("AUDIO"), TEXT("SPLIT SCREEN"), TEXT("INPUT DEVICES"), TEXT("CONTROLS") };
	const TCHAR* TabDescriptions[NumTabs] = {
		TEXT("MASTER, MUSIC AND SOUND EFFECT VOLUME."),
		TEXT("HOW THE SCREEN IS SPLIT BETWEEN THE TWO PLAYERS."),
		TEXT("WHICH DEVICE EACH PLAYER USES."),
		TEXT("CONTROLS FOR EACH CHARACTER AND DEVICE.") };

	const TCHAR* MsgKeyboardTaken = TEXT("ONLY ONE PLAYER CAN USE THE KEYBOARD");
	const TCHAR* MsgDevicesLocked = TEXT("INPUT DEVICES CAN ONLY BE CHANGED FROM THE TITLE SCREEN");

	/** 조작표 한 줄(4.5 — 실제 바인딩 기준으로 확인한 값). */
	struct FControlLine
	{
		const TCHAR* Action;
		const TCHAR* Keyboard;
		const TCHAR* Gamepad;
		const TCHAR* Note;
	};

	// 오빠: IMC_Default(IA_Move/IA_Jump/IA_Fire/IA_Aim) + BrotherCombatComponent 게임패드 RT/LT 런타임 매핑
	const FControlLine BrotherLines[] = {
		{ TEXT("MOVE"),			TEXT("A / D"),						TEXT("LEFT STICK"),					TEXT("") },
		{ TEXT("JUMP"),			TEXT("SPACE"),						TEXT("A"),							TEXT("") },
		{ TEXT("FIRE"),			TEXT("LEFT MOUSE BUTTON"),			TEXT("RT"),							TEXT("") },
		{ TEXT("AIM (HOLD)"),	TEXT("RIGHT MOUSE BUTTON"),			TEXT("LT"),							TEXT("") },
		{ TEXT("DODGE"),		TEXT("LEFT SHIFT"),					TEXT("RB"),							TEXT("NOT YET AVAILABLE") },
		{ TEXT("RELOAD"),		TEXT("AUTOMATIC WHEN EMPTY"),		TEXT("AUTOMATIC WHEN EMPTY"),		TEXT("") },
		{ TEXT("MENU"),			TEXT("ESC"),						TEXT("START"),						TEXT("") },
	};
	// 여동생: IMC_Default(이동/점프) + SisterHackComponent IMC_Hack(F·X / 방향키·D-pad / Backspace·B)
	const FControlLine SisterLines[] = {
		{ TEXT("MOVE"),			TEXT("A / D"),						TEXT("LEFT STICK"),					TEXT("") },
		{ TEXT("JUMP"),			TEXT("SPACE"),						TEXT("A"),							TEXT("") },
		{ TEXT("HACK"),			TEXT("F"),							TEXT("X"),							TEXT("") },
		{ TEXT("PUZZLE INPUT"),	TEXT("ARROW KEYS"),					TEXT("D-PAD"),						TEXT("") },
		{ TEXT("CANCEL HACK"),	TEXT("BACKSPACE"),					TEXT("B"),							TEXT("") },
		{ TEXT("MENU"),			TEXT("ESC"),						TEXT("START"),						TEXT("") },
	};
	constexpr int32 MaxControlLines = UE_ARRAY_COUNT(BrotherLines) > UE_ARRAY_COUNT(SisterLines) ? UE_ARRAY_COUNT(BrotherLines) : UE_ARRAY_COUNT(SisterLines);

	const TCHAR* DeviceLabel(ECXInputDeviceType Device)
	{
		return Device == ECXInputDeviceType::KeyboardMouse ? TEXT("KEYBOARD & MOUSE") : TEXT("GAMEPAD");
	}

	ECXInputDeviceType OtherDevice(ECXInputDeviceType Device)
	{
		return Device == ECXInputDeviceType::KeyboardMouse ? ECXInputDeviceType::Gamepad : ECXInputDeviceType::KeyboardMouse;
	}

	UCXGameUserSettings* Settings()
	{
		return UCXGameUserSettings::GetCXGameUserSettings();
	}
}

// =============================================================================================
// 음량 바

void UCXSettingsBarWidget::SetBarState(int32 InValue, const FLinearColor& InColor, bool bInHighlighted)
{
	Value = InValue;
	Color = InColor;
	bHighlighted = bInHighlighted;
}

int32 UCXSettingsBarWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	const float Gap = 4.0f;
	const float SegW = (Size.X - Gap * (CXSettings::NumSegments - 1)) / CXSettings::NumSegments;
	const int32 Filled = FMath::Clamp(Value / UCXGameUserSettings::VolumeStep, 0, CXSettings::NumSegments);
	// 부모 투명도(열기 페이드, 확인창 뒤 흐림)를 따른다
	const float Opacity = InWidgetStyle.GetColorAndOpacityTint().A;
	for (int32 i = 0; i < CXSettings::NumSegments; ++i)
	{
		const FLinearColor C = i < Filled
			? Color.CopyWithNewOpacity((bHighlighted ? 1.0f : 0.7f) * Opacity)
			: FLinearColor(1.0f, 1.0f, 1.0f, (bHighlighted ? 0.16f : 0.1f) * Opacity);
		CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f(i * (SegW + Gap), 0.0f), FVector2f(SegW, Size.Y), C);
	}
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
}

void UCXSettingsBarWidget::SetFromPointer(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FVector2f Local = FVector2f(InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition()));
	const float Width = FMath::Max(1.0f, static_cast<float>(InGeometry.GetLocalSize().X));
	// 칸 단위 스냅: 칸의 절반을 넘기면 그 칸까지 채움, 맨 왼쪽은 0(MUTE)
	const int32 Steps = FMath::Clamp(FMath::RoundToInt(Local.X / Width * CXSettings::NumSegments), 0, CXSettings::NumSegments);
	OnValueSet.ExecuteIfBound(Steps * UCXGameUserSettings::VolumeStep);
}

FReply UCXSettingsBarWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = true;
		SetFromPointer(InGeometry, InMouseEvent);
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UCXSettingsBarWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && HasMouseCapture())
	{
		SetFromPointer(InGeometry, InMouseEvent);
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UCXSettingsBarWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

// =============================================================================================
// 아이콘

int32 UCXSettingsGlyphWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f S = FVector2f(AllottedGeometry.GetLocalSize());
	const float A = InWidgetStyle.GetColorAndOpacityTint().A;
	const FLinearColor C = Color * FLinearColor(1, 1, 1, A);
	auto Rect = [&](float X, float Y, float W, float H, const FLinearColor& Col)
	{
		CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f(X, Y), FVector2f(W, H), Col);
	};

	switch (Glyph)
	{
	case ECXSettingsGlyph::Lock:
	{
		// 자물쇠: 고리(ㄷ자 3변) + 몸통 + 열쇠 구멍
		const float BodyH = S.Y * 0.55f;
		const float BodyY = S.Y - BodyH;
		const float ShackleW = S.X * 0.6f;
		const float ShackleX = (S.X - ShackleW) * 0.5f;
		const float T = FMath::Max(2.0f, S.X * 0.14f);
		Rect(ShackleX, 0.0f, ShackleW, T, C);
		Rect(ShackleX, 0.0f, T, BodyY, C);
		Rect(ShackleX + ShackleW - T, 0.0f, T, BodyY, C);
		Rect(0.0f, BodyY, S.X, BodyH, C);
		Rect(S.X * 0.5f - T * 0.5f, BodyY + BodyH * 0.3f, T, BodyH * 0.4f, FLinearColor(0.0f, 0.0f, 0.0f, 0.7f * A));
		break;
	}
	case ECXSettingsGlyph::SplitTopBottom:
	case ECXSettingsGlyph::SplitLeftRight:
	{
		// 화면 분할 미리보기: 위 청록/아래 마젠타 또는 좌 청록/우 마젠타
		const FLinearColor Bro = CXMenuStyle::Brother * FLinearColor(1, 1, 1, 0.75f * A);
		const FLinearColor Sis = CXMenuStyle::Sister * FLinearColor(1, 1, 1, 0.75f * A);
		const float G = 2.0f;
		if (Glyph == ECXSettingsGlyph::SplitTopBottom)
		{
			const float H = (S.Y - G) * 0.5f;
			Rect(0.0f, 0.0f, S.X, H, Bro);
			Rect(0.0f, H + G, S.X, H, Sis);
		}
		else
		{
			const float W = (S.X - G) * 0.5f;
			Rect(0.0f, 0.0f, W, S.Y, Bro);
			Rect(W + G, 0.0f, W, S.Y, Sis);
		}
		CXMenuStyle::DrawNeonFrame(OutDrawElements, LayerId + 1, AllottedGeometry, FLinearColor(1, 1, 1, A), 0.5f, 5.0f, 1.0f);
		break;
	}
	}
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 2, InWidgetStyle, bParentEnabled);
}

// =============================================================================================
// 탭

void UCXSettingsTabWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (RootBox)
	{
		return;
	}
	RootBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootBox"));
	RootBox->SetHeightOverride(52.0f);
	WidgetTree->RootWidget = RootBox;

	UOverlay* Layer = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Layer"));
	RootBox->AddChild(Layer);

	Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
	Background->SetBrushColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.04f));
	if (UOverlaySlot* S = Layer->AddChildToOverlay(Background))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}

	LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
	LabelText->SetFont(CXMenuStyle::Font(20, TEXT("Bold"), 160));
	LabelText->SetJustification(ETextJustify::Center);
	if (UOverlaySlot* S = Layer->AddChildToOverlay(LabelText))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Center);
	}

	Underline = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Underline"));
	{
		// SetDesiredSizeOverride는 Slate 위젯 생성 전엔 무시되므로 브러시 크기로 지정
		FSlateBrush Brush;
		Brush.ImageSize = FVector2D(1.0f, 3.0f);
		Underline->SetBrush(Brush);
	}
	if (UOverlaySlot* S = Layer->AddChildToOverlay(Underline))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Bottom);
	}

	Layer->SetVisibility(ESlateVisibility::HitTestInvisible);
	SetVisibility(ESlateVisibility::Visible);
}

void UCXSettingsTabWidget::Setup(int32 InTabIndex, const FText& InLabel, float Width)
{
	TabIndex = InTabIndex;
	LabelText->SetText(InLabel);
	RootBox->SetWidthOverride(Width);
}

void UCXSettingsTabWidget::SetTabState(bool bInSelected, bool bInFocused, const FLinearColor& InColor)
{
	// 현재 탭: 플레이어 색 밑줄 + 밝은 글자 / 탭 줄 포커스: 플레이어 색 바탕
	LabelText->SetColorAndOpacity(FSlateColor(bInSelected ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.45f)));
	LabelText->SetShadowOffset(FVector2D(1.5f, 1.5f));
	LabelText->SetShadowColorAndOpacity(InColor.CopyWithNewOpacity(bInSelected ? 0.5f : 0.0f));
	Underline->SetColorAndOpacity(InColor.CopyWithNewOpacity(bInSelected ? 1.0f : 0.0f));
	Background->SetBrushColor(bInFocused ? InColor.CopyWithNewOpacity(0.22f) : FLinearColor(1.0f, 1.0f, 1.0f, bInSelected ? 0.07f : 0.03f));
}

void UCXSettingsTabWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	OnHovered.ExecuteIfBound(TabIndex);
}

FReply UCXSettingsTabWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnClicked.ExecuteIfBound(TabIndex);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

// =============================================================================================
// 설정 메뉴 — 열기 / 닫기

UCXSettingsWidget* UCXSettingsWidget::Open(UCXMenuWidgetBase* Opener, bool bAllowDeviceChange, TFunction<void()> OnClosed)
{
	UCXMenuSubsystem* Menus = Opener ? UCXMenuSubsystem::Get(Opener) : nullptr;
	APlayerController* Owner = Opener ? Opener->GetOwningPlayer() : nullptr;
	if (!Menus || !Owner)
	{
		return nullptr;
	}
	// 호출한 메뉴(시작화면 100 / ESC 메뉴 200)보다 위
	UCXSettingsWidget* Settings = Cast<UCXSettingsWidget>(Menus->PushMenu(UCXSettingsWidget::StaticClass(), Owner, 300));
	if (Settings)
	{
		Settings->FocusPlayer = Opener->GetFocusPlayer();
		Settings->Configure(bAllowDeviceChange, MoveTemp(OnClosed));
	}
	return Settings;
}

void UCXSettingsWidget::Configure(bool bInAllowDeviceChange, TFunction<void()> InOnClosed)
{
	bAllowDeviceChange = bInAllowDeviceChange;
	OnClosedCallback = MoveTemp(InOnClosed);
	RefreshFocusVisuals();
	// 보기 전용(ESC 메뉴): 조작 장치 기본값 복원은 조건 미충족 항목 → 숨김(0.3)
	if (FCXSettingsRow* Row = FindRow(CXSettings::ResetDevices))
	{
		Row->Item->SetVisibility(bAllowDeviceChange ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	RefreshAll();
}

bool UCXSettingsWidget::OnBack()
{
	CloseSettings();
	return true;
}

void UCXSettingsWidget::CloseSettings()
{
	if (bClosed)
	{
		return;
	}
	bClosed = true;
	// 설정 메뉴를 닫을 때 1회 저장(5장)
	CXSettings::Settings()->SaveSettings();

	TFunction<void()> Callback = MoveTemp(OnClosedCallback);
	OnClosedCallback = nullptr;
	if (UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this))
	{
		Menus->PopMenu(this);
	}
	if (Callback)
	{
		Callback();
	}
}

// =============================================================================================
// 위젯 트리

void UCXSettingsWidget::BuildMenu()
{
	RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = RootOverlay;

	auto MakeText = [this](const FString& Str, int32 Size, FName Typeface, int32 Spacing, const FLinearColor& Color)
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Str));
		T->SetFont(CXMenuStyle::Font(Size, Typeface, Spacing));
		T->SetColorAndOpacity(FSlateColor(Color));
		return T;
	};
	auto MakeRule = [this]()
	{
		UImage* Rule = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush Brush;
		Brush.ImageSize = FVector2D(1.0f, 1.0f);
		Rule->SetBrush(Brush);
		Rule->SetColorAndOpacity(CXMenuStyle::Brother.CopyWithNewOpacity(0.3f));
		return Rule;
	};

	// 1) 딤 + 스캔라인(ESC 메뉴와 같은 배경 — 시작화면에서는 배경 그림 위에 덮인다)
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(WidgetTree->ConstructWidget<UCXPauseDimWidget>(UCXPauseDimWidget::StaticClass(), TEXT("Dim"))))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}

	// 2) 중앙 1200x720 패널
	PanelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelBox"));
	PanelBox->SetWidthOverride(CXSettings::PanelWidth);
	PanelBox->SetHeightOverride(CXSettings::PanelHeight);
	PanelBox->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(PanelBox))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Center);
	}
	UCXNeonPanelWidget* Panel = WidgetTree->ConstructWidget<UCXNeonPanelWidget>(UCXNeonPanelWidget::StaticClass(), TEXT("Panel"));
	Panel->SetPanelPadding(FMargin(44.0f, 30.0f, 44.0f, 22.0f));
	Panel->SetFrameColor(CXMenuStyle::Brother);
	PanelBox->AddChild(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetPanelContent(Column);

	// 제목
	UTextBlock* Title = MakeText(TEXT("SETTINGS"), 34, TEXT("Bold"), 300, FLinearColor::White);
	Title->SetShadowOffset(FVector2D(2.0f, 2.0f));
	Title->SetShadowColorAndOpacity(CXMenuStyle::Brother.CopyWithNewOpacity(0.45f));
	Column->AddChildToVerticalBox(Title);

	// 탭 줄: [Q] 탭4개 [E]
	UHorizontalBox* TabRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TabRow"));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(TabRow))
	{
		S->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 0.0f));
		S->SetHorizontalAlignment(HAlign_Center);
	}
	TabHintLeft = MakeText(TEXT("Q"), 16, TEXT("Bold"), 100, CXMenuStyle::TextDim);
	if (UHorizontalBoxSlot* S = TabRow->AddChildToHorizontalBox(TabHintLeft))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));
	}
	for (int32 i = 0; i < CXSettings::NumTabs; ++i)
	{
		UCXSettingsTabWidget* Tab = WidgetTree->ConstructWidget<UCXSettingsTabWidget>(UCXSettingsTabWidget::StaticClass());
		Tab->Setup(i, FText::FromString(CXSettings::TabLabels[i]), CXSettings::TabWidth);
		Tab->OnHovered.BindUObject(this, &UCXSettingsWidget::HandleTabHovered);
		Tab->OnClicked.BindUObject(this, &UCXSettingsWidget::HandleTabClicked);
		if (UHorizontalBoxSlot* S = TabRow->AddChildToHorizontalBox(Tab))
		{
			S->SetPadding(FMargin(i == 0 ? 0.0f : 6.0f, 0.0f, 0.0f, 0.0f));
		}
		Tabs.Add(Tab);
	}
	TabHintRight = MakeText(TEXT("E"), 16, TEXT("Bold"), 100, CXMenuStyle::TextDim);
	if (UHorizontalBoxSlot* S = TabRow->AddChildToHorizontalBox(TabHintRight))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(FMargin(14.0f, 0.0f, 0.0f, 0.0f));
	}

	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MakeRule()))
	{
		S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	}

	// 페이지
	PageSwitcher = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("Pages"));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(PageSwitcher))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	Pages.SetNum(CXSettings::NumTabs);
	for (int32 i = 0; i < CXSettings::NumTabs; ++i)
	{
		UVerticalBox* List = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		PageSwitcher->AddChild(List);
		Items.Reset();
		BuildPage(i, List);
		Pages[i].Items = Items;
	}
	Items = Pages[CurrentTab].Items;

	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MakeRule()))
	{
		S->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 12.0f));
	}

	// 하단: 설명줄(포커스 항목) + 입력 힌트(마지막 입력 장치)
	DescriptionText = MakeText(TEXT(""), 16, TEXT("Regular"), 120, CXMenuStyle::Text);
	Column->AddChildToVerticalBox(DescriptionText);
	HintText = MakeText(TEXT(""), 14, TEXT("Regular"), 140, CXMenuStyle::TextDim);
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(HintText))
	{
		S->SetHorizontalAlignment(HAlign_Right);
		S->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	}

	// 3) 확인창 레이어
	ModalLayer = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ModalLayer"));
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(ModalLayer))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}
	SetModalHost(ModalLayer);
}

FCXSettingsRow& UCXSettingsWidget::AddRow(UVerticalBox* List, FName Id, const TCHAR* Label, const TCHAR* Description, bool bSlider, bool bChoice)
{
	UCXMenuItemWidget* Item = AddMenuItem(List, Id, FText::FromString(Label));
	Item->Setup(Id, FText::FromString(Label), false, 0.0f, CXSettings::RowFontSize);

	FCXSettingsRow& Row = Rows.AddDefaulted_GetRef();
	Row.Id = Id;
	Row.Item = Item;
	Row.Description = FText::FromString(Description);

	if (!bSlider && !bChoice)
	{
		return Row;
	}

	auto MakeText = [this](int32 Size, FName Typeface, const FLinearColor& Color)
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetFont(CXMenuStyle::Font(Size, Typeface, 120));
		T->SetColorAndOpacity(FSlateColor(Color));
		return T;
	};

	// 값: < [바 또는 선택값] > [숫자/상태] [아이콘]
	UHorizontalBox* Value = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Value->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	Row.LeftArrow = MakeText(22, TEXT("Bold"), CXMenuStyle::TextDim);
	Row.LeftArrow->SetText(FText::FromString(TEXT("<")));
	Row.LeftArrow->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UHorizontalBoxSlot* S = Value->AddChildToHorizontalBox(Row.LeftArrow))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(FMargin(0.0f, 0.0f, 14.0f, 0.0f));
	}

	USizeBox* Middle = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Middle->SetWidthOverride(bSlider ? CXSettings::BarWidth : CXSettings::ValueWidth);
	Middle->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UHorizontalBoxSlot* S = Value->AddChildToHorizontalBox(Middle))
	{
		S->SetVerticalAlignment(VAlign_Center);
	}
	if (bSlider)
	{
		Middle->SetHeightOverride(CXSettings::BarHeight);
		Row.Bar = WidgetTree->ConstructWidget<UCXSettingsBarWidget>(UCXSettingsBarWidget::StaticClass());
		Row.Bar->SetVisibility(ESlateVisibility::Visible);	// 클릭/드래그
		Row.Bar->OnValueSet.BindWeakLambda(this, [this, Id](int32 NewValue)
		{
			if (!CanAcceptInput() || IsModalOpen())
			{
				return;
			}
			bLastInputGamepad = false;
			FocusPlayer = GetKeyboardPlayer();
			bTabFocused = false;
			SetFocusItem(Id);
			SetVolume(Id, NewValue);
		});
		Middle->AddChild(Row.Bar);
	}
	else
	{
		Row.ValueText = MakeText(20, TEXT("Bold"), CXMenuStyle::Text);
		Row.ValueText->SetJustification(ETextJustify::Center);
		Row.ValueText->SetVisibility(ESlateVisibility::HitTestInvisible);
		Middle->AddChild(Row.ValueText);
	}

	Row.RightArrow = MakeText(22, TEXT("Bold"), CXMenuStyle::TextDim);
	Row.RightArrow->SetText(FText::FromString(TEXT(">")));
	Row.RightArrow->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UHorizontalBoxSlot* S = Value->AddChildToHorizontalBox(Row.RightArrow))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(FMargin(14.0f, 0.0f, 0.0f, 0.0f));
	}

	if (bSlider)
	{
		USizeBox* NumberBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		NumberBox->SetWidthOverride(90.0f);
		NumberBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		Row.NumberText = MakeText(20, TEXT("Bold"), CXMenuStyle::Text);
		Row.NumberText->SetJustification(ETextJustify::Right);
		NumberBox->AddChild(Row.NumberText);
		if (UHorizontalBoxSlot* S = Value->AddChildToHorizontalBox(NumberBox))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(10.0f, 0.0f, 0.0f, 0.0f));
		}
	}
	else
	{
		Row.StatusText = MakeText(16, TEXT("Bold"), CXMenuStyle::TextDim);
		Row.StatusText->SetVisibility(ESlateVisibility::Collapsed);
		if (UHorizontalBoxSlot* S = Value->AddChildToHorizontalBox(Row.StatusText))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(28.0f, 0.0f, 0.0f, 0.0f));
		}
	}

	USpacer* Fill = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	if (UHorizontalBoxSlot* S = Value->AddChildToHorizontalBox(Fill))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	if (bChoice)
	{
		auto MakeGlyph = [this, Value](float W, float H)
		{
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Box->SetWidthOverride(W);
			Box->SetHeightOverride(H);
			Box->SetVisibility(ESlateVisibility::HitTestInvisible);
			UCXSettingsGlyphWidget* Glyph = WidgetTree->ConstructWidget<UCXSettingsGlyphWidget>(UCXSettingsGlyphWidget::StaticClass());
			Box->AddChild(Glyph);
			if (UHorizontalBoxSlot* S = Value->AddChildToHorizontalBox(Box))
			{
				S->SetVerticalAlignment(VAlign_Center);
				S->SetPadding(FMargin(16.0f, 0.0f, 16.0f, 0.0f));
			}
			Box->SetVisibility(ESlateVisibility::Collapsed);
			return Glyph;
		};
		Row.Preview = MakeGlyph(56.0f, 32.0f);
		Row.Lock = MakeGlyph(16.0f, 20.0f);
		Row.Lock->SetGlyph(ECXSettingsGlyph::Lock, CXMenuStyle::Warning);
	}

	Item->SetValueWidget(Value, CXSettings::LabelWidth);
	return Row;
}

void UCXSettingsWidget::BuildPage(int32 TabIndex, UVerticalBox* List)
{
	auto AddReset = [this, List](FName Id)
	{
		// 리스트 끝 기본값 복원(앞 항목과 한 줄 띄움)
		USpacer* Gap = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
		Gap->SetSize(FVector2D(1.0f, 20.0f));
		List->AddChildToVerticalBox(Gap);
		AddRow(List, Id, TEXT("RESET TO DEFAULT"), TEXT("RESTORE THIS TAB TO ITS DEFAULT VALUES."), false, false);
	};

	switch (TabIndex)
	{
	case CXSettings::TabAudio:
		AddRow(List, CXSettings::Master, TEXT("MASTER"), TEXT("OVERALL GAME VOLUME."), true, false);
		AddRow(List, CXSettings::Music, TEXT("MUSIC"), TEXT("BACKGROUND MUSIC VOLUME. ACTUAL VOLUME = MUSIC x MASTER."), true, false);
		AddRow(List, CXSettings::Sfx, TEXT("SFX"), TEXT("SOUND EFFECT AND UI VOLUME. ACTUAL VOLUME = SFX x MASTER."), true, false);
		AddReset(CXSettings::ResetAudio);
		break;

	case CXSettings::TabSplit:
		AddRow(List, CXSettings::Layout, TEXT("LAYOUT"), TEXT("TOP / BOTTOM OR LEFT / RIGHT. APPLIES IMMEDIATELY IN GAME."), false, true);
		AddReset(CXSettings::ResetSplit);
		break;

	case CXSettings::TabDevices:
	{
		AddRow(List, CXSettings::DevBrother, TEXT("BROTHER (P1)"), TEXT("INPUT DEVICE FOR THE BROTHER (P1)."), false, true);
		AddRow(List, CXSettings::DevSister, TEXT("SISTER (P2)"), TEXT("INPUT DEVICE FOR THE SISTER (P2)."), false, true);
		AddReset(CXSettings::ResetDevices);
		DeviceNoteText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		DeviceNoteText->SetFont(CXMenuStyle::Font(15, TEXT("Regular"), 120));
		DeviceNoteText->SetColorAndOpacity(FSlateColor(CXMenuStyle::TextDim));
		DeviceNoteText->SetAutoWrapText(true);
		if (UVerticalBoxSlot* S = List->AddChildToVerticalBox(DeviceNoteText))
		{
			S->SetPadding(FMargin(21.0f, 22.0f, 0.0f, 0.0f));
		}
		break;
	}

	case CXSettings::TabControls:
		AddRow(List, CXSettings::CtrlCharacter, TEXT("CHARACTER"), TEXT("WHOSE CONTROLS TO VIEW."), false, true);
		AddRow(List, CXSettings::CtrlDevice, TEXT("DEVICE"), TEXT("SWITCH BETWEEN THE KEYBOARD & MOUSE AND GAMEPAD LAYOUTS. VIEW ONLY."), false, true);
		BuildControlsTable(List);
		break;
	}
}

void UCXSettingsWidget::BuildControlsTable(UVerticalBox* List)
{
	auto MakeText = [this](int32 Size, FName Typeface, const FLinearColor& Color)
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetFont(CXMenuStyle::Font(Size, Typeface, 120));
		T->SetColorAndOpacity(FSlateColor(Color));
		return T;
	};

	UVerticalBox* Table = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ControlsTable"));
	if (UVerticalBoxSlot* S = List->AddChildToVerticalBox(Table))
	{
		S->SetPadding(FMargin(21.0f, 16.0f, 0.0f, 0.0f));
	}
	for (int32 i = 0; i < CXSettings::MaxControlLines; ++i)
	{
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		if (UVerticalBoxSlot* S = Table->AddChildToVerticalBox(Line))
		{
			S->SetPadding(FMargin(0.0f, 3.0f));
		}
		auto AddCell = [this, Line](UTextBlock* Text, float Width)
		{
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			if (Width > 0.0f)
			{
				Box->SetWidthOverride(Width);
			}
			Box->AddChild(Text);
			Line->AddChildToHorizontalBox(Box);
		};
		UTextBlock* Action = MakeText(17, TEXT("Regular"), CXMenuStyle::TextDim);
		UTextBlock* Key = MakeText(17, TEXT("Bold"), CXMenuStyle::Text);
		UTextBlock* Note = MakeText(14, TEXT("Regular"), CXMenuStyle::Warning);
		AddCell(Action, 279.0f);
		AddCell(Key, 420.0f);
		AddCell(Note, 0.0f);
		ControlActionTexts.Add(Action);
		ControlKeyTexts.Add(Key);
		ControlNoteTexts.Add(Note);
	}
	ControlsTipText = MakeText(15, TEXT("Regular"), CXMenuStyle::Brother);
	if (UVerticalBoxSlot* S = List->AddChildToVerticalBox(ControlsTipText))
	{
		S->SetPadding(FMargin(21.0f, 12.0f, 0.0f, 0.0f));
	}
}

FCXSettingsRow* UCXSettingsWidget::FindRow(FName Id)
{
	return Rows.FindByPredicate([Id](const FCXSettingsRow& Row) { return Row.Id == Id; });
}

const FCXSettingsRow* UCXSettingsWidget::FindRow(FName Id) const
{
	return Rows.FindByPredicate([Id](const FCXSettingsRow& Row) { return Row.Id == Id; });
}

// =============================================================================================
// 수명

void UCXSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 기본 포커스: 첫 진입 AUDIO/MASTER, 같은 세션에서 다시 열면 마지막 탭(3장)
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	const int32 StartTab = GI ? FMath::Clamp(GI->SettingsLastTab, 0, CXSettings::NumTabs - 1) : CXSettings::TabAudio;
	FocusPlayer = GI ? GI->ResolvePlayerForInput(false, INPUTDEVICEID_NONE) : 0;

	// 조작법은 오빠 + 현재 설정 장치부터
	ControlsCharacter = 0;
	ControlsDevice = CXSettings::Settings()->GetSlotDevice(0);

	SwitchTab(StartTab, false, false);
	OpenAlpha = 0.0f;
	RootOverlay->SetRenderOpacity(0.0f);
	RefreshAll();

	LockInput();
	PlayUISound(ECXMenuSound::Open);
}

void UCXSettingsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 열기: 0.15초 페이드 + 패널 세로 0.96→1.0
	if (OpenAlpha < 1.0f)
	{
		OpenAlpha = FMath::Min(1.0f, OpenAlpha + InDeltaTime / CXMenuStyle::OpenTime);
		RootOverlay->SetRenderOpacity(OpenAlpha);
		FWidgetTransform Transform;
		Transform.Scale = FVector2D(1.0f, FMath::Lerp(0.96f, 1.0f, OpenAlpha));
		PanelBox->SetRenderTransform(Transform);
	}

	// 마우스 호버로 항목 포커스가 옮겨졌으면 탭 줄 포커스 해제 + 표시 갱신
	if (FocusIndex != LastFocusIndex || (bTabFocused && Items.IsValidIndex(FocusIndex)))
	{
		if (Items.IsValidIndex(FocusIndex))
		{
			bTabFocused = false;
		}
		LastFocusIndex = FocusIndex;
		RefreshRows();
		RefreshTabs();
		RefreshDescription();
	}

	// 확인창이 열리면 뒤 패널을 흐리게
	PanelBox->SetRenderOpacity(IsModalOpen() ? 0.12f : 1.0f);

	// 거부 안내(주황) 표시 시간
	if (FlashRemaining > 0.0f)
	{
		FlashRemaining -= InDeltaTime;
		if (FlashRemaining <= 0.0f)
		{
			FlashText.Reset();
			RefreshDescription();
		}
	}

	// 게임패드 연결 상태(설정이 열린 채로 끊겨도 행 상태만 갱신 — 6장)
	StatusPollTimer -= InDeltaTime;
	if (StatusPollTimer <= 0.0f)
	{
		StatusPollTimer = CXSettings::StatusPollInterval;
		RefreshRows();
	}
}

// =============================================================================================
// 입력

bool UCXSettingsWidget::OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex)
{
	if (Input.Action != ECXMenuAction::None)
	{
		const bool bWasGamepad = bLastInputGamepad;
		bLastInputGamepad = Input.bFromGamepad;
		if (bWasGamepad != bLastInputGamepad)
		{
			RefreshHint();
		}
	}

	switch (Input.Action)
	{
	case ECXMenuAction::TabLeft:
	case ECXMenuAction::TabRight:
		if (!Input.bRepeat)
		{
			FocusPlayer = PlayerIndex;
			SwitchTab((CurrentTab + (Input.Action == ECXMenuAction::TabLeft ? -1 : 1) + CXSettings::NumTabs) % CXSettings::NumTabs, bTabFocused);
		}
		return true;

	case ECXMenuAction::Up:
	case ECXMenuAction::Down:
		FocusPlayer = PlayerIndex;
		MoveVertical(Input.Action == ECXMenuAction::Up ? -1 : 1);
		return true;

	case ECXMenuAction::Left:
	case ECXMenuAction::Right:
	{
		FocusPlayer = PlayerIndex;
		const int32 Dir = Input.Action == ECXMenuAction::Left ? -1 : 1;
		if (bTabFocused)
		{
			// 탭 줄에서는 좌우 = 탭 전환
			if (!Input.bRepeat)
			{
				SwitchTab((CurrentTab + Dir + CXSettings::NumTabs) % CXSettings::NumTabs, true);
			}
		}
		else if (Items.IsValidIndex(FocusIndex))
		{
			ChangeValue(Items[FocusIndex]->GetItemId(), Dir, Input.bRepeat);
		}
		return true;
	}

	case ECXMenuAction::Accept:
		if (!Input.bRepeat)
		{
			FocusPlayer = PlayerIndex;
			if (bTabFocused)
			{
				FocusFirstItem();
				PlayUISound(ECXMenuSound::Move);
			}
			else if (Items.IsValidIndex(FocusIndex))
			{
				ActivateRow(Items[FocusIndex]->GetItemId());
			}
		}
		return true;

	case ECXMenuAction::Start:
		// 서브 화면에서 Start = 한 단계 뒤로(호출한 메뉴로)
		if (!Input.bRepeat)
		{
			FocusPlayer = PlayerIndex;
			PlayUISound(ECXMenuSound::Back);
			CloseSettings();
		}
		return true;

	default:
		return false;	// Back → OnBack
	}
}

void UCXSettingsWidget::HandleTabHovered(int32 TabIndex)
{
	if (!CanAcceptInput() || IsModalOpen() || !IsPointerActive())
	{
		return;
	}
	if (!bTabFocused)
	{
		FocusPlayer = GetKeyboardPlayer();
		FocusTabRow();
		PlayUISound(ECXMenuSound::Move);
	}
}

void UCXSettingsWidget::HandleTabClicked(int32 TabIndex)
{
	if (!CanAcceptInput() || IsModalOpen())
	{
		return;
	}
	bLastInputGamepad = false;
	FocusPlayer = GetKeyboardPlayer();
	if (TabIndex != CurrentTab)
	{
		SwitchTab(TabIndex, true);
	}
	else
	{
		FocusTabRow();
	}
}

// =============================================================================================
// 탭 / 포커스

void UCXSettingsWidget::SwitchTab(int32 NewTab, bool bFocusTabRow, bool bPlaySound)
{
	const bool bChanged = NewTab != CurrentTab;
	CurrentTab = FMath::Clamp(NewTab, 0, CXSettings::NumTabs - 1);
	PageSwitcher->SetActiveWidgetIndex(CurrentTab);
	Items = Pages[CurrentTab].Items;
	FocusIndex = INDEX_NONE;

	if (UCXGameInstance* GI = UCXGameInstance::Get(this))
	{
		GI->SettingsLastTab = CurrentTab;
	}
	if (CurrentTab == CXSettings::TabControls && bChanged)
	{
		// 조작법 탭에 들어올 때마다 현재 캐릭터의 설정 장치부터 보여준다
		ControlsDevice = CXSettings::Settings()->GetSlotDevice(ControlsCharacter);
	}

	if (bFocusTabRow)
	{
		FocusTabRow();
	}
	else
	{
		FocusFirstItem();
	}
	if (bPlaySound && bChanged)
	{
		PlayUISound(ECXMenuSound::Move);
	}
	RefreshAll();
}

void UCXSettingsWidget::FocusTabRow()
{
	bTabFocused = true;
	FocusIndex = INDEX_NONE;
	LastFocusIndex = INDEX_NONE;
	RefreshFocusVisuals();
	RefreshRows();
	RefreshTabs();
	RefreshDescription();
}

void UCXSettingsWidget::FocusFirstItem()
{
	const int32 First = FindVisibleIndex(-1, 1, false);
	if (First == INDEX_NONE)
	{
		FocusTabRow();
		return;
	}
	bTabFocused = false;
	SetFocusIndex(First);
	LastFocusIndex = FocusIndex;
	RefreshRows();
	RefreshTabs();
	RefreshDescription();
}

int32 UCXSettingsWidget::FindVisibleIndex(int32 Start, int32 Delta, bool bWrap) const
{
	for (int32 i = Start + Delta; i >= 0 && i < Items.Num(); i += Delta)
	{
		if (Items[i] && Items[i]->GetVisibility() != ESlateVisibility::Collapsed)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void UCXSettingsWidget::MoveVertical(int32 Delta)
{
	// 탭 줄 ↔ 항목 리스트 순환: 리스트 맨 위에서 위 → 탭 줄, 맨 아래에서 아래 → 탭 줄(3장 / 0.3)
	int32 Next = INDEX_NONE;
	if (bTabFocused)
	{
		Next = Delta > 0 ? FindVisibleIndex(-1, 1, false) : FindVisibleIndex(Items.Num(), -1, false);
	}
	else
	{
		Next = FindVisibleIndex(Items.IsValidIndex(FocusIndex) ? FocusIndex : -1, Delta, false);
	}

	if (Next == INDEX_NONE)
	{
		if (!bTabFocused)
		{
			FocusTabRow();
			PlayUISound(ECXMenuSound::Move);
		}
		return;
	}
	bTabFocused = false;
	SetFocusIndex(Next);
	LastFocusIndex = FocusIndex;
	PlayUISound(ECXMenuSound::Move);
	RefreshRows();
	RefreshTabs();
	RefreshDescription();
}

// =============================================================================================
// 값 변경

void UCXSettingsWidget::SetVolume(FName Id, int32 NewValue)
{
	UCXGameUserSettings* Settings = CXSettings::Settings();
	const int32 Old = Id == CXSettings::Master ? Settings->GetMasterVolume() : Id == CXSettings::Music ? Settings->GetMusicVolume() : Settings->GetSfxVolume();
	if (Id == CXSettings::Master) { Settings->SetMasterVolume(NewValue); }
	else if (Id == CXSettings::Music) { Settings->SetMusicVolume(NewValue); }
	else if (Id == CXSettings::Sfx) { Settings->SetSfxVolume(NewValue); }
	else { return; }

	const int32 New = Id == CXSettings::Master ? Settings->GetMasterVolume() : Id == CXSettings::Music ? Settings->GetMusicVolume() : Settings->GetSfxVolume();
	if (New != Old)
	{
		// 즉시 적용(배경음은 재생 중인 BGM에 바로 반영), 효과음은 미리듣기 큐
		if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
		{
			Audio->ApplyUserVolumes();
			if (Id == CXSettings::Sfx && SfxPreviewSound)
			{
				Audio->PlaySfxPreview(this, SfxPreviewSound);
			}
			else if (SliderStepSound)
			{
				Audio->PlaySfxPreview(this, SliderStepSound);
			}
			else
			{
				PlayUISound(ECXMenuSound::Move);
			}
		}
		RefreshRows();
	}
}

void UCXSettingsWidget::ChangeValue(FName Id, int32 Direction, bool bRepeat)
{
	UCXGameUserSettings* Settings = CXSettings::Settings();

	if (Id == CXSettings::Master || Id == CXSettings::Music || Id == CXSettings::Sfx)
	{
		// 슬라이더는 길게 누르면 반복
		const int32 Cur = Id == CXSettings::Master ? Settings->GetMasterVolume() : Id == CXSettings::Music ? Settings->GetMusicVolume() : Settings->GetSfxVolume();
		SetVolume(Id, Cur + Direction * UCXGameUserSettings::VolumeStep);
		return;
	}
	if (bRepeat)
	{
		// 선택형은 반복 입력 무시(값이 깜박이며 넘어가지 않게)
		return;
	}

	if (Id == CXSettings::Layout)
	{
		Settings->SetSplitLayout(Settings->GetSplitLayout() == ECXSplitLayout::TopBottom ? ECXSplitLayout::LeftRight : ECXSplitLayout::TopBottom);
		PlayUISound(ECXMenuSound::Move);
	}
	else if (Id == CXSettings::DevBrother || Id == CXSettings::DevSister)
	{
		TryChangeDevice(Id == CXSettings::DevBrother ? 0 : 1);
	}
	else if (Id == CXSettings::CtrlCharacter)
	{
		ControlsCharacter = 1 - ControlsCharacter;
		// 캐릭터를 바꾸면 그 캐릭터의 설정 장치 화면부터(4.5)
		ControlsDevice = Settings->GetSlotDevice(ControlsCharacter);
		PlayUISound(ECXMenuSound::Move);
	}
	else if (Id == CXSettings::CtrlDevice)
	{
		ControlsDevice = CXSettings::OtherDevice(ControlsDevice);
		PlayUISound(ECXMenuSound::Move);
	}
	RefreshAll();
}

void UCXSettingsWidget::TryChangeDevice(int32 PlayerIndex)
{
	if (!bAllowDeviceChange)
	{
		// ESC 메뉴에서 열면 보기 전용(4.4)
		PlayUISound(ECXMenuSound::Denied);
		FlashMessage(CXSettings::MsgDevicesLocked);
		return;
	}
	UCXGameInstance* GI = UCXGameInstance::Get(this);
	const ECXInputDeviceType Target = CXSettings::OtherDevice(CXSettings::Settings()->GetSlotDevice(PlayerIndex));
	const bool bChanged = GI ? GI->SetSlotDeviceType(PlayerIndex, Target) : CXSettings::Settings()->SetSlotDevice(PlayerIndex, Target);
	if (!bChanged)
	{
		// 둘 다 키보드 불가 — 값은 그대로, 자동 스왑 없음
		PlayUISound(ECXMenuSound::Denied);
		FlashMessage(CXSettings::MsgKeyboardTaken);
		return;
	}
	// 바뀌었으면 이전 거부 안내는 지운다
	FlashText.Reset();
	FlashRemaining = 0.0f;
	PlayUISound(ECXMenuSound::Move);
}

void UCXSettingsWidget::ActivateRow(FName Id)
{
	if (Id == CXSettings::ResetAudio || Id == CXSettings::ResetSplit || Id == CXSettings::ResetDevices)
	{
		if (Id == CXSettings::ResetDevices && !bAllowDeviceChange)
		{
			PlayUISound(ECXMenuSound::Denied);
			FlashMessage(CXSettings::MsgDevicesLocked);
			return;
		}
		const FString TabName = CXSettings::TabLabels[CurrentTab];
		ShowConfirm(FText::FromString(TEXT("RESET TO DEFAULT?")),
			FText::FromString(FString::Printf(TEXT("%s SETTINGS WILL BE RESTORED TO THEIR DEFAULT VALUES."), *TabName)),
			[this]() { ResetCurrentTab(); });
		return;
	}
	// 선택형 항목은 선택 = 다음 값
	if (FindRow(Id) && FindRow(Id)->ValueText)
	{
		ChangeValue(Id, 1, false);
	}
}

void UCXSettingsWidget::ResetCurrentTab()
{
	UCXGameUserSettings* Settings = CXSettings::Settings();
	switch (CurrentTab)
	{
	case CXSettings::TabAudio:
		Settings->ResetAudioToDefaults();
		if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
		{
			Audio->ApplyUserVolumes();
		}
		break;
	case CXSettings::TabSplit:
		Settings->SetSplitLayout(ECXSplitLayout::TopBottom);
		break;
	case CXSettings::TabDevices:
		if (bAllowDeviceChange)
		{
			// 기본: 오빠 키보드 / 여동생 게임패드. 키보드 중복 거부를 피하려고 여동생 먼저.
			if (UCXGameInstance* GI = UCXGameInstance::Get(this))
			{
				GI->SetSlotDeviceType(1, ECXInputDeviceType::Gamepad);
				GI->SetSlotDeviceType(0, ECXInputDeviceType::KeyboardMouse);
			}
		}
		break;
	default:
		break;
	}
	RefreshAll();
}

// =============================================================================================
// 표시

void UCXSettingsWidget::RefreshAll()
{
	RefreshTabs();
	RefreshRows();
	RefreshControlsTable();
	RefreshDescription();
	RefreshHint();
}

bool UCXSettingsWidget::IsSlotConnected(int32 PlayerIndex) const
{
	const UCXGameUserSettings* Settings = CXSettings::Settings();
	if (Settings->GetSlotDevice(PlayerIndex) == ECXInputDeviceType::KeyboardMouse)
	{
		return true;
	}
	const UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this);
	if (Menus && Menus->GetDisconnectedPlayer() == PlayerIndex)
	{
		return false;
	}
	// 시작화면에서 배정된 패드가 있으면 그 패드 상태
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	const FInputDeviceId Pad = GI ? GI->GetSlotGamepad(PlayerIndex) : INPUTDEVICEID_NONE;
	if (Pad.IsValid())
	{
		return IPlatformInputDeviceMapper::Get().GetInputDeviceConnectionState(Pad) == EInputDeviceConnectionState::Connected;
	}
	// 아니면 이 슬롯이 몇 번째 게임패드 슬롯인지와 연결된 패드 수 비교
	int32 Rank = 0;
	for (int32 i = 0; i <= PlayerIndex; ++i)
	{
		Rank += Settings->GetSlotDevice(i) == ECXInputDeviceType::Gamepad ? 1 : 0;
	}
	return UCXGameInstance::GetConnectedGamepadCount() >= Rank;
}

void UCXSettingsWidget::RefreshRows()
{
	const UCXGameUserSettings* Settings = CXSettings::Settings();
	const FLinearColor Color = CXMenuStyle::PlayerColor(FocusPlayer);
	const FName Focused = (!bTabFocused && Items.IsValidIndex(FocusIndex)) ? Items[FocusIndex]->GetItemId() : NAME_None;

	for (FCXSettingsRow& Row : Rows)
	{
		const bool bFocused = Row.Id == Focused;
		const bool bLocked = !bAllowDeviceChange && (Row.Id == CXSettings::DevBrother || Row.Id == CXSettings::DevSister);
		if (Row.LeftArrow)
		{
			const FLinearColor ArrowColor = bLocked ? CXMenuStyle::TextDim.CopyWithNewOpacity(0.15f)
				: (bFocused ? Color : CXMenuStyle::TextDim.CopyWithNewOpacity(0.25f));
			Row.LeftArrow->SetColorAndOpacity(FSlateColor(ArrowColor));
			Row.RightArrow->SetColorAndOpacity(FSlateColor(ArrowColor));
		}

		if (Row.Bar)
		{
			const int32 V = Row.Id == CXSettings::Master ? Settings->GetMasterVolume() : Row.Id == CXSettings::Music ? Settings->GetMusicVolume() : Settings->GetSfxVolume();
			Row.Bar->SetBarState(V, bFocused ? Color : FLinearColor::White, bFocused);
			// 0 = 완전 음소거(MUTE)
			Row.NumberText->SetText(V == 0 ? FText::FromString(TEXT("MUTE")) : FText::AsNumber(V));
			Row.NumberText->SetColorAndOpacity(FSlateColor(V == 0 ? CXMenuStyle::Warning : (bFocused ? FLinearColor::White : CXMenuStyle::Text)));
		}

		if (Row.Id == CXSettings::Layout)
		{
			const bool bTopBottom = Settings->GetSplitLayout() == ECXSplitLayout::TopBottom;
			Row.ValueText->SetText(FText::FromString(bTopBottom ? TEXT("TOP / BOTTOM") : TEXT("LEFT / RIGHT")));
			Row.Preview->SetGlyph(bTopBottom ? ECXSettingsGlyph::SplitTopBottom : ECXSettingsGlyph::SplitLeftRight, FLinearColor::White);
			Row.Preview->GetParent()->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else if (Row.Id == CXSettings::DevBrother || Row.Id == CXSettings::DevSister)
		{
			const int32 PlayerIndex = Row.Id == CXSettings::DevBrother ? 0 : 1;
			Row.ValueText->SetText(FText::FromString(CXSettings::DeviceLabel(Settings->GetSlotDevice(PlayerIndex))));
			Row.ValueText->SetColorAndOpacity(FSlateColor(bLocked ? CXMenuStyle::TextDim : CXMenuStyle::PlayerColor(PlayerIndex)));
			const bool bConnected = IsSlotConnected(PlayerIndex);
			Row.StatusText->SetText(FText::FromString(bConnected ? TEXT("CONNECTED") : TEXT("NOT CONNECTED")));
			Row.StatusText->SetColorAndOpacity(FSlateColor(bConnected ? CXMenuStyle::TextDim : CXMenuStyle::Warning));
			Row.StatusText->SetVisibility(ESlateVisibility::HitTestInvisible);
			// 보기 전용 자물쇠
			Row.Lock->GetParent()->SetVisibility(bLocked ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		else if (Row.Id == CXSettings::CtrlCharacter)
		{
			Row.ValueText->SetText(FText::FromString(ControlsCharacter == 0 ? TEXT("BROTHER") : TEXT("SISTER")));
			Row.ValueText->SetColorAndOpacity(FSlateColor(CXMenuStyle::PlayerColor(ControlsCharacter)));
		}
		else if (Row.Id == CXSettings::CtrlDevice)
		{
			Row.ValueText->SetText(FText::FromString(CXSettings::DeviceLabel(ControlsDevice)));
			// 현재 조작 장치 설정과 같은 장치면 표시
			const bool bCurrent = Settings->GetSlotDevice(ControlsCharacter) == ControlsDevice;
			Row.StatusText->SetText(FText::FromString(bCurrent ? TEXT("CURRENT DEVICE") : TEXT("")));
			Row.StatusText->SetColorAndOpacity(FSlateColor(CXMenuStyle::TextDim));
			Row.StatusText->SetVisibility(bCurrent ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}

	if (DeviceNoteText)
	{
		// 필요한 게임패드 수 / 연결된 수 + 보기 전용 안내
		const int32 Need = Settings->GetRequiredGamepadCount();
		const int32 Have = UCXGameInstance::GetConnectedGamepadCount();
		FString Note = FString::Printf(TEXT("GAMEPADS REQUIRED: %d    CONNECTED: %d"), Need, Have);
		if (!bAllowDeviceChange)
		{
			Note += TEXT("\nVIEW ONLY — ") + FString(CXSettings::MsgDevicesLocked) + TEXT(".");
		}
		DeviceNoteText->SetText(FText::FromString(Note));
		DeviceNoteText->SetColorAndOpacity(FSlateColor(Have < Need ? CXMenuStyle::Warning : CXMenuStyle::TextDim));
	}
}

void UCXSettingsWidget::RefreshTabs()
{
	const FLinearColor Color = CXMenuStyle::PlayerColor(FocusPlayer);
	for (int32 i = 0; i < Tabs.Num(); ++i)
	{
		Tabs[i]->SetTabState(i == CurrentTab, bTabFocused && i == CurrentTab, Color);
	}
}

void UCXSettingsWidget::RefreshControlsTable()
{
	if (ControlActionTexts.IsEmpty())
	{
		return;
	}
	const bool bBrother = ControlsCharacter == 0;
	const CXSettings::FControlLine* Lines = bBrother ? CXSettings::BrotherLines : CXSettings::SisterLines;
	const int32 NumLines = bBrother ? UE_ARRAY_COUNT(CXSettings::BrotherLines) : UE_ARRAY_COUNT(CXSettings::SisterLines);
	const bool bKeyboard = ControlsDevice == ECXInputDeviceType::KeyboardMouse;
	const FLinearColor Color = CXMenuStyle::PlayerColor(ControlsCharacter);

	for (int32 i = 0; i < ControlActionTexts.Num(); ++i)
	{
		const bool bShown = i < NumLines;
		const ESlateVisibility Vis = bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		ControlActionTexts[i]->SetVisibility(Vis);
		ControlKeyTexts[i]->SetVisibility(Vis);
		ControlNoteTexts[i]->SetVisibility(Vis);
		if (!bShown)
		{
			continue;
		}
		const CXSettings::FControlLine& Line = Lines[i];
		ControlActionTexts[i]->SetText(FText::FromString(Line.Action));
		ControlKeyTexts[i]->SetText(FText::FromString(bKeyboard ? Line.Keyboard : Line.Gamepad));
		ControlKeyTexts[i]->SetColorAndOpacity(FSlateColor(FCString::Strlen(Line.Note) > 0 ? CXMenuStyle::TextDim : Color));
		ControlNoteTexts[i]->SetText(FText::FromString(Line.Note));
	}
	// 근접 자동 조준 팁(오빠만)
	ControlsTipText->SetText(FText::FromString(bBrother ? TEXT("TIP: NEARBY ENEMIES ARE AUTO-TARGETED.") : TEXT("TIP: HACKABLE TERMINALS LIGHT UP WHEN THE SISTER IS IN RANGE.")));
	ControlsTipText->SetColorAndOpacity(FSlateColor(Color.CopyWithNewOpacity(0.8f)));
}

void UCXSettingsWidget::RefreshDescription()
{
	if (!FlashText.IsEmpty())
	{
		// 거부 안내는 설명줄을 잠시 주황으로 덮는다
		DescriptionText->SetText(FText::FromString(FlashText));
		DescriptionText->SetColorAndOpacity(FSlateColor(CXMenuStyle::Warning));
		return;
	}
	FText Text;
	if (bTabFocused || !Items.IsValidIndex(FocusIndex))
	{
		Text = FText::FromString(CXSettings::TabDescriptions[CurrentTab]);
	}
	else if (const FCXSettingsRow* Row = FindRow(Items[FocusIndex]->GetItemId()))
	{
		const bool bLocked = !bAllowDeviceChange && (Row->Id == CXSettings::DevBrother || Row->Id == CXSettings::DevSister);
		Text = bLocked ? FText::FromString(CXSettings::MsgDevicesLocked) : Row->Description;
	}
	DescriptionText->SetText(Text);
	DescriptionText->SetColorAndOpacity(FSlateColor(CXMenuStyle::Text));
}

void UCXSettingsWidget::RefreshHint()
{
	// 마지막 입력 장치에 맞는 힌트(3장)
	if (bLastInputGamepad)
	{
		HintText->SetText(FText::FromString(TEXT("[A] SELECT   [B] BACK   [< >] ADJUST   [LB / RB] TAB")));
		TabHintLeft->SetText(FText::FromString(TEXT("LB")));
		TabHintRight->SetText(FText::FromString(TEXT("RB")));
	}
	else
	{
		HintText->SetText(FText::FromString(TEXT("[ENTER] SELECT   [ESC] BACK   [< >] ADJUST   [Q / E] TAB")));
		TabHintLeft->SetText(FText::FromString(TEXT("Q")));
		TabHintRight->SetText(FText::FromString(TEXT("E")));
	}
}

void UCXSettingsWidget::FlashMessage(const FString& Message)
{
	FlashText = Message;
	FlashRemaining = CXSettings::FlashTime;
	RefreshDescription();
}

FString UCXSettingsWidget::GetDebugState() const
{
	const UCXGameUserSettings* Settings = CXSettings::Settings();
	return FString::Printf(TEXT("%s tab=%d tabFocus=%d vol=[%d,%d,%d] split=%d dev=[%d,%d] allowDev=%d ctrl=[%d,%d] flash='%s'"),
		*Super::GetDebugState(), CurrentTab, bTabFocused ? 1 : 0,
		Settings->GetMasterVolume(), Settings->GetMusicVolume(), Settings->GetSfxVolume(),
		static_cast<int32>(Settings->GetSplitLayout()),
		static_cast<int32>(Settings->GetSlotDevice(0)), static_cast<int32>(Settings->GetSlotDevice(1)),
		bAllowDeviceChange ? 1 : 0, ControlsCharacter, static_cast<int32>(ControlsDevice), *FlashText);
}
