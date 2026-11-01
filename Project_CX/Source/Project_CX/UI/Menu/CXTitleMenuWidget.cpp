// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXTitleMenuWidget.h"
#include "CXMenuItemWidget.h"
#include "CXPlayerSlotWidget.h"
#include "CXTitleBackdropWidget.h"
#include "CXGameInstance.h"
#include "CXSaveGame.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Sound/SoundBase.h"

namespace CXTitle
{
	const FName Continue = TEXT("Continue");
	const FName NewGame = TEXT("NewGame");
	const FName Settings = TEXT("Settings");
	const FName Quit = TEXT("Quit");

	// 진입 연출 타임라인(초)
	constexpr float BackdropFade = 1.0f;
	constexpr float TitleStart = 0.9f;
	constexpr float TitleFlicker = 0.6f;
	constexpr float SubtitleStart = 1.4f;
	constexpr float MenuStart = 1.5f;
	constexpr float MenuSlide = 0.2f;
	constexpr float MenuStagger = 0.04f;
	constexpr float FooterStart = 1.6f;
	constexpr float IntroEnd = 2.0f;
	constexpr float ShortIntro = 0.3f;

	constexpr float DevicePromptTime = 2.6f;
	constexpr float GamepadPollInterval = 0.5f;

	/** 네온 점등 깜박임(0~TitleFlicker초 → 밝기). 2~3회 짧게 꺼졌다 켜짐. */
	float FlickerAlpha(float T)
	{
		if (T <= 0.0f) return 0.0f;
		if (T < 0.06f) return 1.0f;
		if (T < 0.14f) return 0.05f;
		if (T < 0.20f) return 0.8f;
		if (T < 0.30f) return 0.1f;
		if (T < 0.36f) return 1.0f;
		if (T < 0.42f) return 0.3f;
		return FMath::Min(1.0f, 0.6f + (T - 0.42f) / 0.18f * 0.4f);
	}

	float Ramp(float T, float Start, float Duration)
	{
		return FMath::Clamp((T - Start) / Duration, 0.0f, 1.0f);
	}
}

// ---------------------------------------------------------------------------------------------
// 위젯 트리

void UCXTitleMenuWidget::BuildMenu()
{
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	auto AddFill = [Root](UWidget* W)
	{
		if (UOverlaySlot* S = Root->AddChildToOverlay(W))
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

	// 1) 배경
	Backdrop = WidgetTree->ConstructWidget<UCXTitleBackdropWidget>(UCXTitleBackdropWidget::StaticClass(), TEXT("Backdrop"));
	AddFill(Backdrop);

	// 2) 내용(1920x1080 기준 배치, DPI 스케일은 엔진이 맞춘다)
	Content = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Content"));
	AddFill(Content);

	// 게임명: 좌상단 1/3 지점
	UVerticalBox* TitleBlock = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TitleBlock"));
	if (UCanvasPanelSlot* S = Content->AddChildToCanvas(TitleBlock))
	{
		S->SetAutoSize(true);
		S->SetPosition(FVector2D(150.0f, 200.0f));
	}
	TitleText = MakeText(TEXT("PROJECT_CX"), 96, TEXT("Bold"), 180, FLinearColor(0.88f, 1.0f, 1.0f, 1.0f));
	TitleText->SetShadowOffset(FVector2D(2.5f, 2.5f));
	TitleText->SetShadowColorAndOpacity(CXMenuStyle::Brother.CopyWithNewOpacity(0.55f));
	TitleBlock->AddChildToVerticalBox(TitleText);

	UHorizontalBox* SubRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* S = TitleBlock->AddChildToVerticalBox(SubRow))
	{
		S->SetPadding(FMargin(6.0f, 4.0f, 0.0f, 0.0f));
	}
	TitleUnderline = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Underline"));
	{
		FSlateBrush LineBrush;
		LineBrush.ImageSize = FVector2D(300.0f, 2.0f);
		TitleUnderline->SetBrush(LineBrush);
	}
	TitleUnderline->SetColorAndOpacity(CXMenuStyle::Brother);
	if (UHorizontalBoxSlot* S = SubRow->AddChildToHorizontalBox(TitleUnderline))
	{
		S->SetVerticalAlignment(VAlign_Center);
	}
	SubtitleText = MakeText(TEXT("DISTRICT 5"), 22, TEXT("Bold"), 600, CXMenuStyle::Brother.CopyWithNewOpacity(0.9f));
	if (UHorizontalBoxSlot* S = SubRow->AddChildToHorizontalBox(SubtitleText))
	{
		S->SetVerticalAlignment(VAlign_Center);
		S->SetPadding(FMargin(20.0f, 0.0f, 0.0f, 0.0f));
	}

	// 메뉴: 좌측 세로 목록(오른쪽은 비워 풍경이 보이게)
	UVerticalBox* MenuList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuList"));
	if (UCanvasPanelSlot* S = Content->AddChildToCanvas(MenuList))
	{
		S->SetPosition(FVector2D(150.0f, 440.0f));
		S->SetSize(FVector2D(820.0f, 320.0f));
	}
	AddMenuItem(MenuList, CXTitle::Continue, FText::FromString(TEXT("CONTINUE")));
	AddMenuItem(MenuList, CXTitle::NewGame, FText::FromString(TEXT("NEW GAME")));
	AddMenuItem(MenuList, CXTitle::Settings, FText::FromString(TEXT("SETTINGS")));
	AddMenuItem(MenuList, CXTitle::Quit, FText::FromString(TEXT("QUIT GAME")));

	SaveWarningText = MakeText(TEXT("SAVE DATA COULD NOT BE LOADED"), 16, TEXT("Bold"), 120, CXMenuStyle::Warning);
	SaveWarningText->SetVisibility(ESlateVisibility::Collapsed);
	// 메뉴 목록 바로 아래(항목 수가 바뀌어도 따라감)
	if (UVerticalBoxSlot* S = MenuList->AddChildToVerticalBox(SaveWarningText))
	{
		S->SetPadding(FMargin(21.0f, 18.0f, 0.0f, 0.0f));
	}

	// 하단 중앙 플레이어 슬롯 2개
	UHorizontalBox* SlotRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SlotRow"));
	if (UCanvasPanelSlot* S = Content->AddChildToCanvas(SlotRow))
	{
		S->SetAnchors(FAnchors(0.5f, 1.0f));
		S->SetAlignment(FVector2D(0.5f, 1.0f));
		S->SetAutoSize(true);
		S->SetPosition(FVector2D(0.0f, -110.0f));
	}
	const TCHAR* SlotNames[UCXGameInstance::NumPlayerSlots] = { TEXT("P1  BROTHER"), TEXT("P2  SISTER") };
	for (int32 i = 0; i < UCXGameInstance::NumPlayerSlots; ++i)
	{
		UCXPlayerSlotWidget* PlayerSlot = WidgetTree->ConstructWidget<UCXPlayerSlotWidget>(UCXPlayerSlotWidget::StaticClass());
		PlayerSlot->Setup(i, FText::FromString(SlotNames[i]));
		if (UHorizontalBoxSlot* S = SlotRow->AddChildToHorizontalBox(PlayerSlot))
		{
			S->SetPadding(FMargin(i == 0 ? 0.0f : 40.0f, 0.0f, 0.0f, 0.0f));
		}
		PlayerSlots.Add(PlayerSlot);
	}

	DevicePromptText = MakeText(TEXT(""), 20, TEXT("Bold"), 200, CXMenuStyle::Warning);
	DevicePromptText->SetJustification(ETextJustify::Center);
	DevicePromptText->SetShadowOffset(FVector2D(2.0f, 2.0f));
	DevicePromptText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	DevicePromptText->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* S = Content->AddChildToCanvas(DevicePromptText))
	{
		S->SetAnchors(FAnchors(0.5f, 1.0f));
		S->SetAlignment(FVector2D(0.5f, 1.0f));
		S->SetAutoSize(true);
		S->SetPosition(FVector2D(0.0f, -232.0f));
	}

	HintText = MakeText(FString::Printf(TEXT("[A / ENTER]  SELECT        [B / ESC]  BACK            %s"), *VersionLabel), 14, TEXT("Regular"), 160, CXMenuStyle::TextDim);
	if (UCanvasPanelSlot* S = Content->AddChildToCanvas(HintText))
	{
		S->SetAnchors(FAnchors(1.0f, 1.0f));
		S->SetAlignment(FVector2D(1.0f, 1.0f));
		S->SetAutoSize(true);
		S->SetPosition(FVector2D(-60.0f, -40.0f));
	}

	// 3) 확인창 레이어
	ModalLayer = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ModalLayer"));
	AddFill(ModalLayer);
	SetModalHost(ModalLayer);

	// 4) 검정 페이드(진입/게임 시작)
	FadeImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Fade"));
	FadeImage->SetColorAndOpacity(FLinearColor::Black);
	FadeImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	AddFill(FadeImage);

	Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

// ---------------------------------------------------------------------------------------------
// 수명

void UCXTitleMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UCXGameInstance* GI = UCXGameInstance::Get(this);
	if (GI)
	{
		// 시작화면에 올 때마다 게임패드 확인은 다시 받는다(키보드 슬롯은 항상 READY).
		GI->ClearAllSlotGamepads();
		GI->PendingContinueSave = nullptr;
	}

	FocusPlayer = GI ? GI->ResolvePlayerForInput(false, INPUTDEVICEID_NONE) : 0;
	bGamepadAttached = FSlateApplication::IsInitialized() && FSlateApplication::Get().IsGamepadAttached();
	RefreshSaveState();
	RefreshSlots();

	DeviceConnectionHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddUObject(this, &UCXTitleMenuWidget::HandleDeviceConnectionChange);

	// 진입 연출: 첫 실행만 풀 연출, 타이틀로 돌아온 경우 0.3초 페이드
	bShortIntro = GI && GI->bTitleIntroPlayed;
	if (GI)
	{
		GI->bTitleIntroPlayed = true;
	}
	IntroTime = 0.0f;
	bBuzzPlayed = false;
	UpdateIntro(0.0f);
}

void UCXTitleMenuWidget::NativeDestruct()
{
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(DeviceConnectionHandle);
	Super::NativeDestruct();
}

void UCXTitleMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (IntroTime >= 0.0f)
	{
		UpdateIntro(InDeltaTime);
	}

	// 게임패드 연결 상태(NOT CONNECTED 표시, 패드가 모두 빠지면 READY 해제)
	GamepadPollTimer -= InDeltaTime;
	if (GamepadPollTimer <= 0.0f && FSlateApplication::IsInitialized())
	{
		GamepadPollTimer = CXTitle::GamepadPollInterval;
		const bool bAttached = FSlateApplication::Get().IsGamepadAttached();
		if (bAttached != bGamepadAttached)
		{
			bGamepadAttached = bAttached;
			if (!bAttached)
			{
				if (UCXGameInstance* GI = UCXGameInstance::Get(this))
				{
					GI->ClearAllSlotGamepads();
				}
			}
			RefreshSlots();
		}
	}

	if (DevicePromptRemaining > 0.0f)
	{
		DevicePromptRemaining = FMath::Max(0.0f, DevicePromptRemaining - InDeltaTime);
		const float Pulse = 0.55f + 0.45f * FMath::Cos((CXTitle::DevicePromptTime - DevicePromptRemaining) * UE_TWO_PI * 1.5f);
		DevicePromptText->SetRenderOpacity(Pulse * FMath::Min(1.0f, DevicePromptRemaining / 0.3f));
		if (DevicePromptRemaining <= 0.0f)
		{
			DevicePromptText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	if (bStarting)
	{
		StartFadeElapsed += InDeltaTime;
		FadeImage->SetRenderOpacity(FMath::Clamp(StartFadeElapsed / StartFadeTime, 0.0f, 1.0f));
		if (StartFadeElapsed >= StartFadeTime && !PendingMap.IsEmpty())
		{
			const FString Map = MoveTemp(PendingMap);
			PendingMap.Reset();
			UGameplayStatics::OpenLevel(this, FName(*Map));
		}
	}
}

// ---------------------------------------------------------------------------------------------
// 진입 연출

void UCXTitleMenuWidget::UpdateIntro(float DeltaTime)
{
	using namespace CXTitle;
	IntroTime += DeltaTime;
	const float T = IntroTime;

	if (bShortIntro)
	{
		FadeImage->SetRenderOpacity(1.0f - Ramp(T, 0.0f, ShortIntro));
		if (T >= ShortIntro)
		{
			FinishIntro();
		}
		return;
	}

	FadeImage->SetRenderOpacity(1.0f - Ramp(T, 0.0f, BackdropFade));

	const float Flicker = FlickerAlpha(T - TitleStart);
	TitleText->SetRenderOpacity(Flicker);
	if (!bBuzzPlayed && T >= TitleStart)
	{
		bBuzzPlayed = true;
		if (SoundNeonBuzz)
		{
			UGameplayStatics::PlaySound2D(this, SoundNeonBuzz, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
		}
	}

	const float Sub = Ramp(T, SubtitleStart, 0.3f);
	TitleUnderline->SetRenderOpacity(Sub);
	TitleUnderline->SetRenderScale(FVector2D(FMath::Max(0.01f, Sub), 1.0f));
	TitleUnderline->SetRenderTransformPivot(FVector2D(0.0f, 0.5f));
	SubtitleText->SetRenderOpacity(Sub);

	// 메뉴 슬라이드인(항목별 0.04초 간격)
	int32 Shown = 0;
	for (UCXMenuItemWidget* Item : Items)
	{
		if (!Item || Item->GetVisibility() == ESlateVisibility::Collapsed)
		{
			continue;
		}
		const float A = FMath::InterpEaseOut(0.0f, 1.0f, Ramp(T, MenuStart + MenuStagger * Shown, MenuSlide), 2.0f);
		Item->SetIntroOffset(-40.0f * (1.0f - A), A);
		++Shown;
	}

	const float Footer = Ramp(T, FooterStart, 0.3f);
	for (UCXPlayerSlotWidget* PlayerSlot : PlayerSlots)
	{
		PlayerSlot->SetRenderOpacity(Footer);
	}
	HintText->SetRenderOpacity(Footer);
	SaveWarningText->SetRenderOpacity(Footer);

	if (T >= IntroEnd)
	{
		FinishIntro();
	}
}

void UCXTitleMenuWidget::FinishIntro()
{
	IntroTime = -1.0f;
	FadeImage->SetRenderOpacity(0.0f);
	TitleText->SetRenderOpacity(1.0f);
	TitleUnderline->SetRenderOpacity(1.0f);
	TitleUnderline->SetRenderScale(FVector2D(1.0f, 1.0f));
	SubtitleText->SetRenderOpacity(1.0f);
	for (UCXMenuItemWidget* Item : Items)
	{
		if (Item)
		{
			Item->SetIntroOffset(0.0f, 1.0f);
		}
	}
	for (UCXPlayerSlotWidget* PlayerSlot : PlayerSlots)
	{
		PlayerSlot->SetRenderOpacity(1.0f);
	}
	HintText->SetRenderOpacity(1.0f);
	SaveWarningText->SetRenderOpacity(1.0f);
	// 연출 스킵 입력이 메뉴 동작으로 이어지지 않게 짧게 잠금
	LockInput();
}

bool UCXTitleMenuWidget::HandleAnyInput()
{
	if (IntroTime >= 0.0f && !bShortIntro && !bStarting)
	{
		FinishIntro();
		return true;
	}
	return false;
}

bool UCXTitleMenuWidget::CanAcceptInput() const
{
	// 게임 시작 페이드 중 입력은 전부 무시(8장), 진입 연출 중에도 무시
	return Super::CanAcceptInput() && !bStarting && IntroTime < 0.0f;
}

// ---------------------------------------------------------------------------------------------
// 세이브 / 슬롯 상태

void UCXTitleMenuWidget::RefreshSaveState()
{
	UCXSaveGame* Save = nullptr;
	const ECXSaveLoadResult Result = UCXSaveGame::LoadSlot(Save);
	LoadedSave = Save;
	bSaveExists = Result != ECXSaveLoadResult::NoSave;

	// 이어하기는 정상 세이브가 있을 때만. 손상이면 숨기고 경고(새 게임 덮어쓰기 확인창은 그대로).
	const bool bCanContinue = Result == ECXSaveLoadResult::Loaded;
	SetItemShown(CXTitle::Continue, bCanContinue);
	if (bCanContinue)
	{
		FindItem(CXTitle::Continue)->SetInfo(Save->GetSummaryText());
	}
	SaveWarningText->SetVisibility(Result == ECXSaveLoadResult::Corrupt ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	SetFocusItem(bCanContinue ? CXTitle::Continue : CXTitle::NewGame);
}

void UCXTitleMenuWidget::RefreshSlots()
{
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	for (int32 i = 0; i < PlayerSlots.Num(); ++i)
	{
		const ECXInputDeviceType Device = GI ? GI->GetSlotDeviceType(i) : (i == 0 ? ECXInputDeviceType::KeyboardMouse : ECXInputDeviceType::Gamepad);
		const bool bReady = GI ? GI->IsSlotReady(i) : (Device == ECXInputDeviceType::KeyboardMouse);
		PlayerSlots[i]->SetState(Device, bReady, bGamepadAttached);
	}
}

void UCXTitleMenuWidget::HandleGamepadStart(FInputDeviceId DeviceId)
{
	UCXGameInstance* GI = UCXGameInstance::Get(this);
	if (!GI || GI->FindSlotByGamepad(DeviceId) != INDEX_NONE)
	{
		return;
	}
	// 둘 다 게임패드면 먼저 Start를 누른 패드가 P1, 다음이 P2
	const int32 SlotIndex = GI->FindFirstUnassignedGamepadSlot();
	if (SlotIndex == INDEX_NONE)
	{
		return;
	}
	GI->AssignSlotGamepad(SlotIndex, DeviceId);
	bGamepadAttached = true;
	RefreshSlots();
	PlayUISound(ECXMenuSound::Confirm);

	if (GI->AreAllSlotsReady())
	{
		DevicePromptRemaining = FMath::Min(DevicePromptRemaining, 0.3f);
	}
}

void UCXTitleMenuWidget::HandleDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId UserId, FInputDeviceId DeviceId)
{
	if (NewState != EInputDeviceConnectionState::Disconnected)
	{
		return;
	}
	// READY였던 게임패드가 끊기면 다시 PRESS START
	if (UCXGameInstance* GI = UCXGameInstance::Get(this))
	{
		const int32 SlotIndex = GI->FindSlotByGamepad(DeviceId);
		if (SlotIndex != INDEX_NONE)
		{
			GI->ClearSlotGamepad(SlotIndex);
			RefreshSlots();
		}
	}
}

void UCXTitleMenuWidget::ShowDevicePrompt()
{
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	int32 Missing = INDEX_NONE;	// 안내 문구에 쓸 첫 미준비 슬롯
	for (int32 i = 0; i < PlayerSlots.Num(); ++i)
	{
		const bool bReady = GI ? GI->IsSlotReady(i) : PlayerSlots[i]->IsReady();
		if (!bReady)
		{
			PlayerSlots[i]->Pulse();
			Missing = (Missing == INDEX_NONE) ? i : Missing;
		}
	}
	Missing = (Missing == INDEX_NONE) ? 1 : Missing;
	DevicePromptText->SetText(FText::FromString(FString::Printf(TEXT("CONNECT P%d DEVICE AND PRESS START"), Missing + 1)));
	DevicePromptText->SetVisibility(ESlateVisibility::HitTestInvisible);
	DevicePromptRemaining = CXTitle::DevicePromptTime;
	PlayUISound(ECXMenuSound::Denied);
}

// ---------------------------------------------------------------------------------------------
// 입력 / 동작

bool UCXTitleMenuWidget::OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex)
{
	if (Input.Action == ECXMenuAction::Start)
	{
		if (Input.bFromGamepad && !Input.bRepeat)
		{
			HandleGamepadStart(Input.DeviceId);
		}
		return true;
	}
	return false;
}

void UCXTitleMenuWidget::OnItemActivated(FName ItemId)
{
	if (ItemId == CXTitle::Continue)
	{
		TryStartGame(true);
	}
	else if (ItemId == CXTitle::NewGame)
	{
		TryStartGame(false);
	}
	else if (ItemId == CXTitle::Settings)
	{
		// 설정 메뉴는 아직 미구현 → 자리표시 알림창
		PlayUISound(ECXMenuSound::Confirm);
		ShowNotice(FText::FromString(TEXT("SETTINGS — COMING SOON")),
			FText::FromString(TEXT("THE SETTINGS MENU IS NOT AVAILABLE YET.")));
	}
	else if (ItemId == CXTitle::Quit)
	{
		ShowConfirm(FText::FromString(TEXT("QUIT THE GAME?")), FText::GetEmpty(), [this]()
		{
			UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
		});
	}
}

bool UCXTitleMenuWidget::TryStartGame(bool bContinue)
{
	// 이어하기/새 게임은 두 플레이어 장치 확인이 끝나야 실행(5장)
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	if (GI && !GI->AreAllSlotsReady())
	{
		ShowDevicePrompt();
		return false;
	}

	if (bContinue)
	{
		if (!LoadedSave)
		{
			return false;
		}
		if (UCXGameInstance* MutableGI = UCXGameInstance::Get(this))
		{
			MutableGI->PendingContinueSave = LoadedSave;
		}
		PlayUISound(ECXMenuSound::Confirm);
		BeginStartGame(LoadedSave->MapName.IsEmpty() ? NewGameMap : LoadedSave->MapName);
		return true;
	}

	auto StartNew = [this]()
	{
		UCXSaveGame::DeleteSlot();
		BeginStartGame(NewGameMap);
	};

	if (bSaveExists)
	{
		ShowConfirm(FText::FromString(TEXT("START A NEW GAME?")),
			FText::FromString(TEXT("YOUR EXISTING SAVE WILL BE OVERWRITTEN. THIS CANNOT BE UNDONE.")),
			StartNew);
	}
	else
	{
		// 세이브가 없으면 확인창 없이 바로(9장)
		PlayUISound(ECXMenuSound::Confirm);
		StartNew();
	}
	return true;
}

void UCXTitleMenuWidget::BeginStartGame(const FString& MapPath)
{
	if (bStarting)
	{
		return;
	}
	bStarting = true;
	StartFadeElapsed = 0.0f;
	PendingMap = MapPath;
	DevicePromptText->SetVisibility(ESlateVisibility::Collapsed);
}

FString UCXTitleMenuWidget::GetDebugState() const
{
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	return FString::Printf(TEXT("%s save=%d intro=%.2f starting=%d slots=[%d,%d] gamepad=%d"), *Super::GetDebugState(),
		bSaveExists ? 1 : 0, IntroTime, bStarting ? 1 : 0,
		GI && GI->IsSlotReady(0) ? 1 : 0, GI && GI->IsSlotReady(1) ? 1 : 0, bGamepadAttached ? 1 : 0);
}
