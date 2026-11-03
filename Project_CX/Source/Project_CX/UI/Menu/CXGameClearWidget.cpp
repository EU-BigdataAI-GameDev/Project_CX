// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXGameClearWidget.h"
#include "CXMenuItemWidget.h"
#include "CXMenuSubsystem.h"
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
#include "Kismet/GameplayStatics.h"

namespace CXClear
{
	const FName QuitToTitle = TEXT("QuitToTitle");

	constexpr float PanelWidth = 520.0f;
	constexpr float PanelHeight = 300.0f;
	constexpr float DimAlpha = 0.7f;
	/** 제목 네온 점등 시간(패널이 뜬 뒤). */
	constexpr float TitleIgniteTime = 0.4f;
}

// ---------------------------------------------------------------------------------------------
// 테두리(청록/마젠타 반반)

int32 UCXClearFrameWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float A = InWidgetStyle.GetColorAndOpacityTint().A;
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	const float HalfW = Size.X * 0.5f;
	const FLinearColor Left = CXMenuStyle::Brother;
	const FLinearColor Right = CXMenuStyle::Sister;
	const FLinearColor LeftLine = Left.CopyWithNewOpacity(0.45f * A);
	const FLinearColor RightLine = Right.CopyWithNewOpacity(0.45f * A);

	// 얇은 테두리(2px): 위/아래는 가운데에서 색이 나뉜다
	constexpr float T = 2.0f;
	CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f(0.0f, 0.0f), FVector2f(HalfW, T), LeftLine);
	CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f(HalfW, 0.0f), FVector2f(Size.X - HalfW, T), RightLine);
	CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f(0.0f, Size.Y - T), FVector2f(HalfW, T), LeftLine);
	CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f(HalfW, Size.Y - T), FVector2f(Size.X - HalfW, T), RightLine);
	CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f(0.0f, 0.0f), FVector2f(T, Size.Y), LeftLine);
	CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f(Size.X - T, 0.0f), FVector2f(T, Size.Y), RightLine);

	// 모서리 브래킷(3px, 16px) — 왼쪽 청록 / 오른쪽 마젠타
	constexpr float BT = 3.0f;
	constexpr float BL = 16.0f;
	const FLinearColor LeftB = Left.CopyWithNewOpacity(A);
	const FLinearColor RightB = Right.CopyWithNewOpacity(A);
	const int32 L = LayerId + 1;
	CXMenuStyle::DrawRect(OutDrawElements, L, AllottedGeometry, FVector2f(0.0f, 0.0f), FVector2f(BL, BT), LeftB);
	CXMenuStyle::DrawRect(OutDrawElements, L, AllottedGeometry, FVector2f(0.0f, 0.0f), FVector2f(BT, BL), LeftB);
	CXMenuStyle::DrawRect(OutDrawElements, L, AllottedGeometry, FVector2f(0.0f, Size.Y - BT), FVector2f(BL, BT), LeftB);
	CXMenuStyle::DrawRect(OutDrawElements, L, AllottedGeometry, FVector2f(0.0f, Size.Y - BL), FVector2f(BT, BL), LeftB);
	CXMenuStyle::DrawRect(OutDrawElements, L, AllottedGeometry, FVector2f(Size.X - BL, 0.0f), FVector2f(BL, BT), RightB);
	CXMenuStyle::DrawRect(OutDrawElements, L, AllottedGeometry, FVector2f(Size.X - BT, 0.0f), FVector2f(BT, BL), RightB);
	CXMenuStyle::DrawRect(OutDrawElements, L, AllottedGeometry, FVector2f(Size.X - BL, Size.Y - BT), FVector2f(BL, BT), RightB);
	CXMenuStyle::DrawRect(OutDrawElements, L, AllottedGeometry, FVector2f(Size.X - BT, Size.Y - BL), FVector2f(BT, BL), RightB);

	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, L, InWidgetStyle, bParentEnabled);
}

// ---------------------------------------------------------------------------------------------
// 위젯 트리

void UCXGameClearWidget::BuildMenu()
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
	auto MakeRuleHalf = [this](const FLinearColor& Color)
	{
		// 구분선 반쪽(1px). 크기는 브러시로 지정(Slate 위젯 생성 전 SetDesiredSizeOverride 무시됨)
		UImage* Rule = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		FSlateBrush Brush;
		Brush.ImageSize = FVector2D(1.0f, 1.0f);
		Rule->SetBrush(Brush);
		Rule->SetColorAndOpacity(Color.CopyWithNewOpacity(0.35f));
		return Rule;
	};

	// 1) 정지된 분할 화면 위 검정 70% 딤 + 스캔라인(ESC 메뉴 딤 재사용, 마우스가 게임으로 새지 않게 Visible)
	Dim = WidgetTree->ConstructWidget<UCXPauseDimWidget>(UCXPauseDimWidget::StaticClass(), TEXT("Dim"));
	Dim->SetDimAlpha(CXClear::DimAlpha);
	AddFill(Dim);

	// 2) 화면 정중앙 패널(분할선 위에 하나만)
	PanelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelBox"));
	PanelBox->SetWidthOverride(CXClear::PanelWidth);
	PanelBox->SetHeightOverride(CXClear::PanelHeight);
	PanelBox->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(PanelBox))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Center);
	}
	UOverlay* PanelStack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PanelStack"));
	PanelBox->AddChild(PanelStack);

	UBorder* PanelBg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBg"));
	PanelBg->SetBrushColor(CXMenuStyle::Panel);
	PanelBg->SetPadding(FMargin(36.0f, 28.0f));
	if (UOverlaySlot* S = PanelStack->AddChildToOverlay(PanelBg))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}
	UCXClearFrameWidget* Frame = WidgetTree->ConstructWidget<UCXClearFrameWidget>(UCXClearFrameWidget::StaticClass(), TEXT("Frame"));
	Frame->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* S = PanelStack->AddChildToOverlay(Frame))
	{
		S->SetHorizontalAlignment(HAlign_Fill);
		S->SetVerticalAlignment(VAlign_Fill);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	PanelBg->SetContent(Column);

	auto AddFillSpacer = [this, Column]()
	{
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass())))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	};
	AddFillSpacer();

	// 제목 COMING SOON(흰색 90%) — 진입 시 네온 점등
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	TitleText->SetText(FText::FromString(TEXT("COMING SOON")));
	TitleText->SetFont(CXMenuStyle::Font(36, TEXT("Bold"), 240));
	TitleText->SetColorAndOpacity(FSlateColor(CXMenuStyle::Text));
	TitleText->SetJustification(ETextJustify::Center);
	TitleText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	TitleText->SetShadowColorAndOpacity(CXMenuStyle::Sister.CopyWithNewOpacity(0.35f));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(TitleText))
	{
		S->SetHorizontalAlignment(HAlign_Center);
	}

	// 얇은 구분선(왼쪽 청록 / 오른쪽 마젠타)
	UHorizontalBox* Rule = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Rule"));
	for (const FLinearColor& Color : { CXMenuStyle::Brother, CXMenuStyle::Sister })
	{
		if (UHorizontalBoxSlot* S = Rule->AddChildToHorizontalBox(MakeRuleHalf(Color)))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Rule))
	{
		S->SetPadding(FMargin(0.0f, 18.0f, 0.0f, 26.0f));
	}

	// 유일한 버튼
	UCXMenuItemWidget* Item = AddMenuItem(nullptr, CXClear::QuitToTitle, FText::FromString(TEXT("QUIT TO TITLE")));
	Item->Setup(CXClear::QuitToTitle, FText::FromString(TEXT("QUIT TO TITLE")), false, 300.0f, 28);
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Item))
	{
		S->SetHorizontalAlignment(HAlign_Center);
	}
	AddFillSpacer();

	// 3) 하단 힌트(선택만 — 뒤로 힌트 없음)
	HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Hint"));
	HintText->SetText(FText::FromString(TEXT("[A / ENTER]  SELECT")));
	HintText->SetFont(CXMenuStyle::Font(14, TEXT("Regular"), 160));
	HintText->SetColorAndOpacity(FSlateColor(CXMenuStyle::TextDim));
	HintText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(HintText))
	{
		S->SetHorizontalAlignment(HAlign_Right);
		S->SetVerticalAlignment(VAlign_Bottom);
		S->SetPadding(FMargin(0.0f, 0.0f, 64.0f, 48.0f));
	}

	// 4) 타이틀로 이동 검정 페이드
	FadeImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Fade"));
	FadeImage->SetColorAndOpacity(FLinearColor::Black);
	FadeImage->SetRenderOpacity(0.0f);
	FadeImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	AddFill(FadeImage);
}

// ---------------------------------------------------------------------------------------------
// 수명 / 연출

void UCXGameClearWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 시작 포커스 색 = 키보드 사용자(이후 마지막 입력 플레이어)
	if (const UCXGameInstance* GI = UCXGameInstance::Get(this))
	{
		FocusPlayer = GI->ResolvePlayerForInput(false, INPUTDEVICEID_NONE);
	}
	SetFocusItem(CXClear::QuitToTitle);

	Elapsed = 0.0f;
	bQuitting = false;
	Dim->SetRenderOpacity(0.0f);
	PanelBox->SetRenderOpacity(0.0f);
	HintText->SetRenderOpacity(0.0f);
	// 진입 연출(딤 0.5초 + 패널 0.15초) 동안 메뉴 입력 무시 — 마지막 전투 직후 연타로 넘어가지 않게
	LockInput(DimInTime + CXMenuStyle::OpenTime);
}

void UCXGameClearWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	// 월드가 정지돼도 위젯은 Slate 틱(실시간)으로 돈다
	Super::NativeTick(MyGeometry, InDeltaTime);
	Elapsed += InDeltaTime;

	// 0 ~ DimInTime: 딤이 깔림
	Dim->SetRenderOpacity(FMath::Clamp(Elapsed / FMath::Max(0.01f, DimInTime), 0.0f, 1.0f));

	// 이후 0.15초: 패널 페이드 + 세로 0.96→1.0(CRT 켜짐 느낌)
	const float PanelT = Elapsed - DimInTime;
	const float PanelAlpha = FMath::Clamp(PanelT / CXMenuStyle::OpenTime, 0.0f, 1.0f);
	PanelBox->SetRenderOpacity(PanelAlpha);
	HintText->SetRenderOpacity(PanelAlpha);
	FWidgetTransform Transform;
	Transform.Scale = FVector2D(1.0f, FMath::Lerp(0.96f, 1.0f, PanelAlpha));
	PanelBox->SetRenderTransform(Transform);

	// 제목 네온 점등(0.4초, 짧은 깜박임 2회) 후 고정 — 시작화면 게임명 점등과 같은 문법
	float TitleOpacity = 1.0f;
	if (PanelT < CXClear::TitleIgniteTime)
	{
		const bool bFlickerOff = (PanelT >= 0.10f && PanelT < 0.16f) || (PanelT >= 0.24f && PanelT < 0.30f);
		TitleOpacity = PanelT < 0.0f ? 0.0f : (bFlickerOff ? 0.15f : FMath::Clamp(PanelT / 0.08f, 0.0f, 1.0f));
	}
	TitleText->SetRenderOpacity(TitleOpacity);

	// 타이틀로: 0.4초 검정 페이드 후 시작화면 맵
	if (bQuitting)
	{
		QuitElapsed += InDeltaTime;
		FadeImage->SetRenderOpacity(FMath::Clamp(QuitElapsed / FMath::Max(0.01f, QuitFadeTime), 0.0f, 1.0f));
		if (QuitElapsed >= QuitFadeTime && !TitleMap.IsEmpty())
		{
			const FString Map = MoveTemp(TitleMap);
			TitleMap.Reset();
			UGameplayStatics::OpenLevel(this, FName(*Map));
		}
	}
}

bool UCXGameClearWidget::CanAcceptInput() const
{
	// 진입 연출 / 타이틀 페이드 중 입력 무시(소비만)
	return Super::CanAcceptInput() && Elapsed >= DimInTime + CXMenuStyle::OpenTime && !bQuitting;
}

// ---------------------------------------------------------------------------------------------
// 입력 / 동작

bool UCXGameClearWidget::OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex)
{
	switch (Input.Action)
	{
	case ECXMenuAction::Start:
	case ECXMenuAction::Up:
	case ECXMenuAction::Down:
	case ECXMenuAction::Left:
	case ECXMenuAction::Right:
	case ECXMenuAction::TabLeft:
	case ECXMenuAction::TabRight:
		// 순환할 대상이 없고 Start도 아무 동작 없음 — 소비만, 포커스 색은 누른 사람 색으로
		if (!Input.bRepeat && FocusPlayer != PlayerIndex)
		{
			FocusPlayer = PlayerIndex;
			RefreshFocusVisuals();
		}
		return true;
	default:
		return false;
	}
}

void UCXGameClearWidget::OnItemActivated(FName ItemId)
{
	if (ItemId == CXClear::QuitToTitle)
	{
		// 확인창 없음(클리어 시점에는 잃을 진행이 없다 — 세이브는 클리어 판정 때 이미 삭제)
		PlayUISound(ECXMenuSound::Confirm);
		BeginQuitToTitle();
	}
}

void UCXGameClearWidget::BeginQuitToTitle()
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
		// bMenuBlocked는 맵 이동(PreLoadMap) 때 서브시스템이 푼다
		Menus->NotifyLeavingMap();
	}
	// 화면 페이드와 같은 시간으로 남은 음악 페이드아웃
	if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
	{
		Audio->StopMusic(QuitFadeTime);
	}
}

FString UCXGameClearWidget::GetDebugState() const
{
	return FString::Printf(TEXT("%s elapsed=%.2f quitting=%d accept=%d"), *Super::GetDebugState(), Elapsed, bQuitting ? 1 : 0, CanAcceptInput() ? 1 : 0);
}
