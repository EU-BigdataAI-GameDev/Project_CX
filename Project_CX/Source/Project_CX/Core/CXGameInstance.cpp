// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXGameInstance.h"
#include "CXSaveGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

UCXGameInstance::UCXGameInstance()
{
}

UCXGameInstance* UCXGameInstance::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? Cast<UCXGameInstance>(World->GetGameInstance()) : nullptr;
}

ECXInputDeviceType UCXGameInstance::GetSlotDeviceType(int32 PlayerIndex) const
{
	// 조작 장치는 설정(GameUserSettings.ini)이 유일한 기준
	return IsValidSlot(PlayerIndex) ? UCXGameUserSettings::GetCXGameUserSettings()->GetSlotDevice(PlayerIndex) : ECXInputDeviceType::Gamepad;
}

bool UCXGameInstance::SetSlotDeviceType(int32 PlayerIndex, ECXInputDeviceType DeviceType)
{
	if (!IsValidSlot(PlayerIndex))
	{
		return false;
	}
	UCXGameUserSettings* Settings = UCXGameUserSettings::GetCXGameUserSettings();
	if (Settings->GetSlotDevice(PlayerIndex) == DeviceType)
	{
		return true;
	}
	if (!Settings->SetSlotDevice(PlayerIndex, DeviceType))
	{
		return false;
	}
	// 비정상 종료 대비: 조작 장치 변경은 즉시 저장(설정 메뉴 기획 5장)
	Settings->SaveSettings();
	ClearSlotGamepad(PlayerIndex);
	return true;
}

int32 UCXGameInstance::GetConnectedGamepadCount()
{
	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsGamepadAttached())
	{
		return 0;
	}
	// 키보드/마우스(기본 장치)를 뺀 연결 장치 수. XInput 최대 4개.
	IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
	TArray<FInputDeviceId> Devices;
	Mapper.GetAllConnectedInputDevices(Devices);
	int32 Count = 0;
	for (const FInputDeviceId& Device : Devices)
	{
		if (Device != Mapper.GetDefaultInputDevice())
		{
			++Count;
		}
	}
	return FMath::Clamp(Count, 1, 4);
}

bool UCXGameInstance::IsSlotReady(int32 PlayerIndex) const
{
	if (!IsValidSlot(PlayerIndex))
	{
		return false;
	}
	return GetSlotDeviceType(PlayerIndex) == ECXInputDeviceType::KeyboardMouse || SlotGamepads[PlayerIndex].IsValid();
}

bool UCXGameInstance::AreAllSlotsReady() const
{
	for (int32 i = 0; i < NumPlayerSlots; ++i)
	{
		if (!IsSlotReady(i))
		{
			return false;
		}
	}
	return true;
}

FInputDeviceId UCXGameInstance::GetSlotGamepad(int32 PlayerIndex) const
{
	return IsValidSlot(PlayerIndex) ? SlotGamepads[PlayerIndex] : INPUTDEVICEID_NONE;
}

void UCXGameInstance::AssignSlotGamepad(int32 PlayerIndex, FInputDeviceId DeviceId)
{
	if (IsValidSlot(PlayerIndex))
	{
		SlotGamepads[PlayerIndex] = DeviceId;
	}
}

void UCXGameInstance::ClearSlotGamepad(int32 PlayerIndex)
{
	if (IsValidSlot(PlayerIndex))
	{
		SlotGamepads[PlayerIndex] = INPUTDEVICEID_NONE;
	}
}

void UCXGameInstance::ClearAllSlotGamepads()
{
	for (int32 i = 0; i < NumPlayerSlots; ++i)
	{
		SlotGamepads[i] = INPUTDEVICEID_NONE;
	}
}

int32 UCXGameInstance::FindSlotByGamepad(FInputDeviceId DeviceId) const
{
	if (!DeviceId.IsValid())
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < NumPlayerSlots; ++i)
	{
		if (SlotGamepads[i] == DeviceId)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UCXGameInstance::FindFirstUnassignedGamepadSlot() const
{
	for (int32 i = 0; i < NumPlayerSlots; ++i)
	{
		if (GetSlotDeviceType(i) == ECXInputDeviceType::Gamepad && !SlotGamepads[i].IsValid())
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UCXGameInstance::ResolvePlayerForInput(bool bFromGamepad, FInputDeviceId DeviceId) const
{
	if (bFromGamepad)
	{
		const int32 Assigned = FindSlotByGamepad(DeviceId);
		if (Assigned != INDEX_NONE)
		{
			return Assigned;
		}
		const int32 Free = FindFirstUnassignedGamepadSlot();
		if (Free != INDEX_NONE)
		{
			return Free;
		}
		// 게임패드 슬롯이 없는 조합(둘 다 키보드)이면 P2 색
		return 1;
	}
	for (int32 i = 0; i < NumPlayerSlots; ++i)
	{
		if (GetSlotDeviceType(i) == ECXInputDeviceType::KeyboardMouse)
		{
			return i;
		}
	}
	return 0;
}
