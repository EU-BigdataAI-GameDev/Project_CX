// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CXSoloSubsystem.generated.h"

class APawn;
class UGameInstance;

/**
 *  숨김 솔로 모드 — 게임패드 없이 한 사람이 키보드 하나로 2인 협동을 억지로 플레이한다(Docs/Guide/솔로_모드_가이드.md).
 *
 *  - 콘솔 CXSolo [0|1|off](ALocalCoopGameMode). 0 = 오빠 조작(기본), 1 = 여동생 조작, off = 일반 2인 모드로 복귀.
 *  - 분할 화면·로컬 플레이어·빙의는 그대로. 키보드/마우스 이벤트만 UCXGameViewportClient::RemapControllerInput이
 *    "조작 중인 캐릭터의 로컬 플레이어"로 돌린다. 설정(GameUserSettings)은 건드리지 않는다 — 장치 종류는
 *    UCXGameInstance::GetSlotDeviceType이 솔로 중에만 덮어쓴다(조작 슬롯 = 키보드, 나머지 = 게임패드(AI)).
 *  - 조작하지 않는 캐릭터는 UCXSoloFollowerComponent(AI): 따라다니기 + (여동생) 자동 해킹 / (오빠) 자동 사격.
 *  - Tab(FCXMenuInputProcessor)으로 조작 캐릭터 전환. 조작 캐릭터가 쓰러지고 다른 쪽이 살아 있으면 자동 전환.
 *  - 상태는 정적 변수(프로세스 동안) — 다른 맵으로 이동(ExecuteTravel)하면 새 맵에서 이어받고, 시작화면(비협동 맵)에서는 꺼진다.
 *  - 서버(권위) 월드에서만 AI가 동작한다. 일반 2인 모드는 이 서브시스템이 아무것도 하지 않는다.
 */
UCLASS()
class UCXSoloSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UCXSoloSubsystem* Get(const UObject* WorldContextObject);
	static UCXSoloSubsystem* GetForGameInstance(const UGameInstance* GameInstance);

	// ---- 전역 질의(입력 리매핑·HUD·메뉴 — 월드 없이)
	static bool IsActive() { return bActive; }
	/** 조작 슬롯: 0 = 오빠, 1 = 여동생. */
	static int32 GetControlledSlot() { return ControlledSlot; }
	/** 솔로 중이고 이 슬롯(0 오빠 / 1 여동생)이 AI인지. */
	static bool IsAISlot(int32 Slot) { return bActive && Slot != ControlledSlot; }
	/** 맵 이동 직전 호출 — 켜져 있으면 새 맵의 월드 시작에서 같은 설정으로 다시 켠다. */
	static void NotifyTravel();

	// UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

	/** 솔로 켜기. Slot 0 = 오빠, 1 = 여동생. 로컬 협동 게임모드(서버 권위)일 때만. 켰으면 true. */
	bool Enable(int32 Slot);
	/** 솔로 끄기 — AI 컴포넌트 제거, 일반 2인 모드로(설정은 원래 값 그대로). */
	void Disable();
	/** 조작 캐릭터 전환(Tab / CXSoloSwap). 막힌 상태(채팅·해킹·메뉴·사망 등)면 false. */
	bool TrySwitchControl(bool bForce = false);

	FString GetDebugState() const;

private:
	void ApplyControl(int32 NewSlot);
	/** 모든 로컬 플레이어의 눌림 키를 비운다(조작 대상이 바뀔 때 키가 끼지 않게). */
	void FlushAllPressedKeys() const;
	bool IsSwitchBlocked(FString& OutReason) const;
	void FindRolePawns(APawn*& OutBrother, APawn*& OutSister) const;
	void RemoveFollowers();

	static bool bActive;
	static int32 ControlledSlot;
	static bool bPendingRestore;
	static int32 PendingSlot;

	bool bWorldBegun = false;
};
