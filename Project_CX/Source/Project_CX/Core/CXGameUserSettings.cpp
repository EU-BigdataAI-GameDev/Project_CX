// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXGameUserSettings.h"
#include "Engine/Engine.h"

UCXGameUserSettings::UCXGameUserSettings()
{
	ResetCXSettings();
}

UCXGameUserSettings* UCXGameUserSettings::GetCXGameUserSettings()
{
	if (GEngine)
	{
		if (UCXGameUserSettings* Settings = Cast<UCXGameUserSettings>(GEngine->GetGameUserSettings()))
		{
			return Settings;
		}
	}
	// GameUserSettingsClassName이 빠진 경우: 기본값으로 동작(저장은 안 됨)
	return GetMutableDefault<UCXGameUserSettings>();
}

void UCXGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	ResetCXSettings();
}

void UCXGameUserSettings::LoadSettings(bool bForceReload)
{
	Super::LoadSettings(bForceReload);
	// 저장 파일이 손상됐으면 기본값으로(5장)
	SanitizeCXSettings();
}

void UCXGameUserSettings::ResetCXSettings()
{
	ResetAudioToDefaults();
	SplitLayout = ECXSplitLayout::TopBottom;
	BrotherDevice = ECXInputDeviceType::KeyboardMouse;
	SisterDevice = ECXInputDeviceType::Gamepad;
}

void UCXGameUserSettings::ResetAudioToDefaults()
{
	MasterVolume = DefaultMasterVolume;
	MusicVolume = DefaultMusicVolume;
	SfxVolume = DefaultSfxVolume;
}

void UCXGameUserSettings::SanitizeCXSettings()
{
	MasterVolume = SnapVolume(MasterVolume);
	MusicVolume = SnapVolume(MusicVolume);
	SfxVolume = SnapVolume(SfxVolume);

	if (SplitLayout != ECXSplitLayout::TopBottom && SplitLayout != ECXSplitLayout::LeftRight)
	{
		SplitLayout = ECXSplitLayout::TopBottom;
	}

	auto IsValidDevice = [](ECXInputDeviceType D) { return D == ECXInputDeviceType::KeyboardMouse || D == ECXInputDeviceType::Gamepad; };
	const bool bBothKeyboard = BrotherDevice == ECXInputDeviceType::KeyboardMouse && SisterDevice == ECXInputDeviceType::KeyboardMouse;
	if (!IsValidDevice(BrotherDevice) || !IsValidDevice(SisterDevice) || bBothKeyboard)
	{
		BrotherDevice = ECXInputDeviceType::KeyboardMouse;
		SisterDevice = ECXInputDeviceType::Gamepad;
	}
}

int32 UCXGameUserSettings::SnapVolume(int32 Value)
{
	const int32 Clamped = FMath::Clamp(Value, 0, 100);
	return FMath::RoundToInt(static_cast<float>(Clamped) / VolumeStep) * VolumeStep;
}

ECXInputDeviceType UCXGameUserSettings::GetSlotDevice(int32 PlayerIndex) const
{
	return PlayerIndex == 0 ? BrotherDevice : SisterDevice;
}

bool UCXGameUserSettings::SetSlotDevice(int32 PlayerIndex, ECXInputDeviceType Device)
{
	if (!IsValidSlot(PlayerIndex))
	{
		return false;
	}
	const ECXInputDeviceType Other = GetSlotDevice(1 - PlayerIndex);
	if (Device == ECXInputDeviceType::KeyboardMouse && Other == ECXInputDeviceType::KeyboardMouse)
	{
		// 키보드는 하나뿐 — 둘 다 키보드는 불가
		return false;
	}
	(PlayerIndex == 0 ? BrotherDevice : SisterDevice) = Device;
	return true;
}

int32 UCXGameUserSettings::GetRequiredGamepadCount() const
{
	return (BrotherDevice == ECXInputDeviceType::Gamepad ? 1 : 0) + (SisterDevice == ECXInputDeviceType::Gamepad ? 1 : 0);
}

bool UCXGameUserSettings::IsCharacterSwapLayout() const
{
	return BrotherDevice == ECXInputDeviceType::Gamepad && SisterDevice == ECXInputDeviceType::KeyboardMouse;
}

int32 UCXGameUserSettings::GetLocalPlayerIndexForSlot(int32 PlayerIndex) const
{
	const int32 Slot = IsValidSlot(PlayerIndex) ? PlayerIndex : 0;
	return IsCharacterSwapLayout() ? 1 - Slot : Slot;
}
