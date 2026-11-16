// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CXMenuWidgetBase.h"
#include "CXTitleMenuWidget.generated.h"

class UTextBlock;
class UWidget;
class UImage;
class UOverlay;
class UCanvasPanel;
class UVerticalBox;
class USizeBox;
class UTexture2D;
class UCXPlayerSlotWidget;
class UCXTitleBackdropWidget;
class UCXSaveGame;

/**
 *  게임 시작화면(.claude/Plans/게임 시작화면 기획.md).
 *
 *  - 메뉴: CONTINUE(세이브 있을 때만) / NEW GAME / SETTINGS / QUIT GAME, 기본 포커스 = 세이브 있으면 CONTINUE, 없으면 NEW GAME
 *  - 2P 장치 확인: 키보드 슬롯은 처음부터 READY, 게임패드 슬롯은 아무 패드의 Start로 READY(먼저 누른 패드가 앞 슬롯).
 *    준비 안 된 상태로 시작하려 하면 슬롯 펄스 + "CONNECT P2 DEVICE AND PRESS START"
 *  - NEW GAME: 세이브가 있으면 덮어쓰기 확인창 → 0.5초 검정 페이드 → Map1_SoulCity1(리디렉터 Map1_SoulCity 대신 실제 맵)
 *  - SETTINGS: 설정 메뉴(조작 장치 변경 가능). 닫으면 슬롯이 바뀐 조작 장치를 따르고, 패드가 부족하면 경고.
 *  - QUIT GAME: 확인창 → 앱 종료. 최상위 ESC/B는 아무 동작 없음.
 *  - 진입 연출(첫 실행만): 배경 페이드인 → 로고 페이드인 + 살짝 떠오름(글로우 점등 깜박임) → 메뉴 슬라이드인. 아무 입력이나 누르면 즉시 완료.
 *    타이틀로 돌아온 경우는 0.3초 페이드로 축약.
 *  - 화면 구성: 키 아트 배경(UCXTitleBackdropWidget) + 왼쪽 위 로고 이미지(Title_logo, 화면 폭 비율로 크기 결정, 네온 숨쉬기)
 *    + 로고 아래 왼쪽 정렬 메뉴(포커스 강조색 = 네온 마젠타). 로고/배경 텍스처가 없으면 예전 글자 제목·절차 생성 배경으로 돌아간다.
 */
UCLASS()
class UCXTitleMenuWidget : public UCXMenuWidgetBase
{
	GENERATED_BODY()

public:
	UCXTitleMenuWidget(const FObjectInitializer& ObjectInitializer);

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
	FString NewGameMap = TEXT("/Game/Project_CX/Maps/Map1_SoulCity1");

	/** 게임 시작 검정 페이드 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Title")
	float StartFadeTime = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Title")
	FString VersionLabel = TEXT("v0.1");

	/** 로고 이미지(없으면 예전 글자 제목). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	TObjectPtr<UTexture2D> LogoTexture;

	/** 로고 폭 = 화면 폭 × 이 비율(비율 유지). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoWidthFraction = 0.42f;

	/** 로고 높이 상한 = 화면 높이 × 이 비율(세로가 좁은 화면에서 메뉴를 밀어내지 않게). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoMaxHeightFraction = 0.34f;

	/** 로고+메뉴 묶음의 왼쪽 위 위치(1920x1080 기준 좌표). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	FVector2D LeftColumnPosition = FVector2D(96.0, 120.0);

	/** 로고 PNG의 투명 여백 보정: 글자가 시작되는 왼쪽 여백(폭 비율) / 아래 여백(높이 비율). 메뉴·부제를 글자 왼쪽 끝에 맞춘다. */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoInsetLeft = 0.04f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoInsetBottom = 0.09f;

	/** 로고와 메뉴 사이 간격. */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoMenuGap = 30.0f;

	/** 진입 연출: 로고 페이드인 시간(초)과 아래에서 떠오르는 거리(px). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoFadeTime = 0.8f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoRise = 26.0f;

	/** 네온 숨쉬기: 주기(초), 가장 어두울 때 로고 밝기(불투명도), 겹쳐 그리는 글로우 복사본의 최대 불투명도·색. */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoPulsePeriod = 3.0f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoPulseMinOpacity = 0.93f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	float LogoGlowMaxOpacity = 0.55f;
	UPROPERTY(EditDefaultsOnly, Category = "Title|Logo")
	FLinearColor LogoGlowTint = FLinearColor(1.0f, 0.82f, 0.94f, 1.0f);

	/** 메뉴 포커스 강조색(키 아트의 네온 마젠타). */
	UPROPERTY(EditDefaultsOnly, Category = "Title")
	FLinearColor MenuAccentColor = FLinearColor(1.0f, 0.16f, 0.62f, 1.0f);

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
	void OpenSettings();
	/** 설정 메뉴에서 돌아옴: 슬롯 다시 그림, SETTINGS 포커스 복원, 게임패드 부족 경고. */
	void HandleSettingsClosed();
	void ShowPrompt(const FString& Message);
	void FinishIntro();
	/** 화면 크기에 맞춰 로고 크기와 그 아래 줄들의 들여쓰기를 다시 계산. */
	void UpdateLogoLayout(const FVector2D& ScreenSize);
	/** 로고 네온 숨쉬기(GlowIntro = 진입 연출 중 글로우 점등 배율). */
	void UpdateLogoPulse(float GlowIntro);

	UPROPERTY(Transient) TObjectPtr<UCXTitleBackdropWidget> Backdrop;
	UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Content;
	/** 게임명: 로고 이미지 묶음(LogoBox). 로고 텍스처가 없으면 예전 글자 줄 CAELUM : UNCHAINED. 진입 연출은 이 위젯 전체에. */
	UPROPERTY(Transient) TObjectPtr<UWidget> TitleText;
	UPROPERTY(Transient) TObjectPtr<USizeBox> LogoBox;
	UPROPERTY(Transient) TObjectPtr<UImage> LogoImage;
	UPROPERTY(Transient) TObjectPtr<UImage> LogoGlowImage;
	UPROPERTY(Transient) TObjectPtr<UWidget> SubtitleRow;
	UPROPERTY(Transient) TObjectPtr<UWidget> MenuBox;
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

	/** 키 아트 모양(배경 그림이나 로고가 있음). 둘 다 없으면 예전 모양 그대로. */
	bool bKeyArt = false;
	bool bSettingsOpen = false;
	float LogoTime = 0.0f;
	FVector2D LogoLayoutSize = FVector2D::ZeroVector;

	/** 게임 시작 페이드 중(입력 전부 무시). */
	bool bStarting = false;
	float StartFadeElapsed = 0.0f;
	FString PendingMap;

	FDelegateHandle DeviceConnectionHandle;
};
