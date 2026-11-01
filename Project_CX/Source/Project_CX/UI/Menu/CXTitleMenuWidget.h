// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CXMenuWidgetBase.h"
#include "CXTitleMenuWidget.generated.h"

class UTextBlock;
class UImage;
class UOverlay;
class UCanvasPanel;
class UVerticalBox;
class UCXPlayerSlotWidget;
class UCXTitleBackdropWidget;
class UCXSaveGame;

/**
 *  게임 시작화면(.claude/Plans/게임 시작화면 기획.md).
 *
 *  - 메뉴: CONTINUE(세이브 있을 때만) / NEW GAME / SETTINGS / QUIT GAME, 기본 포커스 = 세이브 있으면 CONTINUE, 없으면 NEW GAME
 *  - 2P 장치 확인: 키보드 슬롯은 처음부터 READY, 게임패드 슬롯은 아무 패드의 Start로 READY(먼저 누른 패드가 앞 슬롯).
 *    준비 안 된 상태로 시작하려 하면 슬롯 펄스 + "CONNECT P2 DEVICE AND PRESS START"
 *  - NEW GAME: 세이브가 있으면 덮어쓰기 확인창 → 0.5초 검정 페이드 → Map1_SoulCity
 *  - QUIT GAME: 확인창 → 앱 종료. 최상위 ESC/B는 아무 동작 없음.
 *  - 진입 연출(첫 실행만): 배경 페이드인 → 게임명 네온 점등 깜박임 → 메뉴 슬라이드인. 아무 입력이나 누르면 즉시 완료.
 *    타이틀로 돌아온 경우는 0.3초 페이드로 축약.
 */
UCLASS()
class UCXTitleMenuWidget : public UCXMenuWidgetBase
{
	GENERATED_BODY()

public:
	virtual bool HandleAnyInput() override;
	virtual FString GetDebugState() const override;

protected:
	virtual void BuildMenu() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual void OnItemActivated(FName ItemId) override;
	virtual bool OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex) override;
	virtual bool CanAcceptInput() const override;

	/** 새 게임이 여는 첫 맵. */
	UPROPERTY(EditDefaultsOnly, Category = "Title")
	FString NewGameMap = TEXT("/Game/Project_CX/Maps/Map1_SoulCity");

	/** 게임 시작 검정 페이드 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Title")
	float StartFadeTime = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Title")
	FString VersionLabel = TEXT("v0.1");

	/** 게임명 점등 시 네온 지직음(선택). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Sound")
	TObjectPtr<USoundBase> SoundNeonBuzz;

private:
	void RefreshSaveState();
	void RefreshSlots();
	bool TryStartGame(bool bContinue);
	void BeginStartGame(const FString& MapPath);
	void ShowDevicePrompt();
	void HandleGamepadStart(FInputDeviceId DeviceId);
	void HandleDeviceConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId UserId, FInputDeviceId DeviceId);
	void UpdateIntro(float DeltaTime);
	void FinishIntro();

	UPROPERTY(Transient) TObjectPtr<UCXTitleBackdropWidget> Backdrop;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Content;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UImage> TitleUnderline;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SubtitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SaveWarningText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DevicePromptText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TObjectPtr<UOverlay> ModalLayer;
	UPROPERTY(Transient) TObjectPtr<UImage> FadeImage;
	UPROPERTY(Transient) TArray<TObjectPtr<UCXPlayerSlotWidget>> PlayerSlots;
	UPROPERTY(Transient) TObjectPtr<UCXSaveGame> LoadedSave;

	bool bSaveExists = false;
	bool bGamepadAttached = false;
	float GamepadPollTimer = 0.0f;
	float DevicePromptRemaining = 0.0f;

	/** 진입 연출 진행 시간. < 0 이면 연출 끝. */
	float IntroTime = -1.0f;
	bool bShortIntro = false;
	bool bBuzzPlayed = false;

	/** 게임 시작 페이드 중(입력 전부 무시). */
	bool bStarting = false;
	float StartFadeElapsed = 0.0f;
	FString PendingMap;

	FDelegateHandle DeviceConnectionHandle;
};
