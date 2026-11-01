// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXMenuSubsystem.h"
#include "CXMenuInputProcessor.h"
#include "CXMenuWidgetBase.h"
#include "CXGameInstance.h"
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
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UCXMenuSubsystem::HandlePreLoadMap);
}

void UCXMenuSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
	MenuStack.Reset();
	UpdateInputProcessor();
	InputProcessor.Reset();
	Super::Deinitialize();
}

void UCXMenuSubsystem::HandlePreLoadMap(const FString& MapName)
{
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
	const bool bWant = MenuStack.Num() > 0;
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
	if (Action < static_cast<int32>(ECXMenuAction::Up) || Action > static_cast<int32>(ECXMenuAction::Start))
	{
		return false;
	}
	FCXMenuInput Input;
	Input.Action = static_cast<ECXMenuAction>(Action);
	Input.bFromGamepad = bGamepad;
	Input.DeviceId = FInputDeviceId::CreateFromInternalId(DeviceId);
	// 같은 프레임 중복 필터에 걸리지 않게
	LastInputFrame = 0;
	return DispatchInput(Input);
}

FString UCXMenuSubsystem::GetTopMenuDebugState() const
{
	const UCXMenuWidgetBase* Menu = GetTopMenu();
	return Menu ? FString::Printf(TEXT("%s %s"), *Menu->GetClass()->GetName(), *Menu->GetDebugState()) : TEXT("none");
}
