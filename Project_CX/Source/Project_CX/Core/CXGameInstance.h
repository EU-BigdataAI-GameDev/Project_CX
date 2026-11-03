// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "CXGameUserSettings.h"
#include "CXGameInstance.generated.h"

class UCXSaveGame;

/**
 *  프로젝트 게임 인스턴스 — 맵 이동(타이틀 → 인게임) 사이에 유지해야 하는 값을 들고 있다.
 *
 *  - 슬롯별 조작 장치 종류: UCXGameUserSettings(GameUserSettings.ini)가 유일한 기준 — 여기서는 읽고/쓰기만 중계
 *  - 시작화면 2P 장치 확인 결과(어느 게임패드가 어느 슬롯인지) → 둘 다 게임패드일 때 UCXGameViewportClient가 패드→플레이어 매핑에 사용
 *  - 시작화면 진입 연출을 이미 봤는지(첫 실행만 풀 연출)
 *  - 이어하기로 시작할 때 읽어 둔 세이브
 */
UCLASS()
class UCXGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	static constexpr int32 NumPlayerSlots = 2;

	UCXGameInstance();

	static UCXGameInstance* Get(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "CX|Players")
	ECXInputDeviceType GetSlotDeviceType(int32 PlayerIndex) const;

	/**
	 *  설정 메뉴(시작화면에서만 변경 가능)용. UCXGameUserSettings에 즉시 저장하고, 바뀐 슬롯의 게임패드 확인은 초기화된다.
	 *  둘 다 키보드가 되는 조합이면 바꾸지 않고 false.
	 */
	UFUNCTION(BlueprintCallable, Category = "CX|Players")
	bool SetSlotDeviceType(int32 PlayerIndex, ECXInputDeviceType DeviceType);

	/** 현재 연결된 게임패드 수(키보드/마우스 기본 장치 제외). 패드가 하나도 없으면 0. */
	static int32 GetConnectedGamepadCount();

	/** 키보드 슬롯은 항상, 게임패드 슬롯은 Start로 패드가 배정된 뒤 준비 완료. */
	UFUNCTION(BlueprintPure, Category = "CX|Players")
	bool IsSlotReady(int32 PlayerIndex) const;

	UFUNCTION(BlueprintPure, Category = "CX|Players")
	bool AreAllSlotsReady() const;

	FInputDeviceId GetSlotGamepad(int32 PlayerIndex) const;
	void AssignSlotGamepad(int32 PlayerIndex, FInputDeviceId DeviceId);
	void ClearSlotGamepad(int32 PlayerIndex);
	void ClearAllSlotGamepads();

	/** 이 게임패드가 배정된 슬롯. 없으면 INDEX_NONE. */
	int32 FindSlotByGamepad(FInputDeviceId DeviceId) const;

	/** 아직 패드가 배정되지 않은 첫 게임패드 슬롯(먼저 Start를 누른 패드가 P1). 없으면 INDEX_NONE. */
	int32 FindFirstUnassignedGamepadSlot() const;

	/** 메뉴 입력이 어느 플레이어 것인지(포커스 하이라이트 색). 키보드/마우스 → 키보드 슬롯, 패드 → 배정 슬롯/빈 패드 슬롯. */
	int32 ResolvePlayerForInput(bool bFromGamepad, FInputDeviceId DeviceId) const;

	/** 시작화면 첫 진입 연출을 이미 재생했는지. 타이틀로 돌아오면 짧은 페이드만. */
	UPROPERTY(BlueprintReadWrite, Category = "CX|Title")
	bool bTitleIntroPlayed = false;

	/** 이어하기로 시작했다면 그때 읽은 세이브(세이브포인트 스폰에 사용 예정). 새 게임이면 null. */
	UPROPERTY(BlueprintReadOnly, Category = "CX|Save")
	TObjectPtr<UCXSaveGame> PendingContinueSave;

	/** 설정 메뉴 마지막 탭(같은 세션에서 다시 열면 유지 — 설정 메뉴 기획 3장). */
	int32 SettingsLastTab = 0;

private:
	FInputDeviceId SlotGamepads[NumPlayerSlots];

	static bool IsValidSlot(int32 PlayerIndex) { return PlayerIndex >= 0 && PlayerIndex < NumPlayerSlots; }
};
