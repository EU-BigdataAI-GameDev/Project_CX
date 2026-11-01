// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "CXGameInstance.generated.h"

class UCXSaveGame;

/** 플레이어 슬롯(P1 오빠 / P2 여동생)이 쓰는 조작 장치 종류(설정 메뉴 기획 4.4 `조작 장치`). */
UENUM(BlueprintType)
enum class ECXInputDeviceType : uint8
{
	KeyboardMouse	UMETA(DisplayName = "Keyboard & Mouse"),
	Gamepad			UMETA(DisplayName = "Gamepad")
};

/**
 *  프로젝트 게임 인스턴스 — 맵 이동(타이틀 → 인게임) 사이에 유지해야 하는 값을 들고 있다.
 *
 *  - 슬롯별 조작 장치 설정(설정 메뉴가 생기기 전까지 기본값: 오빠 키보드/마우스, 여동생 게임패드)
 *  - 시작화면 2P 장치 확인 결과(어느 게임패드가 어느 슬롯인지) → 인게임 ALocalCoopGameMode가 장치 매핑에 사용할 예정
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

	/** 설정 메뉴(시작화면에서만 변경 가능)용. 바뀐 슬롯의 게임패드 확인은 초기화된다. */
	UFUNCTION(BlueprintCallable, Category = "CX|Players")
	void SetSlotDeviceType(int32 PlayerIndex, ECXInputDeviceType DeviceType);

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

protected:
	/** 슬롯별 조작 장치 기본값 [P1, P2]. */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Players")
	TArray<ECXInputDeviceType> SlotDeviceTypes;

private:
	FInputDeviceId SlotGamepads[NumPlayerSlots];

	static bool IsValidSlot(int32 PlayerIndex) { return PlayerIndex >= 0 && PlayerIndex < NumPlayerSlots; }
};
