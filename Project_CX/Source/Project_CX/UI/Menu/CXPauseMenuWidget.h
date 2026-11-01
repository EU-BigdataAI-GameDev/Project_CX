// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CXMenuWidgetBase.h"
#include "CXPauseMenuWidget.generated.h"

class UTextBlock;
class UImage;
class UOverlay;
class USizeBox;

/**
 *  일시정지 메뉴 배경: 정지된 분할 화면 위 검정 55% 딤 + 약한 스캔라인(설정 메뉴 기획 0.5).
 *  마우스 입력이 게임으로 새지 않게 Visible.
 */
UCLASS()
class UCXPauseDimWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};

/**
 *  인게임 ESC(일시정지) 메뉴(.claude/Plans/ESC 메뉴 기획.md).
 *
 *  - 열기/닫기/월드 일시정지는 UCXMenuSubsystem(OpenPauseMenu / FinishClosePauseMenu)이 담당하고, 이 위젯은 화면과 항목 동작만 맡는다.
 *  - 항목: RESUME(기본 포커스) / SETTINGS(자리표시 알림창) / QUIT TO TITLE(확인창 → 0.4초 검정 페이드 → 시작화면).
 *  - 최상위 ESC/B, 게임패드 Start = RESUME과 동일(닫기). 닫을 때 0.15초 페이드아웃 후 재개.
 *  - 게임패드 연결 끊김: 상단 경고 + RESUME 숨김, 재연결되면 RECONNECTED + RESUME 복귀(6장).
 *  - 하단: 현재 구역명(ACoopHUD 구역명), 두 플레이어 장치/연결 태그, 조작 힌트.
 */
UCLASS()
class UCXPauseMenuWidget : public UCXMenuWidgetBase
{
	GENERATED_BODY()

public:
	/** 닫기 요청(페이드아웃 시작). 연결 끊김 경고 중이거나 이미 닫는 중이면 무시. */
	void RequestResume();

	/** 서브시스템의 연결 끊김/재연결 상태를 화면에 반영. */
	void RefreshConnectionState();

	bool IsClosing() const { return bClosing; }
	bool IsQuitting() const { return bQuitting; }

	virtual FString GetDebugState() const override;

protected:
	virtual void BuildMenu() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual void OnItemActivated(FName ItemId) override;
	virtual bool OnBack() override;
	virtual bool OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex) override;
	virtual bool CanAcceptInput() const override;
	virtual bool ConsumesTopLevelBack() const override { return true; }

	/** 타이틀로 이동할 시작화면 맵. */
	UPROPERTY(EditDefaultsOnly, Category = "Pause")
	FString TitleMap = TEXT("/Game/Project_CX/Maps/Map_Title");

	/** 타이틀로 이동 전 검정 페이드 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Pause")
	float QuitFadeTime = 0.4f;

private:
	void RefreshPlayerTags();
	void BeginQuitToTitle();
	FString ResolveZoneName() const;
	bool IsPlayerConnected(int32 PlayerIndex) const;

	UPROPERTY(Transient) TObjectPtr<UOverlay> RootOverlay;
	UPROPERTY(Transient) TObjectPtr<USizeBox> PanelBox;
	UPROPERTY(Transient) TObjectPtr<UImage> BlinkSquare;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WarningText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InfoText;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TagDeviceTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TagStatusTexts;
	UPROPERTY(Transient) TObjectPtr<UOverlay> ModalLayer;
	UPROPERTY(Transient) TObjectPtr<UImage> FadeImage;

	/** 열기 페이드 진행(0 → 1). */
	float OpenAlpha = 0.0f;
	/** 닫기 페이드 중. */
	bool bClosing = false;
	float CloseElapsed = 0.0f;
	/** 타이틀로 이동 페이드 중(입력 전부 무시). */
	bool bQuitting = false;
	float QuitElapsed = 0.0f;

	/** 연결 끊김 경고 표시 중(RESUME 숨김). */
	bool bShowingDisconnect = false;
	float Clock = 0.0f;
	float TagPollTimer = 0.0f;
};
