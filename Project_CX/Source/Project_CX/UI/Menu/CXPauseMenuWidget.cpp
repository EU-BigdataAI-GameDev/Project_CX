// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXPauseMenuWidget.h"
#include "CXMenuItemWidget.h"
#include "CXMenuSubsystem.h"
#include "CXNeonPanelWidget.h"
#include "CXGameInstance.h"
#include "CXSettingsWidget.h"
#include "CXAudioSubsystem.h"
#include "CoopHUD.h"
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
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"

namespace CXPause
{
	const FName Resume = TEXT("Resume");
	const FName Settings = TEXT("Settings");
	const FName QuitToTitle = TEXT("QuitToTitle");

	constexpr float PanelWidth = 520.0f;
	constexpr float PanelHeight = 400.0f;
	constexpr int32 ItemFontSize = 28;
	constexpr float TagPollInterval = 0.5f;
	/** 스캔라인 간격(px)/진하기. */
	constexpr float ScanlineStep = 3.0f;
	constexpr float ScanlineAlpha = 0.18f;
}

// ---------------------------------------------------------------------------------------------
// 딤 + 스캔라인

int32 UCXPauseDimWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float Opacity = InWidgetStyle.GetColorAndOpacityTint().A;
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f::ZeroVector, Size, CXMenuStyle::Dim.CopyWithNewOpacity(DimAlpha * Opacity));

	// 가로 스캔라인(1px, 3px 간격) — 정지 화면이 "모니터 위 화면"처럼 읽히게
	const FLinearColor Line(0.0f, 0.0f, 0.0f, CXPause::ScanlineAlpha * Opacity);
	for (float Y = 0.0f; Y < Size.Y; Y += CXPause::ScanlineStep)
	{
		CXMenuStyle::DrawRect(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2f(0.0f, Y), FVector2f(Size.X, 1.0f), Line);
	}
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
}

// ---------------------------------------------------------------------------------------------
// 위젯 트리

void UCXPauseMenuWidget::BuildMenu()
{
	RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = RootOverlay;

	auto AddFill = [this](UWidget* W)
	{
		if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(W))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}
	};
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
		// 패널 구분선(1px). 크기는 브러시로 지정(Slate 위젯 생성 전 SetDesiredSizeOverride 무시됨)
		UImage* Rule = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush Brush;
		Brush.ImageSize = FVector2D(1.0f, 1.0f);
		Rule->SetBrush(Brush);
		Rule->SetColorAndOpacity(CXMenuStyle::Brother.CopyWithNewOpacity(0.3f));
		return Rule;
	};

	// 1) 딤 + 스캔라인(분할 화면 전체)
	AddFill(WidgetTree->ConstructWidget<UCXPauseDimWidget>(UCXPauseDimWidget::StaticClass(), TEXT("Dim")));

	// 2) 화면 정중앙: 경고 + 패널(분할선 위에 하나만)
	UVerticalBox* CenterColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CenterColumn"));
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(CenterColumn))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Center);
	}

	WarningText = MakeText(TEXT(""), 20, TEXT("Bold"), 160, CXMenuStyle::Warning);
	WarningText->SetJustification(ETextJustify::Center);
	WarningText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	WarningText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	WarningText->SetVisibility(ESlateVisibility::Hidden);	// 자리는 유지 → 경고가 떠도 패널이 움직이지 않음
	if (UVerticalBoxSlot* S = CenterColumn->AddChildToVerticalBox(WarningText))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 18.0f));
	}

	PanelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelBox"));
	PanelBox->SetWidthOverride(CXPause::PanelWidth);
	PanelBox->SetHeightOverride(CXPause::PanelHeight);
	PanelBox->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	if (UVerticalBoxSlot* S = CenterColumn->AddChildToVerticalBox(PanelBox))
	{
		S->SetHorizontalAlignment(HAlign_Center);
	}

	UCXNeonPanelWidget* Panel = WidgetTree->ConstructWidget<UCXNeonPanelWidget>(UCXNeonPanelWidget::StaticClass(), TEXT("Panel"));
	Panel->SetPanelPadding(FMargin(36.0f, 30.0f, 36.0f, 24.0f));
	Panel->SetFrameColor(CXMenuStyle::Brother);
	PanelBox->AddChild(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetPanelContent(Column);

	// 제목 PAUSED + 1초 주기 깜박이는 작은 사각형(녹화 정지 느낌)
	UHorizontalBox* TitleRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TitleRow"));
	Column->AddChildToVerticalBox(TitleRow);
	UTextBlock* TitleText = MakeText(TEXT("PAUSED"), 36, TEXT("Bold"), 300, FLinearColor::White);
	TitleText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	TitleText->SetShadowColorAndOpacity(CXMenuStyle::Brother.CopyWithNewOpacity(0.45f));
	if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(TitleText))
	{
		S->SetVerticalAlignment(VAlign_Center);
	}
	BlinkSquare = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BlinkSquare"));
	{
		FSlateBrush Brush;
		Brush.ImageSize = FVector2D(12.0f, 12.0f);
		BlinkSquare->SetBrush(Brush);
	}
	BlinkSquare->SetColorAndOpacity(CXMenuStyle::Warning);
	if (UHorizontalBoxSlot* S = TitleRow->AddChildToHorizontalBox(BlinkSquare))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(FMargin(16.0f, 4.0f, 0.0f, 0.0f));
	}

	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MakeRule()))
	{
		S->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 16.0f));
	}

	// 항목
	UVerticalBox* MenuList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuList"));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MenuList))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	const TPair<FName, const TCHAR*> ItemDefs[] = {
		{ CXPause::Resume, TEXT("RESUME") },
		{ CXPause::Settings, TEXT("SETTINGS") },
		{ CXPause::QuitToTitle, TEXT("QUIT TO TITLE") },
	};
	for (const TPair<FName, const TCHAR*>& Def : ItemDefs)
	{
		UCXMenuItemWidget* Item = AddMenuItem(MenuList, Def.Key, FText::FromString(Def.Value));
		Item->Setup(Def.Key, FText::FromString(Def.Value), false, 0.0f, CXPause::ItemFontSize);
	}

	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MakeRule()))
	{
		S->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 12.0f));
	}

	// 하단 정보줄: 현재 구역명(세이브포인트 시스템이 생기면 "마지막 저장" 추가)
	InfoText = MakeText(TEXT(""), 15, TEXT("Regular"), 160, CXMenuStyle::TextDim);
	Column->AddChildToVerticalBox(InfoText);

	// 3) 화면 하단: 플레이어 장치 태그(좌) + 조작 힌트(우). 아래 HUD 패널과 겹쳐도 읽히게 어두운 띠 위에 둔다.
	UBorder* FooterStrip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("FooterStrip"));
	FooterStrip->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.72f));
	FooterStrip->SetPadding(FMargin(60.0f, 16.0f, 60.0f, 18.0f));
	FooterStrip->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(FooterStrip))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Bottom);
		S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 24.0f));
	}
	UHorizontalBox* Footer = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Footer"));
	FooterStrip->SetContent(Footer);
	const TCHAR* TagNames[UCXGameInstance::NumPlayerSlots] = { TEXT("P1  BROTHER"), TEXT("P2  SISTER") };
	for (int32 i = 0; i < UCXGameInstance::NumPlayerSlots; ++i)
	{
		const FLinearColor Color = CXMenuStyle::PlayerColor(i);
		UImage* Square = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		{
			FSlateBrush Brush;
			Brush.ImageSize = FVector2D(10.0f, 10.0f);
			Square->SetBrush(Brush);
		}
		Square->SetColorAndOpacity(Color);
		if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(Square))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(i == 0 ? 0.0f : 56.0f, 0.0f, 10.0f, 0.0f));
		}
		if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(MakeText(TagNames[i], 16, TEXT("Bold"), 200, Color)))
		{
			S->SetVerticalAlignment(VAlign_Center);
		}
		UTextBlock* DeviceText = MakeText(TEXT(""), 16, TEXT("Regular"), 160, CXMenuStyle::Text);
		if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(DeviceText))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(18.0f, 0.0f, 0.0f, 0.0f));
		}
		UTextBlock* StatusText = MakeText(TEXT(""), 16, TEXT("Bold"), 160, CXMenuStyle::TextDim);
		if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(StatusText))
		{
			S->SetVerticalAlignment(VAlign_Center);
			S->SetPadding(FMargin(14.0f, 0.0f, 0.0f, 0.0f));
		}
		TagDeviceTexts.Add(DeviceText);
		TagStatusTexts.Add(StatusText);
	}
	USpacer* FooterFill = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(FooterFill))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(MakeText(TEXT("[A / ENTER]  SELECT        [B / ESC]  BACK"), 14, TEXT("Regular"), 160, CXMenuStyle::TextDim)))
	{
		S->SetVerticalAlignment(VAlign_Center);
	}

	// 4) 확인창 레이어
	ModalLayer = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ModalLayer"));
	AddFill(ModalLayer);
	SetModalHost(ModalLayer);

	// 5) 타이틀로 이동 검정 페이드
	FadeImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Fade"));
	FadeImage->SetColorAndOpacity(FLinearColor::Black);
	FadeImage->SetRenderOpacity(0.0f);
	FadeImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	AddFill(FadeImage);
}

// ---------------------------------------------------------------------------------------------
// 수명

void UCXPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 메뉴를 연 사람(키보드면 키보드 슬롯) 색으로 시작
	if (const UCXGameInstance* GI = UCXGameInstance::Get(this))
	{
		FocusPlayer = GI->ResolvePlayerForInput(false, INPUTDEVICEID_NONE);
	}
	SetFocusItem(CXPause::Resume);
	InfoText->SetText(FText::FromString(FString::Printf(TEXT("%s  ·  DISTRICT 5"), *ResolveZoneName())));

	bClosing = bQuitting = false;
	OpenAlpha = 0.0f;
	Clock = 0.0f;
	RootOverlay->SetRenderOpacity(0.0f);
	RefreshConnectionState();
	RefreshPlayerTags();

	// 두 사람이 동시에 ESC/Start를 눌러도 바로 닫히지 않게(6장)
	LockInput();
	PlayUISound(ECXMenuSound::Open);
}

void UCXPauseMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	// 월드는 정지 상태지만 위젯은 Slate 틱(실시간)으로 돈다.
	Super::NativeTick(MyGeometry, InDeltaTime);
	Clock += InDeltaTime;

	// 열기: 0.15초 페이드 + 패널 세로 0.96→1.0(CRT 켜짐 느낌)
	if (!bClosing && OpenAlpha < 1.0f)
	{
		OpenAlpha = FMath::Min(1.0f, OpenAlpha + InDeltaTime / CXMenuStyle::OpenTime);
		RootOverlay->SetRenderOpacity(OpenAlpha);
		FWidgetTransform Transform;
		Transform.Scale = FVector2D(1.0f, FMath::Lerp(0.96f, 1.0f, OpenAlpha));
		PanelBox->SetRenderTransform(Transform);
	}

	// 닫기: 0.15초 페이드아웃 후 서브시스템이 메뉴를 내리고 재개
	if (bClosing)
	{
		CloseElapsed += InDeltaTime;
		const float A = 1.0f - FMath::Clamp(CloseElapsed / CXMenuStyle::OpenTime, 0.0f, 1.0f);
		RootOverlay->SetRenderOpacity(FMath::Min(A, OpenAlpha));
		if (CloseElapsed >= CXMenuStyle::OpenTime)
		{
			bClosing = false;
			if (UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this))
			{
				Menus->FinishClosePauseMenu(this);	// 이 안에서 RemoveFromParent
			}
			return;
		}
	}

	// 타이틀로: 0.4초 검정 페이드 후 시작화면 맵
	if (bQuitting)
	{
		QuitElapsed += InDeltaTime;
		FadeImage->SetRenderOpacity(FMath::Clamp(QuitElapsed / QuitFadeTime, 0.0f, 1.0f));
		if (QuitElapsed >= QuitFadeTime && !TitleMap.IsEmpty())
		{
			const FString Map = MoveTemp(TitleMap);
			TitleMap.Reset();
			UGameplayStatics::OpenLevel(this, FName(*Map));
		}
	}

	// 확인창/알림창이 열리면 뒤 패널을 흐리게(반투명 확인창 너머로 항목 글자가 겹쳐 보이지 않게)
	PanelBox->SetRenderOpacity(IsModalOpen() ? 0.12f : 1.0f);

	// 1초 주기 깜박임
	BlinkSquare->SetRenderOpacity(FMath::Fmod(Clock, 1.0f) < 0.5f ? 1.0f : 0.0f);

	// 연결 끊김 경고는 펄스
	if (bShowingDisconnect)
	{
		WarningText->SetRenderOpacity(0.6f + 0.4f * FMath::Cos(Clock * UE_TWO_PI * 1.2f));
	}

	TagPollTimer -= InDeltaTime;
	if (TagPollTimer <= 0.0f)
	{
		TagPollTimer = CXPause::TagPollInterval;
		RefreshPlayerTags();
	}
}

bool UCXPauseMenuWidget::CanAcceptInput() const
{
	// 닫는 중/타이틀 페이드 중에는 모든 메뉴 입력 무시(4장)
	return Super::CanAcceptInput() && !bClosing && !bQuitting;
}

// ---------------------------------------------------------------------------------------------
// 입력 / 동작

bool UCXPauseMenuWidget::OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex)
{
	if (Input.Action == ECXMenuAction::Start)
	{
		// 게임패드 Start = 다시 누르면 닫기
		if (!Input.bRepeat)
		{
			FocusPlayer = PlayerIndex;
			if (bShowingDisconnect)
			{
				PlayUISound(ECXMenuSound::Denied);
			}
			else
			{
				PlayUISound(ECXMenuSound::Back);
				RequestResume();
			}
		}
		return true;
	}
	return false;
}

bool UCXPauseMenuWidget::OnBack()
{
	// 최상위 ESC/B = RESUME. 연결 끊김 경고 중에는 닫지 않는다(소비만).
	if (bShowingDisconnect)
	{
		PlayUISound(ECXMenuSound::Denied);
		return false;
	}
	RequestResume();
	return true;
}

void UCXPauseMenuWidget::OnItemActivated(FName ItemId)
{
	if (ItemId == CXPause::Resume)
	{
		PlayUISound(ECXMenuSound::Confirm);
		RequestResume();
	}
	else if (ItemId == CXPause::Settings)
	{
		OpenSettings();
	}
	else if (ItemId == CXPause::QuitToTitle)
	{
		// 세이브포인트 시스템이 없는 동안의 본문(7장). 여기서는 아무것도 저장하지 않는다.
		ShowConfirm(FText::FromString(TEXT("RETURN TO TITLE?")),
			FText::FromString(TEXT("PROGRESS ON THIS MAP WILL NOT BE SAVED.")),
			[this]() { BeginQuitToTitle(); });
	}
}

void UCXPauseMenuWidget::OpenSettings()
{
	PlayUISound(ECXMenuSound::Confirm);
	// 설정 메뉴가 자체 딤/패널을 그리므로 ESC 메뉴는 잠시 숨긴다(월드는 계속 일시정지)
	RootVisibilityBeforeSettings = RootOverlay->GetVisibility();
	RootOverlay->SetVisibility(ESlateVisibility::Hidden);

	TWeakObjectPtr<UCXPauseMenuWidget> WeakThis(this);
	// 인게임에서는 조작 장치 보기 전용(설정 메뉴 기획 1장)
	const UCXSettingsWidget* Settings = UCXSettingsWidget::Open(this, false, [WeakThis]()
	{
		if (UCXPauseMenuWidget* Pause = WeakThis.Get())
		{
			Pause->RootOverlay->SetVisibility(Pause->RootVisibilityBeforeSettings);
			// 들어갔던 항목에 포커스 복원(0.3)
			Pause->SetFocusItem(CXPause::Settings);
			Pause->RefreshPlayerTags();
		}
	});
	if (!Settings)
	{
		RootOverlay->SetVisibility(RootVisibilityBeforeSettings);
	}
}

void UCXPauseMenuWidget::SetOpenedBy(int32 PlayerIndex)
{
	if (PlayerIndex >= 0 && PlayerIndex < UCXGameInstance::NumPlayerSlots)
	{
		FocusPlayer = PlayerIndex;
		RefreshFocusVisuals();
	}
}

void UCXPauseMenuWidget::RequestResume()
{
	if (bClosing || bQuitting || bShowingDisconnect)
	{
		return;
	}
	bClosing = true;
	CloseElapsed = 0.0f;
}

void UCXPauseMenuWidget::BeginQuitToTitle()
{
	if (bQuitting)
	{
		return;
	}
	bQuitting = true;
	QuitElapsed = 0.0f;
	if (UCXGameInstance* GI = UCXGameInstance::Get(this))
	{
		// 타이틀로 돌아가는 경우는 진입 연출 축약(0.3초 페이드)
		GI->bTitleIntroPlayed = true;
	}
	if (UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this))
	{
		Menus->NotifyLeavingMap();
	}
	// 화면 페이드와 같이 배경음도 내린다
	if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
	{
		Audio->StopMusic(QuitFadeTime);
	}
}

// ---------------------------------------------------------------------------------------------
// 연결 상태 / 정보줄

void UCXPauseMenuWidget::RefreshConnectionState()
{
	const UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this);
	const int32 Disconnected = Menus ? Menus->GetDisconnectedPlayer() : INDEX_NONE;
	const bool bWasShowing = bShowingDisconnect;
	bShowingDisconnect = Disconnected != INDEX_NONE;

	if (bShowingDisconnect)
	{
		// 재연결 전에는 RESUME 숨김(SETTINGS / QUIT TO TITLE은 가능)
		WarningText->SetText(FText::FromString(FString::Printf(TEXT("P%d CONTROLLER DISCONNECTED — PLEASE RECONNECT"), Disconnected + 1)));
		WarningText->SetColorAndOpacity(FSlateColor(CXMenuStyle::Warning));
		WarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
		SetItemShown(CXPause::Resume, false);
		if (!bWasShowing)
		{
			PlayUISound(ECXMenuSound::Denied);
		}
	}
	else
	{
		SetItemShown(CXPause::Resume, true);
		if (Menus && Menus->WasReconnected())
		{
			WarningText->SetText(FText::FromString(TEXT("RECONNECTED")));
			WarningText->SetColorAndOpacity(FSlateColor(CXMenuStyle::Brother));
			WarningText->SetRenderOpacity(1.0f);
			WarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			WarningText->SetVisibility(ESlateVisibility::Hidden);
		}
		if (bWasShowing)
		{
			// RESUME 복귀 → 기본 포커스로
			SetFocusItem(CXPause::Resume);
			PlayUISound(ECXMenuSound::Confirm);
		}
	}
	RefreshPlayerTags();
}

bool UCXPauseMenuWidget::IsPlayerConnected(int32 PlayerIndex) const
{
	const UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this);
	if (Menus && Menus->GetDisconnectedPlayer() == PlayerIndex)
	{
		return false;
	}
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	if (GI && GI->GetSlotDeviceType(PlayerIndex) == ECXInputDeviceType::KeyboardMouse)
	{
		return true;
	}
	// 시작화면에서 배정된 패드가 있으면 그 패드 상태, 없으면(맵에서 바로 PIE 등) 아무 패드라도 연결돼 있는지
	const FInputDeviceId Pad = GI ? GI->GetSlotGamepad(PlayerIndex) : INPUTDEVICEID_NONE;
	if (Pad.IsValid())
	{
		return IPlatformInputDeviceMapper::Get().GetInputDeviceConnectionState(Pad) == EInputDeviceConnectionState::Connected;
	}
	return FSlateApplication::IsInitialized() && FSlateApplication::Get().IsGamepadAttached();
}

void UCXPauseMenuWidget::RefreshPlayerTags()
{
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	for (int32 i = 0; i < TagDeviceTexts.Num(); ++i)
	{
		const ECXInputDeviceType Device = GI ? GI->GetSlotDeviceType(i) : (i == 0 ? ECXInputDeviceType::KeyboardMouse : ECXInputDeviceType::Gamepad);
		const bool bKeyboard = Device == ECXInputDeviceType::KeyboardMouse;
		TagDeviceTexts[i]->SetText(FText::FromString(bKeyboard ? TEXT("KEYBOARD & MOUSE") : TEXT("GAMEPAD")));

		const bool bConnected = IsPlayerConnected(i);
		TagStatusTexts[i]->SetText(FText::FromString(bConnected ? TEXT("CONNECTED") : TEXT("NOT CONNECTED")));
		TagStatusTexts[i]->SetColorAndOpacity(FSlateColor(bConnected ? CXMenuStyle::TextDim : CXMenuStyle::Warning));
		// 키보드는 연결 상태 표기 생략(항상 연결)
		TagStatusTexts[i]->SetVisibility(bKeyboard ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

FString UCXPauseMenuWidget::ResolveZoneName() const
{
	// ACoopHUD의 구역명(ZoneTitles) 재사용. 협동 HUD가 없는 맵이면 맵 이름.
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (const ACoopHUD* HUD = Cast<ACoopHUD>(PC->GetHUD()))
		{
			if (!HUD->GetZoneTitle().IsEmpty())
			{
				return HUD->GetZoneTitle();
			}
		}
	}
	return UGameplayStatics::GetCurrentLevelName(this, true).ToUpper();
}

FString UCXPauseMenuWidget::GetDebugState() const
{
	return FString::Printf(TEXT("%s closing=%d quitting=%d disconnect=%d resumeShown=%d"), *Super::GetDebugState(),
		bClosing ? 1 : 0, bQuitting ? 1 : 0, bShowingDisconnect ? 1 : 0,
		FindItem(CXPause::Resume) && FindItem(CXPause::Resume)->GetVisibility() != ESlateVisibility::Collapsed ? 1 : 0);
}
