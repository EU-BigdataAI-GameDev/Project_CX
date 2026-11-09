// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXGameViewportClient.h"
#include "CXGameUserSettings.h"
#include "CXGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"

void UCXGameViewportClient::UpdateActiveSplitscreenType()
{
	Super::UpdateActiveSplitscreenType();

	// 2인 분할일 때만 설정값으로 상하/좌우를 고른다(1인/분할 끔은 엔진 판단 그대로)
	if (ActiveSplitscreenType == ESplitScreenType::TwoPlayer_Horizontal || ActiveSplitscreenType == ESplitScreenType::TwoPlayer_Vertical)
	{
		const UCXGameUserSettings* Settings = UCXGameUserSettings::GetCXGameUserSettings();
		ActiveSplitscreenType = Settings->GetSplitLayout() == ECXSplitLayout::LeftRight
			? ESplitScreenType::TwoPlayer_Vertical
			: ESplitScreenType::TwoPlayer_Horizontal;
	}
}

int32 UCXGameViewportClient::ResolveGamepadLocalPlayer(FInputDeviceId InputDevice, int32 ControllerId) const
{
	const UGameInstance* OwningGI = World ? World->GetGameInstance() : nullptr;
	if (!OwningGI || OwningGI->GetNumLocalPlayers() < 2)
	{
		return INDEX_NONE;
	}
	const UCXGameUserSettings* Settings = UCXGameUserSettings::GetCXGameUserSettings();
	if (Settings->GetRequiredGamepadCount() < 2)
	{
		// 패드가 한 명 몫이면 엔진의 오프셋(모든 패드 → LocalPlayer 1)이 그대로 맞다
		return INDEX_NONE;
	}

	// 둘 다 게임패드: 시작화면 PRESS START로 배정된 패드면 그 슬롯, 아니면 ControllerId 순서(패드0 → P1, 그 외 → P2)
	const UCXGameInstance* GI = Cast<UCXGameInstance>(OwningGI);
	int32 Slot = GI ? GI->FindSlotByGamepad(InputDevice) : INDEX_NONE;
	if (Slot == INDEX_NONE)
	{
		Slot = ControllerId <= 0 ? 0 : 1;
	}
	return Settings->GetLocalPlayerIndexForSlot(Slot);
}

void UCXGameViewportClient::RemapControllerInput(FInputKeyEventArgs& InOutKeyEvent)
{
	if (!InOutKeyEvent.Key.IsGamepadKey())
	{
		Super::RemapControllerInput(InOutKeyEvent);
		return;
	}
	const int32 TargetIndex = ResolveGamepadLocalPlayer(InOutKeyEvent.InputDevice, InOutKeyEvent.ControllerId);
	const UGameInstance* OwningGI = World ? World->GetGameInstance() : nullptr;
	const ULocalPlayer* Target = (TargetIndex != INDEX_NONE && OwningGI) ? OwningGI->GetLocalPlayerByIndex(TargetIndex) : nullptr;
	if (!Target)
	{
		Super::RemapControllerInput(InOutKeyEvent);
		return;
	}

	// 대상 로컬 플레이어의 플랫폼 사용자에 매핑된 장치로 바꿔 보낸다(엔진 오프셋 처리와 같은 방식)
	IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
	const FPlatformUserId TargetUser = Target->GetPlatformUserId();
	if (DeviceMapper.GetUserForInputDevice(InOutKeyEvent.InputDevice) != TargetUser)
	{
		FInputDeviceId TargetDevice = DeviceMapper.GetPrimaryInputDeviceForUser(TargetUser);
		if (!TargetDevice.IsValid())
		{
			TargetDevice = DeviceMapper.AllocateNewInputDeviceId();
			DeviceMapper.Internal_MapInputDeviceToUser(TargetDevice, TargetUser, EInputDeviceConnectionState::Connected);
		}
		InOutKeyEvent.InputDevice = TargetDevice;
	}
	InOutKeyEvent.ControllerId = Target->GetControllerId();
}

int32 UCXGameViewportClient::DebugResolveGamepadPlayer(const UObject* WorldContext, int32 InputDeviceId, int32 ControllerId)
{
	const UWorld* ContextWorld = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* OwningGI = ContextWorld ? ContextWorld->GetGameInstance() : nullptr;
	UCXGameViewportClient* Client = OwningGI ? Cast<UCXGameViewportClient>(OwningGI->GetGameViewportClient()) : nullptr;
	if (!Client)
	{
		return -1;
	}
	FInputKeyEventArgs Args(Client->Viewport, FInputDeviceId::CreateFromInternalId(InputDeviceId), EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, FPlatformTime::Cycles64());
	Args.ControllerId = ControllerId;
	Client->RemapControllerInput(Args);
	const ULocalPlayer* Player = GEngine->GetLocalPlayerFromInputDevice(Client, Args.InputDevice);
	return Player ? Player->GetLocalPlayerIndex() : -1;
}
