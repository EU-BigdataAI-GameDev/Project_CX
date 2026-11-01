// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CXMenuStyle.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "CXMenuSubsystem.generated.h"

class UCXMenuWidgetBase;
class UCXPauseMenuWidget;
class FCXMenuInputProcessor;
class APlayerController;
class USoundMix;

/**
 *  전체 화면 메뉴 생성/구동(게임 시작화면 기획 11장).
 *
 *  - PushMenu/PopMenu로 메뉴 위젯 스택을 관리하고, 메뉴가 하나라도 떠 있는 동안 Slate 입력 프리프로세서를 등록한다.
 *  - 프리프로세서가 모은 입력(키보드/모든 게임패드)을 맨 위 메뉴로 보낸다. 입력 장치 → 플레이어 판정은 UCXGameInstance 기준.
 *  - 같은 프레임에 두 플레이어 입력이 오면 먼저 온 것만 처리(0.1).
 *  - 맵 이동(PreLoadMap) 시 스택을 비우고 프리프로세서를 해제한다.
 *
 *  시작화면이 첫 사용처이고, ESC 메뉴/설정/게임 오버화면도 같은 경로로 띄운다.
 *
 *  ESC(일시정지) 메뉴(ESC 메뉴 기획):
 *  - 프리프로세서는 메뉴가 없어도 항상 등록해 두고, 인게임 월드에서 ESC/게임패드 Start를 보면 OpenPauseMenu.
 *    (Slate 단계라 월드 일시정지 중에도, PlayerController 클래스와 상관없이 동작)
 *  - 열면 월드 전체 일시정지(SetGamePaused), 닫으면 재개 + 0.15초 게임플레이 입력 무시.
 *  - 게임패드 연결 끊김 → 자동으로 열고 경고, 재연결되면 RESUME 복귀.
 *  - 열 수 없음: 시작화면, 맵 로딩/전환 중, 다른 메뉴가 떠 있을 때, bMenuBlocked(게임 오버 등 — 이후 시스템용 훅).
 */
UCLASS(Config = Game)
class UCXMenuSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UCXMenuSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 메뉴를 만들어 뷰포트에 올리고 UI 전용 입력 모드로 바꾼다. */
	UCXMenuWidgetBase* PushMenu(TSubclassOf<UCXMenuWidgetBase> MenuClass, APlayerController* OwningPlayer, int32 ZOrder = 100);

	/** 메뉴를 내린다. 스택이 비면 게임 입력 모드로 돌린다. */
	void PopMenu(UCXMenuWidgetBase* Menu);

	UCXMenuWidgetBase* GetTopMenu() const;
	bool HasActiveMenu() const { return GetTopMenu() != nullptr; }

	// --- 입력 프리프로세서가 호출 ---
	bool RouteMenuInput(const FCXMenuInput& Input);
	bool RouteAnyInput();
	/** bMouse = 마우스 움직임/클릭이면 커서 표시, 키/패드면 숨김. */
	void NotifyInputDeviceUsed(bool bMouse);
	/** 게임 뷰포트(PIE 창 포함)에 포커스가 있을 때만 입력을 가로챈다. */
	bool IsGameViewportFocused() const;

	/**
	 *  테스트/자동화용: 장치 입력을 흉내 내 메뉴로 보낸다(포커스 검사 생략).
	 *  Action: 1 Up, 2 Down, 3 Left, 4 Right, 5 Accept, 6 Back, 7 Start, 8 TabLeft(Q/LB), 9 TabRight(E/RB). bGamepad면 DeviceId의 게임패드 입력.
	 *  메뉴가 없을 때 Back(ESC)/Start는 일시정지 메뉴 열기 경로(HandlePauseKey)로 간다.
	 */
	UFUNCTION(BlueprintCallable, Category = "CX|Menu|Debug")
	bool SimulateMenuInput(int32 Action, bool bGamepad, int32 DeviceId = 0);

	UFUNCTION(BlueprintCallable, Category = "CX|Menu|Debug")
	FString GetTopMenuDebugState() const;

	// --- ESC(일시정지) 메뉴 ---

	/** 일시정지 메뉴를 열고 월드를 정지. 열 수 없는 상태면 false. Requester는 정보용(비어도 됨). */
	UFUNCTION(BlueprintCallable, Category = "CX|Menu|Pause")
	bool OpenPauseMenu(APlayerController* Requester = nullptr);

	/** 지금 일시정지 메뉴를 열 수 있는지(인게임 월드, 로딩 중 아님, 다른 메뉴 없음, 차단 플래그 꺼짐). */
	UFUNCTION(BlueprintPure, Category = "CX|Menu|Pause")
	bool CanOpenPauseMenu() const;

	UFUNCTION(BlueprintPure, Category = "CX|Menu|Pause")
	bool IsPauseMenuOpen() const { return PauseMenu != nullptr; }

	/** 게임 오버 연출/사망 판정 직후 등 ESC 메뉴를 막아야 하는 상태(해당 시스템이 생기면 여기서 켠다). */
	UFUNCTION(BlueprintCallable, Category = "CX|Menu|Pause")
	void SetMenuBlocked(bool bBlocked) { bMenuBlocked = bBlocked; }

	UFUNCTION(BlueprintPure, Category = "CX|Menu|Pause")
	bool IsMenuBlocked() const { return bMenuBlocked; }

	/** 일시정지 메뉴 위젯이 닫기 페이드를 끝내면 호출 → 메뉴 내림 + 재개 + 0.15초 게임플레이 입력 무시. */
	void FinishClosePauseMenu(UCXPauseMenuWidget* Menu);

	/** 타이틀로 이동 시작(페이드 중 다시 열리지 않게). */
	void NotifyLeavingMap() { bLeavingMap = true; }

	/** 연결이 끊긴 플레이어 슬롯(0/1). 없으면 INDEX_NONE. */
	int32 GetDisconnectedPlayer() const { return DisconnectedPlayer; }
	/** 이번에 열린 메뉴에서 끊겼던 패드가 다시 연결됐는지(RECONNECTED 표시). */
	bool WasReconnected() const { return bReconnected; }

	/** 재개 직후 게임플레이 입력 무시 중(A/Enter가 점프·사격으로 새지 않게). */
	bool IsGameplayInputBlocked() const;

	/** 프리프로세서: 메뉴가 없을 때 ESC/Start. 인게임 월드면 열기 시도 후 소비(true), 시작화면 등이면 false(흘려보냄). */
	bool HandlePauseKey(bool bFromGamepad, FInputDeviceId DeviceId);

	/** 테스트용: 게임패드 연결 끊김/재연결 흉내(실제 패드를 뽑지 않고 6장 흐름 확인). */
	UFUNCTION(BlueprintCallable, Category = "CX|Menu|Debug")
	void DebugSimulateGamepadConnection(int32 PlayerIndex, bool bConnected);

private:
	bool DispatchInput(const FCXMenuInput& Input);
	void UpdateInputProcessor();
	void HandlePreLoadMap(const FString& MapName);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void HandleDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId UserId, FInputDeviceId DeviceId);
	void SetPlayerConnection(int32 PlayerIndex, bool bConnected);
	bool IsPauseableWorld() const;
	void ResetPauseState();
	void SetPauseSoundMix(bool bPush);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCXMenuWidgetBase>> MenuStack;

	TSharedPtr<FCXMenuInputProcessor> InputProcessor;
	bool bProcessorRegistered = false;

	uint64 LastInputFrame = 0;
	int32 LastInputPlayer = INDEX_NONE;
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle DeviceConnectionHandle;

	/** 일시정지 메뉴 위젯 클래스. */
	UPROPERTY(Transient)
	TSubclassOf<UCXPauseMenuWidget> PauseMenuClass;

	UPROPERTY(Transient)
	TObjectPtr<UCXPauseMenuWidget> PauseMenu;

	/** 일시정지 중 BGM 로우패스 + -6dB(SM_PauseDuck). 에셋이 생기면 DefaultGame.ini에서 지정 — 비어 있으면 무시. */
	UPROPERTY(Config)
	TSoftObjectPtr<USoundMix> PauseSoundMix;

	bool bSoundMixPushed = false;
	bool bDeinitializing = false;
	bool bMenuBlocked = false;
	bool bMapLoading = false;
	bool bLeavingMap = false;
	int32 DisconnectedPlayer = INDEX_NONE;
	bool bReconnected = false;
	/** FPlatformTime::Seconds 기준 게임플레이 입력 무시 종료 시각. */
	double GameplayInputBlockEnd = 0.0;
};
