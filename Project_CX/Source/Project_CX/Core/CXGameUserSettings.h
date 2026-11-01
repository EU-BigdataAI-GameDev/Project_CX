// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "CXGameUserSettings.generated.h"

/** 플레이어 슬롯(P1 오빠 / P2 여동생)이 쓰는 조작 장치 종류(설정 메뉴 기획 4.4 `조작 장치`). */
UENUM(BlueprintType)
enum class ECXInputDeviceType : uint8
{
	KeyboardMouse	UMETA(DisplayName = "Keyboard & Mouse"),
	Gamepad			UMETA(DisplayName = "Gamepad")
};

/** 2인 화면 분할 방식(설정 메뉴 기획 4.3). */
UENUM(BlueprintType)
enum class ECXSplitLayout : uint8
{
	TopBottom	UMETA(DisplayName = "Top / Bottom"),
	LeftRight	UMETA(DisplayName = "Left / Right")
};

/**
 *  설정 메뉴 값 저장(설정 메뉴 기획 5장 / 8장) — GameUserSettings.ini에 저장되어 세이브(USaveGame)와 분리된다.
 *
 *  - 음량 3개(0~100, 10단위), 화면 분할 방식, 슬롯별 조작 장치
 *  - 조작 장치는 이 값이 유일한 기준(UCXGameInstance가 여기서 읽는다)
 *  - 파일이 없거나 값이 깨져 있으면 기본값(둘 다 키보드 같은 불가 조합도 기본값으로 되돌림)
 *  - 저장: 설정 메뉴를 닫을 때 SaveSettings, 조작 장치는 바꾸는 즉시 저장
 *
 *  DefaultEngine.ini [/Script/Engine.Engine] GameUserSettingsClassName으로 지정한다.
 */
UCLASS(BlueprintType)
class UCXGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	static constexpr int32 DefaultMasterVolume = 80;
	static constexpr int32 DefaultMusicVolume = 70;
	static constexpr int32 DefaultSfxVolume = 80;
	static constexpr int32 VolumeStep = 10;

	UCXGameUserSettings();

	/** 엔진의 GameUserSettings. 클래스 지정이 빠졌으면 CDO(기본값, 세션 동안만 유지)로 대신한다. */
	UFUNCTION(BlueprintPure, Category = "CX|Settings")
	static UCXGameUserSettings* GetCXGameUserSettings();

	virtual void SetToDefaults() override;
	virtual void LoadSettings(bool bForceReload = false) override;

	// --- 음량 ---
	UFUNCTION(BlueprintPure, Category = "CX|Settings|Audio") int32 GetMasterVolume() const { return MasterVolume; }
	UFUNCTION(BlueprintPure, Category = "CX|Settings|Audio") int32 GetMusicVolume() const { return MusicVolume; }
	UFUNCTION(BlueprintPure, Category = "CX|Settings|Audio") int32 GetSfxVolume() const { return SfxVolume; }
	/** 0~100으로 잘라 10단위로 스냅. 즉시 음량에 적용하려면 UCXAudioSubsystem::ApplyUserVolumes. */
	UFUNCTION(BlueprintCallable, Category = "CX|Settings|Audio") void SetMasterVolume(int32 Value) { MasterVolume = SnapVolume(Value); }
	UFUNCTION(BlueprintCallable, Category = "CX|Settings|Audio") void SetMusicVolume(int32 Value) { MusicVolume = SnapVolume(Value); }
	UFUNCTION(BlueprintCallable, Category = "CX|Settings|Audio") void SetSfxVolume(int32 Value) { SfxVolume = SnapVolume(Value); }
	void ResetAudioToDefaults();

	// --- 화면 분할 ---
	UFUNCTION(BlueprintPure, Category = "CX|Settings|Split") ECXSplitLayout GetSplitLayout() const { return SplitLayout; }
	UFUNCTION(BlueprintCallable, Category = "CX|Settings|Split") void SetSplitLayout(ECXSplitLayout Layout) { SplitLayout = Layout; }

	// --- 조작 장치 ---
	UFUNCTION(BlueprintPure, Category = "CX|Settings|Devices")
	ECXInputDeviceType GetSlotDevice(int32 PlayerIndex) const;

	/** 둘 다 키보드가 되는 조합이면 바꾸지 않고 false(설정 메뉴 4.4 — 자동 스왑 없음). */
	UFUNCTION(BlueprintCallable, Category = "CX|Settings|Devices")
	bool SetSlotDevice(int32 PlayerIndex, ECXInputDeviceType Device);

	/** 이 조합에 필요한 게임패드 수(0~2). */
	UFUNCTION(BlueprintPure, Category = "CX|Settings|Devices")
	int32 GetRequiredGamepadCount() const;

	/** 오빠 게임패드 / 여동생 키보드 — 키보드는 항상 LocalPlayer 0이라 LocalPlayer 0이 여동생 폰을 맡는다(8장). */
	UFUNCTION(BlueprintPure, Category = "CX|Settings|Devices")
	bool IsCharacterSwapLayout() const;

	/** 슬롯(0 오빠 / 1 여동생)을 조작하는 로컬 플레이어 인덱스. */
	int32 GetLocalPlayerIndexForSlot(int32 PlayerIndex) const;

	static bool IsValidSlot(int32 PlayerIndex) { return PlayerIndex == 0 || PlayerIndex == 1; }

protected:
	/** 전체 음량(0~100). */
	UPROPERTY(Config)
	int32 MasterVolume = DefaultMasterVolume;

	/** 배경음 음량(0~100). 실제 음량 = 배경음 × 전체. */
	UPROPERTY(Config)
	int32 MusicVolume = DefaultMusicVolume;

	/** 효과음 음량(0~100). UI/환경음 포함. */
	UPROPERTY(Config)
	int32 SfxVolume = DefaultSfxVolume;

	UPROPERTY(Config)
	ECXSplitLayout SplitLayout = ECXSplitLayout::TopBottom;

	UPROPERTY(Config)
	ECXInputDeviceType BrotherDevice = ECXInputDeviceType::KeyboardMouse;

	UPROPERTY(Config)
	ECXInputDeviceType SisterDevice = ECXInputDeviceType::Gamepad;

private:
	static int32 SnapVolume(int32 Value);
	void ResetCXSettings();
	/** 깨진 값(범위 밖, 둘 다 키보드)을 고친다. */
	void SanitizeCXSettings();
};
