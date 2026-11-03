// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXGameOverWidget.h"
#include "CXMenuItemWidget.h"
#include "CXMenuSubsystem.h"
#include "CXNeonPanelWidget.h"
#include "CXPauseMenuWidget.h"
#include "CXGameInstance.h"
#include "CXAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
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
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace CXGameOver
{
	const FName Retry = TEXT("Retry");
	const FName QuitToTitle = TEXT("QuitToTitle");

	constexpr float PanelWidth = 680.0f;
	constexpr float PanelHeight = 460.0f;
	constexpr float DimAlpha = 0.7f;
	/** 제목 글리치(패널이 뜬 뒤). */
	constexpr float TitleGlitchTime = 0.4f;
	/** 패널이 뜬 뒤 메뉴가 나타나기까지 / 메뉴 페이드 시간. */
	constexpr float MenuDelay = 0.2f;
	constexpr float MenuFadeTime = 0.15f;
	/** 전환 이후에도 남는 약한 노이즈. */
	constexpr float NoiseBase = 0.12f;
}

// ---------------------------------------------------------------------------------------------
// 신호 끊김 노이즈

int32 UCXSignalNoiseWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const float A = InWidgetStyle.GetColorAndOpacityTint().A;
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());

	// 굵은 스캔라인(2px, 6px 간격) — 정지 화면을 "끊긴 모니터"처럼
	const FLinearColor Line(0.0f, 0.0f, 0.0f, 0.28f * A);
	for (float Y = 0.0f; Y < Size.Y; Y += 6.0f)
	{
		CXMenuStyle::DrawRect(OutDrawElements, LayerId, AllottedGeometry, FVector2f(0.0f, Y), FVector2f(Size.X, 2.0f), Line);
	}

	// 무작위 가로 노이즈 띠(매 프레임 새로) — 밝은 띠 + 어두운 띠 + 주황 틴트 약간
	const int32 Bars = FMath::RoundToInt(48.0f * Intensity);
	for (int32 i = 0; i < Bars; ++i)
	{
		const float Y = FMath::FRand() * Size.Y;
		const float H = 1.0f + FMath::FRand() * (2.0f + 10.0f * Intensity);
		const float X = FMath::FRand() < 0.6f ? 0.0f : FMath::FRand() * Size.X * 0.6f;
		const float W = Size.X - X - (FMath::FRand() < 0.5f ? 0.0f : FMath::FRand() * Size.X * 0.4f);
		const float Roll = FMath::FRand();
		FLinearColor C = Roll < 0.45f ? FLinearColor(1.0f, 1.0f, 1.0f, 1.0f) : (Roll < 0.85f ? FLinearColor::Black : CXMenuStyle::Warning);
		C.A = (0.05f + 0.25f * FMath::FRand()) * Intensity * A;
		CXMenuStyle::DrawRect(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2f(X, Y), FVector2f(W, H), C);
	}
	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
}

// ---------------------------------------------------------------------------------------------
// 위젯 트리

void UCXGameOverWidget::BuildMenu()
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
	auto MakeText = [this](const TCHAR* Text, int32 Size, FName Typeface, int32 Spacing, const FLinearColor& Color)
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Block->SetText(FText::FromString(Text));
		Block->SetFont(CXMenuStyle::Font(Size, Typeface, Spacing));
		Block->SetColorAndOpacity(FSlateColor(Color));
		return Block;
	};

	// 1) 정지 화면 위 검정 70% 딤 + 스캔라인(ESC 메뉴 딤 재사용, 마우스가 게임으로 새지 않게 Visible)
	Dim = WidgetTree->ConstructWidget<UCXPauseDimWidget>(UCXPauseDimWidget::StaticClass(), TEXT("Dim"));
	Dim->SetDimAlpha(CXGameOver::DimAlpha);
	AddFill(Dim);

	// 2) 신호 끊김 노이즈(두 칸 동시에 → 전체 화면)
	Noise = WidgetTree->ConstructWidget<UCXSignalNoiseWidget>(UCXSignalNoiseWidget::StaticClass(), TEXT("Noise"));
	Noise->SetVisibility(ESlateVisibility::HitTestInvisible);
	AddFill(Noise);

	// 3) 화면 정중앙 패널(주황 테두리 — 경고 톤)
	PanelBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelBox"));
	PanelBox->SetWidthOverride(CXGameOver::PanelWidth);
	PanelBox->SetHeightOverride(CXGameOver::PanelHeight);
	PanelBox->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(PanelBox))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Center);
	}
	UCXNeonPanelWidget* Panel = WidgetTree->ConstructWidget<UCXNeonPanelWidget>(UCXNeonPanelWidget::StaticClass(), TEXT("Panel"));
	Panel->SetPanelPadding(FMargin(40.0f, 30.0f, 40.0f, 26.0f));
	Panel->SetFrameColor(CXMenuStyle::Warning);
	Panel->SetFrameLineAlpha(0.5f);
	PanelBox->AddChild(Panel);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	Panel->SetPanelContent(Column);

	auto AddFillSpacer = [this, Column]()
	{
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass())))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	};
	AddFillSpacer();

	// 제목 ALL SIGNALS LOST(흰색 90%, 주황 그림자) — 진입 시 글리치
	TitleText = MakeText(TEXT("ALL SIGNALS LOST"), 38, TEXT("Bold"), 240, CXMenuStyle::Text);
	TitleText->SetJustification(ETextJustify::Center);
	TitleText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	TitleText->SetShadowColorAndOpacity(CXMenuStyle::Warning.CopyWithNewOpacity(0.45f));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(TitleText))
	{
		S->SetHorizontalAlignment(HAlign_Center);
	}

	// 주황 구분선(1px). 크기는 브러시로 지정(Slate 위젯 생성 전 SetDesiredSizeOverride 무시됨)
	UImage* Rule = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Rule"));
	FSlateBrush RuleBrush;
	RuleBrush.ImageSize = FVector2D(1.0f, 1.0f);
	Rule->SetBrush(RuleBrush);
	Rule->SetColorAndOpacity(CXMenuStyle::Warning.CopyWithNewOpacity(0.6f));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Rule))
	{
		S->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 14.0f));
	}

	// 마지막으로 쓰러진 플레이어 — 그 플레이어 색(동시 사망이면 두 이름)
	UHorizontalBox* FallenRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("FallenRow"));
	FallenNameA = MakeText(TEXT("BROTHER"), 22, TEXT("Bold"), 160, CXMenuStyle::Brother);
	FallenAmp = MakeText(TEXT(" & "), 22, TEXT("Bold"), 160, CXMenuStyle::Text);
	FallenNameB = MakeText(TEXT("SISTER"), 22, TEXT("Bold"), 160, CXMenuStyle::Sister);
	FallenSuffix = MakeText(TEXT("  —  SIGNAL LOST"), 22, TEXT("Bold"), 160, CXMenuStyle::Brother);
	for (UTextBlock* Part : { FallenNameA.Get(), FallenAmp.Get(), FallenNameB.Get(), FallenSuffix.Get() })
	{
		FallenRow->AddChildToHorizontalBox(Part);
	}
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(FallenRow))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 30.0f));
	}

	// 메뉴 RETRY(기본) / QUIT TO TITLE
	MenuBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuBox"));
	for (const TPair<FName, const TCHAR*>& Entry : { TPair<FName, const TCHAR*>(CXGameOver::Retry, TEXT("RETRY")), TPair<FName, const TCHAR*>(CXGameOver::QuitToTitle, TEXT("QUIT TO TITLE")) })
	{
		UCXMenuItemWidget* Item = AddMenuItem(nullptr, Entry.Key, FText::FromString(Entry.Value));
		Item->Setup(Entry.Key, FText::FromString(Entry.Value), false, 320.0f, 28);
		if (UVerticalBoxSlot* S = MenuBox->AddChildToVerticalBox(Item))
		{
			S->SetHorizontalAlignment(HAlign_Center);
			S->SetPadding(FMargin(0.0f, 2.0f));
		}
	}
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(MenuBox))
	{
		S->SetHorizontalAlignment(HAlign_Center);
	}

	// 재시작 지점 안내(세이브포인트 시스템 없음 → 맵 시작 지점)
	InfoText = MakeText(TEXT("RESTART FROM: MAP START"), 14, TEXT("Regular"), 160, CXMenuStyle::TextDim);
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(InfoText))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetPadding(FMargin(0.0f, 26.0f, 0.0f, 0.0f));
	}
	AddFillSpacer();

	// 4) 상단 연결 끊김 경고(게임패드가 끊겨도 메뉴는 그대로 — 기획 6장)
	WarningText = MakeText(TEXT(""), 18, TEXT("Bold"), 160, CXMenuStyle::Warning);
	WarningText->SetVisibility(ESlateVisibility::Collapsed);
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(WarningText))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Top);
		S->SetPadding(FMargin(0.0f, 56.0f, 0.0f, 0.0f));
	}

	// 5) 하단 힌트(선택만 — 뒤로 힌트 없음)
	HintText = MakeText(TEXT("[A / ENTER]  SELECT"), 14, TEXT("Regular"), 160, CXMenuStyle::TextDim);
	HintText->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (UOverlaySlot* S = RootOverlay->AddChildToOverlay(HintText))
	{
		S->SetHorizontalAlignment(HAlign_Right);
		S->SetVerticalAlignment(VAlign_Bottom);
		S->SetPadding(FMargin(0.0f, 0.0f, 64.0f, 48.0f));
	}

	// 6) 재시작/타이틀로 이동 검정 페이드
	FadeImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Fade"));
	FadeImage->SetColorAndOpacity(FLinearColor::Black);
	FadeImage->SetRenderOpacity(0.0f);
	FadeImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	AddFill(FadeImage);
}

void UCXGameOverWidget::SetFallen(bool bBrother, bool bSister)
{
	bFallenBrother = bBrother;
	bFallenSister = bSister;
	const bool bBoth = bBrother && bSister;

	// 오빠 — / 여동생 — / 오빠 & 여동생 —  (기획 4.3)
	FallenNameA->SetText(FText::FromString(bBrother || !bSister ? TEXT("BROTHER") : TEXT("SISTER")));
	FallenNameA->SetColorAndOpacity(FSlateColor(bBrother || !bSister ? CXMenuStyle::Brother : CXMenuStyle::Sister));
	FallenAmp->SetVisibility(bBoth ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	FallenNameB->SetVisibility(bBoth ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	const FLinearColor SuffixColor = bBoth ? CXMenuStyle::Text : (bSister ? CXMenuStyle::Sister : CXMenuStyle::Brother);
	FallenSuffix->SetColorAndOpacity(FSlateColor(SuffixColor));
}

// ---------------------------------------------------------------------------------------------
// 수명 / 연출

void UCXGameOverWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 시작 포커스 색 = 키보드 사용자(이후 마지막 입력 플레이어)
	if (const UCXGameInstance* GI = UCXGameInstance::Get(this))
	{
		FocusPlayer = GI->ResolvePlayerForInput(false, INPUTDEVICEID_NONE);
	}
	SetFocusItem(CXGameOver::Retry);

	Elapsed = 0.0f;
	bLeaving = false;
	Dim->SetRenderOpacity(0.0f);
	Noise->SetIntensity(0.0f);
	PanelBox->SetRenderOpacity(0.0f);
	MenuBox->SetRenderOpacity(0.0f);
	InfoText->SetRenderOpacity(0.0f);
	HintText->SetRenderOpacity(0.0f);
	// 진입 연출 동안 메뉴 입력 무시(소비만)
	LockInput(GetIntroTime());
}

float UCXGameOverWidget::GetIntroTime() const
{
	return GetPanelStartTime() + CXGameOver::MenuDelay;
}

void UCXGameOverWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	// 슬로우/월드 정지와 상관없이 위젯은 Slate 틱(실시간)으로 돈다
	Super::NativeTick(MyGeometry, InDeltaTime);
	Elapsed += InDeltaTime;

	// 0 ~ SlowMo: 두 칸 위로 노이즈가 차오름(딤은 살짝)
	// SlowMo ~ +Transition: 게임모드가 월드 정지 + 분할 끔 → 노이즈 피크 후 감소, 딤 70%까지
	const float SlowT = FMath::Clamp(Elapsed / FMath::Max(0.01f, SlowMoTime), 0.0f, 1.0f);
	const float TransT = FMath::Clamp((Elapsed - SlowMoTime) / FMath::Max(0.01f, TransitionTime), 0.0f, 1.0f);
	const float NoiseLevel = Elapsed < SlowMoTime ? SlowT : FMath::Lerp(1.0f, CXGameOver::NoiseBase, TransT);
	Noise->SetIntensity(NoiseLevel);
	Dim->SetRenderOpacity(FMath::Lerp(0.0f, 0.35f, SlowT) + 0.65f * TransT);

	// 패널: 0.15초 페이드 + 세로 0.96→1.0(CRT 켜짐 느낌)
	const float PanelT = Elapsed - GetPanelStartTime();
	const float PanelAlpha = FMath::Clamp(PanelT / CXMenuStyle::OpenTime, 0.0f, 1.0f);
	PanelBox->SetRenderOpacity(PanelAlpha);
	FWidgetTransform PanelTransform;
	PanelTransform.Scale = FVector2D(1.0f, FMath::Lerp(0.96f, 1.0f, PanelAlpha));
	PanelBox->SetRenderTransform(PanelTransform);

	// 제목 글리치 2~3회(가로 흔들림 + 깜박임) 후 고정
	float TitleOpacity = PanelT < 0.0f ? 0.0f : 1.0f;
	FWidgetTransform TitleTransform;
	if (PanelT >= 0.0f && PanelT < CXGameOver::TitleGlitchTime)
	{
		const bool bGlitch = (PanelT >= 0.06f && PanelT < 0.11f) || (PanelT >= 0.19f && PanelT < 0.23f) || (PanelT >= 0.31f && PanelT < 0.34f);
		if (bGlitch)
		{
			TitleOpacity = 0.25f + 0.4f * FMath::FRand();
			TitleTransform.Translation = FVector2D(FMath::FRandRange(-10.0f, 10.0f), FMath::FRandRange(-2.0f, 2.0f));
		}
	}
	TitleText->SetRenderOpacity(TitleOpacity);
	TitleText->SetRenderTransform(TitleTransform);

	// 메뉴: 패널 0.2초 뒤 페이드인
	const float MenuAlpha = FMath::Clamp((PanelT - CXGameOver::MenuDelay) / CXGameOver::MenuFadeTime, 0.0f, 1.0f);
	MenuBox->SetRenderOpacity(MenuAlpha);
	InfoText->SetRenderOpacity(MenuAlpha);
	HintText->SetRenderOpacity(MenuAlpha);

	// 게임패드 끊김 경고(메뉴는 그대로, 키보드/다른 패드로 선택 가능)
	if (const UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this))
	{
		const int32 Disconnected = Menus->GetDisconnectedPlayer();
		if (Disconnected != INDEX_NONE)
		{
			WarningText->SetText(FText::FromString(FString::Printf(TEXT("P%d CONTROLLER DISCONNECTED"), Disconnected + 1)));
			WarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
			WarningText->SetRenderOpacity(0.55f + 0.45f * FMath::Abs(FMath::Sin(Elapsed * 4.0f)));
		}
		else
		{
			WarningText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// 재시작/타이틀로: 0.4초 검정 페이드 후 맵 열기
	if (bLeaving)
	{
		LeaveElapsed += InDeltaTime;
		FadeImage->SetRenderOpacity(FMath::Clamp(LeaveElapsed / FMath::Max(0.01f, LeaveFadeTime), 0.0f, 1.0f));
		if (LeaveElapsed >= LeaveFadeTime && !LeaveTarget.IsEmpty())
		{
			const FString Map = MoveTemp(LeaveTarget);
			LeaveTarget.Reset();
			UGameplayStatics::OpenLevel(this, FName(*Map));
		}
	}
}

bool UCXGameOverWidget::CanAcceptInput() const
{
	// 진입 연출 / 페이드 중 입력 무시(소비만)
	return Super::CanAcceptInput() && Elapsed >= GetIntroTime() && !bLeaving;
}

// ---------------------------------------------------------------------------------------------
// 입력 / 동작

bool UCXGameOverWidget::OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex)
{
	switch (Input.Action)
	{
	case ECXMenuAction::Start:
	case ECXMenuAction::Left:
	case ECXMenuAction::Right:
	case ECXMenuAction::TabLeft:
	case ECXMenuAction::TabRight:
		// Start는 일시정지 메뉴를 열지 않고 아무 동작 없음 — 소비만, 포커스 색은 누른 사람 색으로
		if (!Input.bRepeat && FocusPlayer != PlayerIndex)
		{
			FocusPlayer = PlayerIndex;
			RefreshFocusVisuals();
		}
		return true;
	default:
		// 위/아래(순환)·선택은 베이스 처리
		return false;
	}
}

void UCXGameOverWidget::OnItemActivated(FName ItemId)
{
	// 둘 다 확인창 없음(기획 4.4 — 잃을 진행이 없다)
	if (ItemId == CXGameOver::Retry)
	{
		PlayUISound(ECXMenuSound::Confirm);
		BeginLeave(false);
	}
	else if (ItemId == CXGameOver::QuitToTitle)
	{
		PlayUISound(ECXMenuSound::Confirm);
		BeginLeave(true);
	}
}

void UCXGameOverWidget::BeginLeave(bool bToTitle)
{
	if (bLeaving)
	{
		return;
	}
	bLeaving = true;
	LeaveElapsed = 0.0f;

	if (bToTitle)
	{
		LeaveTarget = TitleMap;
		if (UCXGameInstance* GI = UCXGameInstance::Get(this))
		{
			// 타이틀로 돌아가는 경우는 진입 연출 축약(0.3초 페이드)
			GI->bTitleIntroPlayed = true;
		}
	}
	else
	{
		// 재시작: 세이브포인트 시스템이 없어 현재 맵을 처음부터(기본 PlayerStart — 2P 시작 태그 로직 그대로).
		// TODO(세이브포인트): UCXSaveGame의 맵·세이브포인트가 있으면 그 맵을 열고 세이브포인트에 스폰.
		const UWorld* World = GetWorld();
		LeaveTarget = World ? UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) : FString();
	}

	if (UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this))
	{
		// 페이드 중 다시 열리지 않게. bMenuBlocked·분할 끔은 맵 이동(PreLoadMap) 때 서브시스템이 푼다
		Menus->NotifyLeavingMap();
	}
	// 화면 페이드와 같은 시간으로 남은 음악 페이드아웃(진입 시 이미 컷했지만 안전하게)
	if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
	{
		Audio->StopMusic(LeaveFadeTime);
	}
}

FString UCXGameOverWidget::GetDebugState() const
{
	FString Fallen = FallenNameA ? FallenNameA->GetText().ToString() : FString();
	if (bFallenBrother && bFallenSister)
	{
		Fallen = TEXT("BROTHER & SISTER");
	}
	return FString::Printf(TEXT("%s gameover fallen=%s elapsed=%.2f leaving=%d target=%s accept=%d"), *Super::GetDebugState(), *Fallen,
		Elapsed, bLeaving ? 1 : 0, *LeaveTarget, CanAcceptInput() ? 1 : 0);
}
