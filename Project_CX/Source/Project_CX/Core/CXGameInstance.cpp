// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXGameInstance.h"
#include "CXSaveGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

UCXGameInstance::UCXGameInstance()
{
	// 설정 메뉴가 없으므로 기획 기본값: 오빠 키보드/마우스, 여동생 게임패드
	SlotDeviceTypes = { ECXInputDeviceType::KeyboardMouse, ECXInputDeviceType::Gamepad };
}

UCXGameInstance* UCXGameInstance::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? Cast<UCXGameInstance>(World->GetGameInstance()) : nullptr;
}

ECXInputDeviceType UCXGameInstance::GetSlotDeviceType(int32 PlayerIndex) const
{
	return SlotDeviceTypes.IsValidIndex(PlayerIndex) ? SlotDeviceTypes[PlayerIndex] : ECXInputDeviceType::Gamepad;
}

void UCXGameInstance::SetSlotDeviceType(int32 PlayerIndex, ECXInputDeviceType DeviceType)
{
	if (!IsValidSlot(PlayerIndex))
	{
		return;
	}
	SlotDeviceTypes.SetNum(NumPlayerSlots);
	if (SlotDeviceTypes[PlayerIndex] != DeviceType)
	{
		SlotDeviceTypes[PlayerIndex] = DeviceType;
		ClearSlotGamepad(PlayerIndex);
	}
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
