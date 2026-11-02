// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXMenuSubsystem.h"
#include "CXMenuInputProcessor.h"
#include "CXMenuWidgetBase.h"
#include "CXPauseMenuWidget.h"
#include "CXGameInstance.h"
#include "CXTitleGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LocalPlayer.h"
#include "Sound/SoundMix.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "UObject/UObjectGlobals.h"

UCXMenuSubsystem* UCXMenuSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCXMenuSubsystem>() : nullptr;
}

void UCXMenuSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PauseMenuClass = UCXPauseMenuWidget::StaticClass();
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UCXMenuSubsystem::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UCXMenuSubsystem::HandlePostLoadMap);
	DeviceConnectionHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddUObject(this, &UCXMenuSubsystem::HandleDeviceConnectionChange);
	// ESC 메뉴 열기 키를 보려면 메뉴가 없어도 프리프로세서가 필요하다
	UpdateInputProcessor();
}

void UCXMenuSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(DeviceConnectionHandle);
	SetPauseSoundMix(false);
	MenuStack.Reset();
	PauseMenu = nullptr;
	bDeinitializing = true;
	UpdateInputProcessor();
	InputProcessor.Reset();
	Super::Deinitialize();
}

void UCXMenuSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	bMapLoading = false;
	bLeavingMap = false;
	// Initialize 시점에 Slate가 아직 없었다면 여기서 등록
	UpdateInputProcessor();
}

void UCXMenuSubsystem::HandlePreLoadMap(const FString& MapName)
{
	// 맵 로딩/전환 중에는 ESC 메뉴를 열 수 없다
	bMapLoading = true;
	ResetPauseState();

	// 맵 이동 시 뷰포트 위젯은 엔진이 모두 내린다 → 스택만 정리.
	// UI 전용 입력 모드(IgnoreInput, 마우스 캡처 없음)는 GameViewportClient에 남아 다음 맵까지 이어지므로 게임 입력으로 되돌린다.
	if (UCXMenuWidgetBase* Top = GetTopMenu())
	{
		if (APlayerController* PC = Top->GetOwningPlayer())
		{
			PC->SetInputMode(FInputModeGameOnly());
			PC->SetShowMouseCursor(false);
		}
	}
	MenuStack.Reset();
	UpdateInputProcessor();
}

UCXMenuWidgetBase* UCXMenuSubsystem::PushMenu(TSubclassOf<UCXMenuWidgetBase> MenuClass, APlayerController* OwningPlayer, int32 ZOrder)
{
	if (!MenuClass || !OwningPlayer)
	{
		return nullptr;
	}
	UCXMenuWidgetBase* Menu = CreateWidget<UCXMenuWidgetBase>(OwningPlayer, MenuClass);
	if (!Menu)
	{
		return nullptr;
	}
	// 분할 화면이어도 화면 전체에 하나(0.1) → 플레이어 칸이 아니라 뷰포트에 올린다.
	Menu->AddToViewport(ZOrder);
	MenuStack.Add(Menu);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Menu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	OwningPlayer->SetInputMode(InputMode);
	// 커서는 마우스를 움직이면 나타난다(NotifyInputDeviceUsed). 처음엔 키/패드 포커스 기준.
	OwningPlayer->SetShowMouseCursor(false);

	UpdateInputProcessor();
	return Menu;
}

void UCXMenuSubsystem::PopMenu(UCXMenuWidgetBase* Menu)
{
	if (!Menu)
	{
		return;
	}
	APlayerController* OwningPlayer = Menu->GetOwningPlayer();
	MenuStack.Remove(Menu);
	Menu->RemoveFromParent();

	if (UCXMenuWidgetBase* Top = GetTopMenu())
	{
		Top->LockInput();
		// 내린 메뉴가 키보드 포커스를 갖고 있었으므로 아래 메뉴로 포커스를 돌려준다(포커스가 비면 IsGameViewportFocused가 false가 됨)
		if (APlayerController* TopOwner = Top->GetOwningPlayer())
		{
			FInputModeUIOnly InputMode;
			InputMode.SetWidgetToFocus(Top->TakeWidget());
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			TopOwner->SetInputMode(InputMode);
		}
	}
	else if (OwningPlayer)
	{
		OwningPlayer->SetInputMode(FInputModeGameOnly());
		OwningPlayer->SetShowMouseCursor(false);
	}
	UpdateInputProcessor();
}

UCXMenuWidgetBase* UCXMenuSubsystem::GetTopMenu() const
{
	for (int32 i = MenuStack.Num() - 1; i >= 0; --i)
	{
		if (MenuStack[i] && MenuStack[i]->IsInViewport())
		{
			return MenuStack[i];
		}
	}
	return nullptr;
}

void UCXMenuSubsystem::UpdateInputProcessor()
{
	if (!FSlateApplication::IsInitialized())
	{
		bProcessorRegistered = false;
		return;
	}
	// ESC 메뉴 열기 키 감시를 위해 서브시스템이 살아 있는 동안 항상 등록(메뉴가 없으면 ESC/Start 외에는 흘려보냄)
	const bool bWant = !bDeinitializing;
	if (bWant && !bProcessorRegistered)
	{
		if (!InputProcessor.IsValid())
		{
			InputProcessor = MakeShared<FCXMenuInputProcessor>(this);
		}
		// 맨 앞(0번)에 등록 → 다른 프리프로세서보다 먼저 메뉴 입력을 본다.
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor, 0);
		bProcessorRegistered = true;
	}
	else if (!bWant && bProcessorRegistered)
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
		bProcessorRegistered = false;
	}
}

bool UCXMenuSubsystem::IsGameViewportFocused() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UGameViewportClient* ViewportClient = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	const TSharedPtr<SViewport> ViewportWidget = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr;
	if (!ViewportWidget.IsValid() || !FSlateApplication::IsInitialized())
	{
		return false;
	}
	FSlateApplication& Slate = FSlateApplication::Get();
	if (!Slate.IsActive())
	{
		return false;
	}
	// 키보드 포커스가 게임 뷰포트(또는 그 안의 메뉴 위젯)에 있어야 한다. 에디터 다른 패널에 포커스가 있으면 흘려보낸다.
	return ViewportWidget->HasFocusedDescendants() || ViewportWidget->HasAnyUserFocus().IsSet();
}

bool UCXMenuSubsystem::RouteMenuInput(const FCXMenuInput& Input)
{
	return DispatchInput(Input);
}

bool UCXMenuSubsystem::DispatchInput(const FCXMenuInput& Input)
{
	UCXMenuWidgetBase* Menu = GetTopMenu();
	if (!Menu)
	{
		return false;
	}

	// 진입 연출 중이면 아무 입력이나 연출 완료(입력 자체는 소비)
	if (!Input.bRepeat && Menu->HandleAnyInput())
	{
		return true;
	}

	const UCXGameInstance* GI = Cast<UCXGameInstance>(GetGameInstance());
	const int32 Player = GI ? GI->ResolvePlayerForInput(Input.bFromGamepad, Input.DeviceId) : (Input.bFromGamepad ? 1 : 0);

	// 같은 프레임에 다른 플레이어 입력이 또 오면 버린다(먼저 온 입력 우선). Start(장치 확인)는 예외.
	const uint64 Frame = GFrameCounter;
	if (Input.Action != ECXMenuAction::Start && !Input.bRepeat)
	{
		if (Frame == LastInputFrame && LastInputPlayer != INDEX_NONE && LastInputPlayer != Player)
		{
			return true;
		}
		LastInputFrame = Frame;
		LastInputPlayer = Player;
	}
	return Menu->HandleMenuInput(Input, Player);
}

bool UCXMenuSubsystem::RouteAnyInput()
{
	UCXMenuWidgetBase* Menu = GetTopMenu();
	return Menu && Menu->HandleAnyInput();
}

void UCXMenuSubsystem::NotifyInputDeviceUsed(bool bMouse)
{
	UCXMenuWidgetBase* Menu = GetTopMenu();
	APlayerController* PC = Menu ? Menu->GetOwningPlayer() : nullptr;
	if (PC && PC->ShouldShowMouseCursor() != bMouse)
	{
		// 마우스 커서는 마우스가 움직일 때만 보이고, 키/패드 입력이 오면 숨긴다(0.2).
		PC->SetShowMouseCursor(bMouse);
	}
}

bool UCXMenuSubsystem::SimulateMenuInput(int32 Action, bool bGamepad, int32 DeviceId)
{
	if (Action < static_cast<int32>(ECXMenuAction::Up) || Action > static_cast<int32>(ECXMenuAction::TabRight))
	{
		return false;
	}
	FCXMenuInput Input;
	Input.Action = static_cast<ECXMenuAction>(Action);
	Input.bFromGamepad = bGamepad;
	Input.DeviceId = FInputDeviceId::CreateFromInternalId(DeviceId);
	// 메뉴가 없을 때 ESC(Back)/Start = 프리프로세서와 같은 일시정지 메뉴 열기 경로
	if (!HasActiveMenu() && (Input.Action == ECXMenuAction::Back || Input.Action == ECXMenuAction::Start))
	{
		return HandlePauseKey(bGamepad, Input.DeviceId);
	}
	// 같은 프레임 중복 필터에 걸리지 않게
	LastInputFrame = 0;
	return DispatchInput(Input);
}

FString UCXMenuSubsystem::GetTopMenuDebugState() const
{
	const UCXMenuWidgetBase* Menu = GetTopMenu();
	return Menu ? FString::Printf(TEXT("%s %s"), *Menu->GetClass()->GetName(), *Menu->GetDebugState()) : TEXT("none");
}

// ---------------------------------------------------------------------------------------------
// ESC(일시정지) 메뉴

bool UCXMenuSubsystem::IsPauseableWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || !World->HasBegunPlay() || World->bIsTearingDown)
	{
		return false;
	}
	// 시작화면은 자체 메뉴가 있다(리슨 서버 클라이언트는 AuthGameMode가 없으므로 null이면 인게임으로 본다)
	return !Cast<ACXTitleGameMode>(World->GetAuthGameMode()) && GameInstance->GetFirstLocalPlayerController(World) != nullptr;
}

bool UCXMenuSubsystem::CanOpenPauseMenu() const
{
	return !bMenuBlocked && !bMapLoading && !bLeavingMap && !HasActiveMenu() && !PauseMenu && IsPauseableWorld();
}

bool UCXMenuSubsystem::IsGameplayInputBlocked() const
{
	return FPlatformTime::Seconds() < GameplayInputBlockEnd;
}

bool UCXMenuSubsystem::HandlePauseKey(bool bFromGamepad, FInputDeviceId DeviceId)
{
	if (!IsPauseableWorld())
	{
		// 시작화면 등: ESC를 소비하지 않는다(에디터 PIE의 ESC 종료 유지)
		return false;
	}
	if (CanOpenPauseMenu())
	{
		const UCXGameInstance* GI = Cast<UCXGameInstance>(GetGameInstance());
		const int32 Player = GI ? GI->ResolvePlayerForInput(bFromGamepad, DeviceId) : (bFromGamepad ? 1 : 0);
		OpenPauseMenu(UGameplayStatics::GetPlayerController(GetGameInstance()->GetWorld(), Player));
	}
	// 인게임에서는 열 수 없는 상태(로딩/차단)라도 ESC를 게임/에디터로 흘리지 않는다
	return true;
}

bool UCXMenuSubsystem::OpenPauseMenu(APlayerController* Requester)
{
	if (!CanOpenPauseMenu())
	{
		return false;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance->GetWorld();
	// 메뉴는 분할 화면 전체에 하나 → 1P 컨트롤러가 소유(누가 열었든 누구나 조작)
	APlayerController* Owner = GameInstance->GetFirstLocalPlayerController(World);
	if (!Owner)
	{
		return false;
	}

	// 월드 전체 일시정지(로컬 협동이라 두 플레이어 모두, 해킹 세션/타이머 포함)
	UGameplayStatics::SetGamePaused(World, true);
	bReconnected = false;

	PauseMenu = Cast<UCXPauseMenuWidget>(PushMenu(PauseMenuClass, Owner, 200));
	if (!PauseMenu)
	{
		UGameplayStatics::SetGamePaused(World, false);
		return false;
	}
	// 포커스 색 = 연 사람 색(연결 끊김 자동 열기는 기본값 유지)
	if (Requester && Requester->GetLocalPlayer())
	{
		PauseMenu->SetOpenedBy(Requester->GetLocalPlayer()->GetLocalPlayerIndex());
	}
	SetPauseSoundMix(true);
	return true;
}

void UCXMenuSubsystem::FinishClosePauseMenu(UCXPauseMenuWidget* Menu)
{
	if (!Menu || Menu != PauseMenu)
	{
		return;
	}
	PauseMenu = nullptr;
	bReconnected = false;
	PopMenu(Menu);
	SetPauseSoundMix(false);

	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (World)
	{
		UGameplayStatics::SetGamePaused(World, false);
		// 메뉴에서 누른 키가 눌린 채로 게임 입력에 남지 않게
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				PC->FlushPressedKeys();
			}
		}
	}
	// 재개 직후 0.15초 게임플레이 입력 무시(A/Enter가 점프·사격으로 새지 않게)
	GameplayInputBlockEnd = FPlatformTime::Seconds() + CXMenuStyle::InputLockTime;
}

void UCXMenuSubsystem::ResetPauseState()
{
	SetPauseSoundMix(false);
	PauseMenu = nullptr;
	bReconnected = false;
	DisconnectedPlayer = INDEX_NONE;
	GameplayInputBlockEnd = 0.0;
	// 게임 클리어/오버 등에서 켠 차단은 그 맵에서만 — 맵 이동(PreLoadMap) 때 풀어서 다음 세션에서 ESC가 다시 동작하게
	bMenuBlocked = false;
}

void UCXMenuSubsystem::SetPauseSoundMix(bool bPush)
{
	// BGM 덕킹 훅: 에셋(SM_PauseDuck)이 아직 없어 PauseSoundMix가 비어 있으면 아무것도 안 한다
	if (bPush == bSoundMixPushed)
	{
		return;
	}
	USoundMix* Mix = PauseSoundMix.IsNull() ? nullptr : PauseSoundMix.LoadSynchronous();
	const UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!Mix || !World)
	{
		bSoundMixPushed = false;
		return;
	}
	if (bPush)
	{
		UGameplayStatics::PushSoundMixModifier(World, Mix);
	}
	else
	{
		UGameplayStatics::PopSoundMixModifier(World, Mix);
	}
	bSoundMixPushed = bPush;
}

// ---------------------------------------------------------------------------------------------
// 게임패드 연결 끊김(ESC 메뉴 기획 6장)

void UCXMenuSubsystem::HandleDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId UserId, FInputDeviceId DeviceId)
{
	// 키보드/마우스(기본 입력 장치)는 대상 아님. 시작화면은 자체 처리(CXTitleMenuWidget).
	if (DeviceId == IPlatformInputDeviceMapper::Get().GetDefaultInputDevice() || !IsPauseableWorld())
	{
		return;
	}
	UCXGameInstance* GI = Cast<UCXGameInstance>(GetGameInstance());

	if (NewState == EInputDeviceConnectionState::Disconnected)
	{
		// 시작화면에서 배정된 패드면 그 슬롯, 아니면(맵에서 바로 시작 등) 첫 게임패드 슬롯(기본 P2 여동생)
		int32 Player = GI ? GI->FindSlotByGamepad(DeviceId) : INDEX_NONE;
		if (Player == INDEX_NONE)
		{
			for (int32 i = 0; i < UCXGameInstance::NumPlayerSlots; ++i)
			{
				if (!GI || GI->GetSlotDeviceType(i) == ECXInputDeviceType::Gamepad)
				{
					Player = i;
					break;
				}
			}
		}
		if (Player != INDEX_NONE)
		{
			SetPlayerConnection(Player, false);
		}
	}
	else if (NewState == EInputDeviceConnectionState::Connected && DisconnectedPlayer != INDEX_NONE)
	{
		// 끊긴 슬롯에 다시 연결된 패드를 배정(같은 패드면 같은 ID)
		if (GI && GI->GetSlotDeviceType(DisconnectedPlayer) == ECXInputDeviceType::Gamepad && GI->FindSlotByGamepad(DeviceId) == INDEX_NONE)
		{
			GI->AssignSlotGamepad(DisconnectedPlayer, DeviceId);
		}
		SetPlayerConnection(DisconnectedPlayer, true);
	}
}

void UCXMenuSubsystem::SetPlayerConnection(int32 PlayerIndex, bool bConnected)
{
	if (!bConnected)
	{
		DisconnectedPlayer = PlayerIndex;
		bReconnected = false;
		// 즉시 일시정지 메뉴 자동 열기(이미 열려 있으면 경고만 갱신)
		if (!PauseMenu)
		{
			OpenPauseMenu(nullptr);
		}
	}
	else if (DisconnectedPlayer == PlayerIndex)
	{
		DisconnectedPlayer = INDEX_NONE;
		bReconnected = PauseMenu != nullptr;
	}
	if (PauseMenu)
	{
		PauseMenu->RefreshConnectionState();
	}
}

void UCXMenuSubsystem::DebugSimulateGamepadConnection(int32 PlayerIndex, bool bConnected)
{
	if (PlayerIndex >= 0 && PlayerIndex < UCXGameInstance::NumPlayerSlots && IsPauseableWorld())
	{
		SetPlayerConnection(PlayerIndex, bConnected);
	}
}
