// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXCutsceneWidget.h"
#include "CXMenuStyle.h"
#include "CXMenuSubsystem.h"
#include "CXSfx.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXCutscene, Log, All);

namespace CXCutscene
{
	constexpr float RefHeight = 1080.0f;
	/** 콜백 호출 뒤 맵 전환이 안 일어나도 이 시간 후 스스로 정리(멈춤 방지). */
	constexpr float HoldTimeout = 6.0f;
	/** 재생이 (전체 길이 + 이 값)초를 넘기면 강제 종료(감시 타이머). */
	constexpr float WatchdogMargin = 10.0f;
	constexpr float ReturnFadeTime = 0.4f;
	/** 번쩍임: 올라가는 시간 / 사라지는 시간. */
	constexpr float FlashRise = 0.06f;
	constexpr float FlashDecay = 0.6f;

	TWeakObjectPtr<UCXCutsceneWidget> Active;

	float Ramp(float T, float Start, float Duration)
	{
		return FMath::Clamp((T - Start) / FMath::Max(0.0001f, Duration), 0.0f, 1.0f);
	}

	FCXCutsceneSubtitle Sub(float Start, float End, const TCHAR* Ko, const TCHAR* En)
	{
		FCXCutsceneSubtitle S;
		S.Start = Start;
		S.End = End;
		S.Ko = Ko;
		S.En = En;
		return S;
	}
}

// ---------------------------------------------------------------------------------------------
// 컷 데이터 — 오프닝 컷신의 모든 내용(이미지 이름, 길이, 켄 번스, 자막)이 여기 한 곳에 있다.
// (Docs/Story/오프닝_컷신_초안.md 의 컷 1~5. 시간은 컷 시작 기준 초, 자막 첫 줄 ≈0.5초, 둘째 ≈55%)

const TArray<FCXCutsceneCut>& UCXCutsceneWidget::GetOpeningCuts()
{
	static TArray<FCXCutsceneCut> Cuts;
	if (Cuts.Num() > 0)
	{
		return Cuts;
	}

	// 컷 1. 도시 전경(8초) — 타워 꼭대기에서 아래(5구역)로 패닝(위→아래), 후반 약간 확대
	{
		FCXCutsceneCut C;
		C.TextureName = TEXT("cut1");
		C.Duration = 8.0f;
		C.ScaleStart = 1.22f;
		C.ScaleEnd = 1.38f;
		C.PanStart = FVector2f(0.0f, -1.0f);
		C.PanEnd = FVector2f(0.0f, 0.9f);
		C.Subtitles.Add(CXCutscene::Sub(0.8f, 7.4f, TEXT("이 도시에서는, 태어난 곳이 살아갈 곳을 정했다."), TEXT("In this city, where you were born decided where you belonged.")));
		Cuts.Add(MoveTemp(C));
	}

	// 컷 2. 5구역의 남매(8초) — 남매를 향해 천천히 줌 인, 마지막에 둘이 위를 올려다봄
	{
		FCXCutsceneCut C;
		C.TextureName = TEXT("cut2");
		C.Duration = 8.0f;
		C.ScaleStart = 1.08f;
		C.ScaleEnd = 1.32f;
		C.PanStart = FVector2f(0.0f, 0.1f);
		C.PanEnd = FVector2f(0.0f, 0.4f);
		C.Subtitles.Add(CXCutscene::Sub(0.5f, 3.8f, TEXT("남매는 하루를 버텼다."), TEXT("The siblings endured each day.")));
		C.Subtitles.Add(CXCutscene::Sub(4.2f, 7.5f, TEXT("하지만 평생을 이렇게 살 생각은 없었다."), TEXT("But they refused to live that way forever.")));
		Cuts.Add(MoveTemp(C));
	}

	// 컷 3. 이상한 고철(7초) — 물체 접사에서 천천히 뒤로 빠지며 남매가 같이 보는 구도로
	{
		FCXCutsceneCut C;
		C.TextureName = TEXT("cut3");
		C.Duration = 7.0f;
		C.ScaleStart = 1.45f;
		C.ScaleEnd = 1.08f;
		C.PanStart = FVector2f(-0.25f, 0.15f);
		C.PanEnd = FVector2f(0.0f, 0.0f);
		C.Subtitles.Add(CXCutscene::Sub(0.6f, 6.2f, TEXT("버려진 것들 사이에서, 또 하나의 운명을 만났다."), TEXT("Among the forgotten, they met another fate.")));
		Cuts.Add(MoveTemp(C));
	}

	// 컷 4. AI가 깨어남(9초) — 드론 화면이 켜지는 순간 점멸, 드론 클로즈업에서 남매의 놀란 얼굴로 천천히 이동
	{
		FCXCutsceneCut C;
		C.TextureName = TEXT("cut4");
		C.Duration = 9.0f;
		C.ScaleStart = 1.30f;
		C.ScaleEnd = 1.12f;
		C.PanStart = FVector2f(0.0f, -0.4f);
		C.PanEnd = FVector2f(0.0f, 0.5f);
		C.FlashTime = 1.2f;
		C.FlashColor = FLinearColor(0.75f, 0.92f, 1.0f, 0.55f);
		C.Subtitles.Add(CXCutscene::Sub(0.5f, 4.3f, TEXT("초기화 완료. 두 분을 커맨더로 등록했습니다."), TEXT("Initialization complete. Two Commanders registered.")));
		C.Subtitles.Add(CXCutscene::Sub(4.95f, 8.4f, TEXT("무엇을 도와드릴까요?"), TEXT("How may I assist you?")));
		Cuts.Add(MoveTemp(C));
	}

	// 컷 5. 추격 시작(8초) — 붉은 경보로 한 번 번쩍, 위(관리자 실루엣)에서 아래(남매)로, 마지막에 로고 페이드인
	{
		FCXCutsceneCut C;
		C.TextureName = TEXT("cut5");
		C.Duration = 8.0f;
		C.ScaleStart = 1.20f;
		C.ScaleEnd = 1.30f;
		C.PanStart = FVector2f(0.0f, -1.0f);
		C.PanEnd = FVector2f(0.0f, 0.8f);
		C.FlashTime = 0.4f;
		C.FlashColor = FLinearColor(1.0f, 0.05f, 0.1f, 0.45f);
		C.LogoTime = 5.4f;
		C.Subtitles.Add(CXCutscene::Sub(0.6f, 5.0f, TEXT("작은 선택 하나가, 거대한 질서에 균열을 냈다."), TEXT("One small choice had begun to fracture an entire system.")));
		Cuts.Add(MoveTemp(C));
	}

	return Cuts;
}

UTexture2D* UCXCutsceneWidget::LoadCutTexture(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return nullptr;
	}
	// 임포트한 에셋 이름 대소문자가 cut1 / Cut1 어느 쪽이어도 찾는다
	FString Alt = Name;
	Alt[0] = FChar::ToUpper(Alt[0]);
	const FString Candidates[] = { Name, Alt };
	for (const FString& N : Candidates)
	{
		const FString Path = FString::Printf(TEXT("/Game/Project_CX/Cutscene/%s.%s"), *N, *N);
		if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Tex;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// 그림 위젯

void UCXCutsceneArtWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 켄 번스로 화면보다 크게 그리는 부분이 뷰포트 밖으로 새지 않게
	SetClipping(EWidgetClipping::ClipToBounds);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCXCutsceneArtWidget::SetArt(UTexture2D* InTexture)
{
	Texture = InTexture;
	if (Texture)
	{
		ArtBrush = FSlateBrush();
		ArtBrush.SetResourceObject(Texture);
		ArtBrush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		ArtBrush.DrawAs = ESlateBrushDrawType::Image;
		ArtBrush.Tiling = ESlateBrushTileType::NoTile;
	}
}

int32 UCXCutsceneArtWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 L = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	const FGeometry& G = AllottedGeometry;
	const FVector2f Size = FVector2f(G.GetLocalSize());
	if (Size.X <= 1.0f || Size.Y <= 1.0f)
	{
		return L;
	}
	const float A = InWidgetStyle.GetColorAndOpacityTint().A;

	if (Texture && ArtBrush.GetResourceObject())
	{
		const FVector2f ImageSize(FMath::Max(1.0f, (float)ArtBrush.ImageSize.X), FMath::Max(1.0f, (float)ArtBrush.ImageSize.Y));
		// cover: 비율을 지키며 화면을 꽉 채우는 최소 배율 × 켄 번스 배율(≥1) → 그림이 화면보다 작아지는 일이 없다
		const float DrawScale = FMath::Max(Size.X / ImageSize.X, Size.Y / ImageSize.Y) * FMath::Max(1.0f, Scale);
		const FVector2f DrawSize = ImageSize * DrawScale;
		// 남는 여백 안에서만 패닝(-1..1) → 가장자리가 드러나지 않는다
		const FVector2f P(FMath::Clamp(Pan.X, -1.0f, 1.0f), FMath::Clamp(Pan.Y, -1.0f, 1.0f));
		const FVector2f Pos = (Size - DrawSize) * (FVector2f(0.5f, 0.5f) + P * 0.5f);
		++L;
		FSlateDrawElement::MakeBox(OutDrawElements, L, G.ToPaintGeometry(DrawSize, FSlateLayoutTransform(Pos)), &ArtBrush, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, A));
	}

	// 하단 그라디언트(자막 가독성)
	if (BottomShadeAlpha > 0.0f)
	{
		++L;
		TArray<FSlateGradientStop> Stops;
		const float H = Size.Y * 0.38f;
		Stops.Add(FSlateGradientStop(FVector2D(0.0f, 0.0f), FLinearColor(0, 0, 0, 0.0f)));
		Stops.Add(FSlateGradientStop(FVector2D(0.0f, H), FLinearColor(0, 0, 0, BottomShadeAlpha * A)));
		FSlateDrawElement::MakeGradient(OutDrawElements, L, G.ToPaintGeometry(FVector2f(Size.X, H), FSlateLayoutTransform(FVector2f(0.0f, Size.Y - H))), MoveTemp(Stops), Orient_Horizontal);
	}
	if (DimAlpha > 0.001f)
	{
		++L;
		CXMenuStyle::DrawRect(OutDrawElements, L, G, FVector2f::ZeroVector, Size, FLinearColor(0.0f, 0.0f, 0.0f, DimAlpha * A));
	}
	if (FlashColor.A > 0.001f)
	{
		++L;
		CXMenuStyle::DrawRect(OutDrawElements, L, G, FVector2f::ZeroVector, Size, FLinearColor(FlashColor.R, FlashColor.G, FlashColor.B, FlashColor.A * A));
	}
	return L;
}

// ---------------------------------------------------------------------------------------------
// 재생 시작

UCXCutsceneWidget* UCXCutsceneWidget::Play(const UObject* WorldContext, TFunction<void()> OnFinished, int32 StartCut)
{
	if (CXCutscene::Active.IsValid())
	{
		UE_LOG(LogCXCutscene, Warning, TEXT("Cutscene is already playing."));
		return nullptr;
	}
	UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(WorldContext);
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	APlayerController* PC = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!Menus || !PC)
	{
		UE_LOG(LogCXCutscene, Warning, TEXT("Cutscene skipped: no menu subsystem or player controller."));
		return nullptr;
	}

	// 텍스처가 하나도 없으면 재생하지 않는다(호출자가 바로 레벨을 연다)
	bool bAnyTexture = false;
	for (const FCXCutsceneCut& Cut : GetOpeningCuts())
	{
		if (LoadCutTexture(Cut.TextureName))
		{
			bAnyTexture = true;
			break;
		}
	}
	if (!bAnyTexture)
	{
		UE_LOG(LogCXCutscene, Warning, TEXT("Cutscene skipped: no cut textures found under /Game/Project_CX/Cutscene/ (cut1..cut5)."));
		return nullptr;
	}

	UCXCutsceneWidget* Widget = Cast<UCXCutsceneWidget>(Menus->PushMenu(UCXCutsceneWidget::StaticClass(), PC, 400));
	if (!Widget)
	{
		UE_LOG(LogCXCutscene, Warning, TEXT("Cutscene skipped: could not create widget."));
		return nullptr;
	}
	Widget->Init(MoveTemp(OnFinished), StartCut);
	return Widget;
}

void UCXCutsceneWidget::Init(TFunction<void()> InOnFinished, int32 StartCut)
{
	OnFinishedCallback = MoveTemp(InOnFinished);
	const TArray<FCXCutsceneCut>& Cuts = GetOpeningCuts();

	CutTextures.Reset();
	TotalDuration = 0.0f;
	StartOffset = 0.0f;
	const int32 First = FMath::Clamp(StartCut, 0, Cuts.Num() - 1);
	for (int32 i = 0; i < Cuts.Num(); ++i)
	{
		UTexture2D* Tex = LoadCutTexture(Cuts[i].TextureName);
		if (!Tex)
		{
			UE_LOG(LogCXCutscene, Warning, TEXT("Cutscene: texture '%s' missing - showing black + subtitles for this cut."), *Cuts[i].TextureName);
		}
		CutTextures.Add(Tex);
		if (i < First)
		{
			StartOffset += Cuts[i].Duration;
		}
		TotalDuration += Cuts[i].Duration;
	}
	Elapsed = StartOffset;
	LogoTexture = LoadObject<UTexture2D>(nullptr, *LogoPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (LogoTexture && LogoImage)
	{
		FSlateBrush LogoBrush;
		LogoBrush.SetResourceObject(LogoTexture);
		LogoBrush.ImageSize = FVector2D(LogoTexture->GetSizeX(), LogoTexture->GetSizeY());
		LogoBrush.DrawAs = ESlateBrushDrawType::Image;
		LogoImage->SetBrush(LogoBrush);
	}

	LayoutSize = FVector2D::ZeroVector;	// 첫 Tick에서 실제 화면 크기로 로고·자막 배치를 다시 계산
	State = EState::Playing;
	StateTime = 0.0f;
	OpenElapsed = 0.0f;
	IntroTimeLeft = OpenFadeTime + IntroBlackHold;
	CurrentCut = INDEX_NONE;
	CurrentSubtitle = INDEX_NONE;
	StartWallTime = FPlatformTime::Seconds();
	bInitialized = true;
	// NEW GAME을 확정한 키가 바로 건너뛰기가 되지 않게
	LockInput(InputGraceTime);
	CXCutscene::Active = this;
}

// ---------------------------------------------------------------------------------------------
// 위젯 트리

void UCXCutsceneWidget::BuildMenu()
{
	RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = RootOverlay;

	auto AddSlot = [this](UWidget* W, EHorizontalAlignment H, EVerticalAlignment V) -> UOverlaySlot*
	{
		UOverlaySlot* S = RootOverlay->AddChildToOverlay(W);
		if (S)
		{
			S->SetHorizontalAlignment(H);
			S->SetVerticalAlignment(V);
		}
		return S;
	};

	// 1) 검정 배경(Visible → 아래 시작화면으로 마우스 클릭이 새지 않는다). 시작 시 서서히 덮는다.
	BlackBackdrop = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BlackBackdrop"));
	BlackBackdrop->SetColorAndOpacity(FLinearColor::Black);
	BlackBackdrop->SetRenderOpacity(0.0f);
	BlackBackdrop->SetCursor(EMouseCursor::None);
	AddSlot(BlackBackdrop, HAlign_Fill, VAlign_Fill);

	// 2) 일러스트(켄 번스 + 하단 그라디언트)
	Art = WidgetTree->ConstructWidget<UCXCutsceneArtWidget>(UCXCutsceneArtWidget::StaticClass(), TEXT("Art"));
	Art->SetRenderOpacity(0.0f);
	AddSlot(Art, HAlign_Fill, VAlign_Fill);

	// 3) 로고(마지막 컷)
	LogoBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("LogoBox"));
	LogoImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Logo"));
	LogoBox->AddChild(LogoImage);
	LogoBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	LogoBox->SetRenderOpacity(0.0f);
	AddSlot(LogoBox, HAlign_Center, VAlign_Center);

	// 4) 자막: 화면 하단 중앙, 한글 → 영어
	SubtitleBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SubtitleBox"));
	UVerticalBox* SubStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SubtitleStack"));
	SubtitleBox->AddChild(SubStack);
	SubtitleBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	SubtitleBox->SetRenderOpacity(0.0f);

	auto MakeLine = [this, SubStack](const TCHAR* Name) -> UTextBlock*
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		T->SetAutoWrapText(true);
		T->SetJustification(ETextJustify::Center);
		T->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UVerticalBoxSlot* S = SubStack->AddChildToVerticalBox(T))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
		}
		return T;
	};
	KoText = MakeLine(TEXT("KoText"));
	KoText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	EnText = MakeLine(TEXT("EnText"));
	EnText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, EnOpacity)));
	AddSlot(SubtitleBox, HAlign_Center, VAlign_Bottom);

	// 5) 건너뛰기 안내(오른쪽 아래)
	HintText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Hint"));
	HintText->SetText(FText::FromString(TEXT("[SPACE / A]  건너뛰기  SKIP")));
	HintText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.6f)));
	HintText->SetShadowOffset(FVector2D(1.0f, 1.0f));
	HintText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	HintText->SetVisibility(ESlateVisibility::HitTestInvisible);
	HintText->SetRenderOpacity(0.0f);
	AddSlot(HintText, HAlign_Right, VAlign_Bottom);

	// 6) 검정 페이드(건너뛰기/종료)
	FadeImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Fade"));
	FadeImage->SetColorAndOpacity(FLinearColor::Black);
	FadeImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	FadeImage->SetRenderOpacity(0.0f);
	AddSlot(FadeImage, HAlign_Fill, VAlign_Fill);

	UpdateLayout(FVector2D(1920.0, 1080.0));
}

void UCXCutsceneWidget::UpdateLayout(const FVector2D& ScreenSize)
{
	if (ScreenSize.X <= 1.0 || ScreenSize.Y <= 1.0 || ScreenSize.Equals(LayoutSize, 0.5))
	{
		return;
	}
	LayoutSize = ScreenSize;
	const float Scale = (float)ScreenSize.Y / CXCutscene::RefHeight;

	// 자막: 폭 70%, 글자 크기는 화면 높이에 비례
	SubtitleBox->SetWidthOverride((float)ScreenSize.X * SubtitleMaxWidthFraction);
	if (UOverlaySlot* S = Cast<UOverlaySlot>(SubtitleBox->Slot))
	{
		S->SetPadding(FMargin(0.0f, 0.0f, 0.0f, (float)ScreenSize.Y * SubtitleBottomFraction));
	}
	const int32 Outline = FMath::Max(1, FMath::RoundToInt(OutlineSize * Scale));
	auto Style = [&](UTextBlock* T, FSlateFontInfo Font, float ShadowPx)
	{
		Font.OutlineSettings.OutlineSize = Outline;
		Font.OutlineSettings.OutlineColor = FLinearColor::Black;
		T->SetFont(Font);
		T->SetShadowOffset(FVector2D(ShadowPx, ShadowPx));
		T->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
	};
	Style(KoText, CXMenuStyle::Font(FMath::Max(8, FMath::RoundToInt(KoFontSize * Scale)), TEXT("Bold")), 2.0f * Scale);
	Style(EnText, CXMenuStyle::Font(FMath::Max(6, FMath::RoundToInt(KoFontSize * EnScale * Scale)), TEXT("Medium")), 1.5f * Scale);
	if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(EnText->Slot))
	{
		S->SetPadding(FMargin(0.0f, SubtitleLineGap * Scale, 0.0f, 0.0f));
	}

	HintText->SetFont(CXMenuStyle::Font(FMath::Max(8, FMath::RoundToInt(16.0f * Scale)), TEXT("Medium"), 60));
	if (UOverlaySlot* S = Cast<UOverlaySlot>(HintText->Slot))
	{
		S->SetPadding(FMargin(0.0f, 0.0f, 40.0f * Scale, 28.0f * Scale));
	}

	// 로고: 화면 폭 비율, 높이 40% 이하, 비율 유지
	if (LogoTexture)
	{
		const float Aspect = (float)LogoTexture->GetSizeX() / FMath::Max(1.0f, (float)LogoTexture->GetSizeY());
		float W = (float)ScreenSize.X * LogoWidthFraction;
		W = FMath::Min(W, (float)ScreenSize.Y * 0.4f * Aspect);
		LogoBox->SetWidthOverride(W);
		LogoBox->SetHeightOverride(W / Aspect);
	}
}

// ---------------------------------------------------------------------------------------------
// 수명

void UCXCutsceneWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetCursor(EMouseCursor::None);
}

void UCXCutsceneWidget::NativeDestruct()
{
	if (CXCutscene::Active.Get() == this)
	{
		CXCutscene::Active.Reset();
	}
	Super::NativeDestruct();
}

bool UCXCutsceneWidget::CanAcceptInput() const
{
	return Super::CanAcceptInput() && State == EState::Playing && bInitialized;
}

FString UCXCutsceneWidget::GetDebugState() const
{
	return FString::Printf(TEXT("%s cutscene t=%.1f/%.1f cut=%d state=%d"), *Super::GetDebugState(), Elapsed, TotalDuration, CurrentCut, (int32)State);
}

// ---------------------------------------------------------------------------------------------
// 입력: 메뉴 입력(Enter/A=Accept, Esc/B=Back, Start)은 프리프로세서가, Space 등 매핑 없는 키는 Slate 포커스가 이 위젯으로 보낸다.
// 어느 쪽이든 아래 시작화면 메뉴에는 전달되지 않는다.

bool UCXCutsceneWidget::OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex)
{
	if (!Input.bRepeat && (Input.Action == ECXMenuAction::Accept || Input.Action == ECXMenuAction::Back || Input.Action == ECXMenuAction::Start))
	{
		RequestSkip();
	}
	return true;
}

FReply UCXCutsceneWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!InKeyEvent.IsRepeat() && (InKeyEvent.GetKey() == EKeys::SpaceBar || InKeyEvent.GetKey() == EKeys::Enter) && CanAcceptInput())
	{
		RequestSkip();
	}
	// 다른 키도 소비(아래 메뉴/게임으로 새지 않게)
	return FReply::Handled();
}

FReply UCXCutsceneWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Handled();
}

void UCXCutsceneWidget::RequestSkip()
{
	if (State != EState::Playing || !bInitialized)
	{
		return;
	}
	State = EState::Skipping;
	StateTime = 0.0f;
	PlayUISound(ECXMenuSound::Confirm);
}

void UCXCutsceneWidget::FinishSequence()
{
	// 모든 연출을 걷고 완전한 검정으로
	Art->SetRenderOpacity(0.0f);
	LogoBox->SetRenderOpacity(0.0f);
	SubtitleBox->SetRenderOpacity(0.0f);
	HintText->SetRenderOpacity(0.0f);
	BlackBackdrop->SetRenderOpacity(1.0f);
	FadeImage->SetRenderOpacity(1.0f);
	StateTime = 0.0f;

	if (OnFinishedCallback)
	{
		// 새 게임: 원래의 "첫 맵 열기" 흐름으로 넘긴다. 맵이 바뀔 때까지 검정을 유지(PreLoadMap이 위젯을 내린다).
		State = EState::Holding;
		if (!bCallbackFired)
		{
			bCallbackFired = true;
			TFunction<void()> Callback = MoveTemp(OnFinishedCallback);
			OnFinishedCallback = nullptr;
			Callback();
		}
	}
	else
	{
		// 콘솔 재생: 검정에서 원래 화면으로 돌아간다
		State = EState::Returning;
	}
}

void UCXCutsceneWidget::ClosePlayback()
{
	if (UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this))
	{
		Menus->PopMenu(this);
	}
	else
	{
		RemoveFromParent();
	}
}

// ---------------------------------------------------------------------------------------------
// 재생

void UCXCutsceneWidget::SwitchCut(int32 NewIndex)
{
	CurrentCut = NewIndex;
	CurrentSubtitle = INDEX_NONE;
	Art->SetArt(CutTextures.IsValidIndex(NewIndex) ? CutTextures[NewIndex].Get() : nullptr);
	KoText->SetText(FText::GetEmpty());
	EnText->SetText(FText::GetEmpty());
	SubtitleBox->SetRenderOpacity(0.0f);
}

void UCXCutsceneWidget::UpdateSubtitle(const FCXCutsceneCut& Cut, float CutTime)
{
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < Cut.Subtitles.Num(); ++i)
	{
		if (CutTime >= Cut.Subtitles[i].Start && CutTime < Cut.Subtitles[i].End)
		{
			Index = i;
			break;
		}
	}
	if (Index != CurrentSubtitle)
	{
		CurrentSubtitle = Index;
		if (Index != INDEX_NONE)
		{
			KoText->SetText(FText::FromString(Cut.Subtitles[Index].Ko));
			EnText->SetText(FText::FromString(Cut.Subtitles[Index].En));
		}
	}
	float Alpha = 0.0f;
	if (Index != INDEX_NONE)
	{
		const FCXCutsceneSubtitle& S = Cut.Subtitles[Index];
		Alpha = FMath::Min(CXCutscene::Ramp(CutTime, S.Start, SubtitleFadeTime), 1.0f - CXCutscene::Ramp(CutTime, S.End - SubtitleFadeTime, SubtitleFadeTime));
	}
	SubtitleBox->SetRenderOpacity(Alpha);
}

void UCXCutsceneWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!bInitialized)
	{
		return;
	}
	UpdateLayout(FVector2D(MyGeometry.GetLocalSize()));
	StateTime += InDeltaTime;

	switch (State)
	{
	case EState::Playing:
	{
		OpenElapsed += InDeltaTime;
		BlackBackdrop->SetRenderOpacity(CXCutscene::Ramp(OpenElapsed, 0.0f, OpenFadeTime));
		HintText->SetRenderOpacity(CXCutscene::Ramp(OpenElapsed, InputGraceTime, 0.5f));

		// 시작 연출: 타이틀이 검정으로 페이드된 뒤 잠깐 멈췄다가 첫 컷이 나온다(그동안 컷 시간은 흐르지 않음)
		if (IntroTimeLeft > 0.0f)
		{
			IntroTimeLeft -= InDeltaTime;
			Art->SetRenderOpacity(0.0f);
			SubtitleBox->SetRenderOpacity(0.0f);
			LogoBox->SetRenderOpacity(0.0f);
			break;
		}
		Elapsed += InDeltaTime;

		// 감시 타이머: 어떤 이유로든 너무 오래 끝나지 않으면 강제 종료(멈춤 방지)
		if (FPlatformTime::Seconds() - StartWallTime > (double)(TotalDuration - StartOffset + CXCutscene::WatchdogMargin))
		{
			UE_LOG(LogCXCutscene, Warning, TEXT("Cutscene watchdog fired - forcing finish."));
			FinishSequence();
			return;
		}
		if (Elapsed >= TotalDuration)
		{
			FinishSequence();
			return;
		}

		// 현재 컷 찾기
		const TArray<FCXCutsceneCut>& Cuts = GetOpeningCuts();
		float CutStart = 0.0f;
		int32 Index = 0;
		for (; Index < Cuts.Num() - 1 && Elapsed >= CutStart + Cuts[Index].Duration; ++Index)
		{
			CutStart += Cuts[Index].Duration;
		}
		const FCXCutsceneCut& Cut = Cuts[Index];
		const float CutTime = Elapsed - CutStart;
		if (Index != CurrentCut)
		{
			SwitchCut(Index);
		}

		const float Progress = FMath::Clamp(CutTime / FMath::Max(0.01f, Cut.Duration), 0.0f, 1.0f);
		const float CutAlpha = FMath::Min(CXCutscene::Ramp(CutTime, 0.0f, CutFadeTime),
			1.0f - CXCutscene::Ramp(CutTime, Cut.Duration - CutFadeTime, CutFadeTime));
		Art->SetKenBurns(FMath::Lerp(Cut.ScaleStart, Cut.ScaleEnd, Progress), FMath::Lerp(Cut.PanStart, Cut.PanEnd, Progress));
		Art->SetRenderOpacity(CutAlpha);
		Art->SetBottomShade(BottomShadeAlpha);

		// 번쩍임
		float Flash = 0.0f;
		if (Cut.FlashTime >= 0.0f && CutTime >= Cut.FlashTime)
		{
			const float FT = CutTime - Cut.FlashTime;
			Flash = FT < CXCutscene::FlashRise ? FT / CXCutscene::FlashRise : 1.0f - CXCutscene::Ramp(FT, CXCutscene::FlashRise, CXCutscene::FlashDecay);
		}
		Art->SetFlash(FLinearColor(Cut.FlashColor.R, Cut.FlashColor.G, Cut.FlashColor.B, Cut.FlashColor.A * Flash));

		// 로고(마지막 컷): 서서히 떠오르고 그림은 살짝 어두워진다. 컷 끝 페이드로 같이 검정으로.
		float LogoAlpha = 0.0f;
		if (Cut.LogoTime >= 0.0f)
		{
			LogoAlpha = FMath::InterpEaseOut(0.0f, 1.0f, CXCutscene::Ramp(CutTime, Cut.LogoTime, LogoFadeTime), 2.0f);
		}
		LogoBox->SetRenderOpacity(LogoAlpha * CutAlpha);
		Art->SetDim(LogoDimAlpha * LogoAlpha);

		UpdateSubtitle(Cut, CutTime);
		break;
	}

	case EState::Skipping:
		FadeImage->SetRenderOpacity(CXCutscene::Ramp(StateTime, 0.0f, SkipFadeTime));
		BlackBackdrop->SetRenderOpacity(1.0f);
		if (StateTime >= SkipFadeTime)
		{
			FinishSequence();
		}
		break;

	case EState::Holding:
		// 콜백(맵 열기)이 이어졌으면 곧 PreLoadMap이 이 위젯을 내린다. 안 내려가면 스스로 정리.
		if (StateTime >= CXCutscene::HoldTimeout)
		{
			UE_LOG(LogCXCutscene, Warning, TEXT("Cutscene hold timeout - closing overlay."));
			ClosePlayback();
		}
		break;

	case EState::Returning:
		SetRenderOpacity(1.0f - CXCutscene::Ramp(StateTime, 0.0f, CXCutscene::ReturnFadeTime));
		if (StateTime >= CXCutscene::ReturnFadeTime)
		{
			ClosePlayback();
		}
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// 콘솔 명령

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GCXCutsceneCommand(
	TEXT("CXCutscene"),
	TEXT("Play the opening cutscene (test). Usage: CXCutscene [startCut 1-5]. Returns to the current screen when it ends; does not load a map."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const int32 StartCut = Args.Num() > 0 ? FMath::Max(0, FCString::Atoi(*Args[0]) - 1) : 0;
		if (!UCXCutsceneWidget::Play(World, nullptr, StartCut))
		{
			UE_LOG(LogCXCutscene, Warning, TEXT("CXCutscene: could not start (already playing, no textures imported yet, or no player controller)."));
		}
	}));
#endif
