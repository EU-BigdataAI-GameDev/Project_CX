// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CXMenuWidgetBase.h"
#include "CXGameClearWidget.generated.h"

class UTextBlock;
class UImage;
class UOverlay;
class USizeBox;
class UCXPauseDimWidget;

/**
 *  클리어화면 패널 테두리: 얇은 네온 라인 + 모서리 브래킷을 왼쪽 절반 청록 / 오른쪽 절반 마젠타로(두 사람의 클리어 — 기획 5장).
 */
UCLASS()
class UCXClearFrameWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};

/**
 *  게임 클리어화면(임시 — .claude/Plans/게임 클리어 화면 기획.md).
 *
 *  - ALocalCoopGameMode::NotifyGameCleared가 1P 컨트롤러 소유로 화면 전체(ZOrder 200)에 띄운다. 세이브 삭제·메뉴 차단·월드 정지는 게임모드 담당.
 *  - 진입 연출: 0.5초 동안 검정 70% 딤 + 스캔라인이 깔림 → 패널 0.15초 페이드 + 세로 0.96→1.0 → 제목 네온 점등(0.4초, 깜박임 2회).
 *    연출 중(약 0.65초)에는 메뉴 입력 무시.
 *  - 중앙 520x300 패널: COMING SOON + 구분선(청록/마젠타) + 버튼 하나 QUIT TO TITLE(기본 포커스, 확인창 없음).
 *  - ESC/B/Start는 아무 동작 없음(입력은 소비), 위/아래 무시. 두 플레이어 누구나 선택, 포커스 색 = 마지막 입력 플레이어.
 *  - QUIT TO TITLE: UCXPauseMenuWidget::BeginQuitToTitle과 같은 흐름 — NotifyLeavingMap, bTitleIntroPlayed, 0.4초 검정 페이드 + 음악 페이드아웃 → Map_Title.
 */
UCLASS()
class UCXGameClearWidget : public UCXMenuWidgetBase
{
	GENERATED_BODY()

public:
	bool IsQuitting() const { return bQuitting; }

	virtual FString GetDebugState() const override;

protected:
	virtual void BuildMenu() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual void OnItemActivated(FName ItemId) override;
	virtual bool OnBack() override { return false; }
	virtual bool OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex) override;
	virtual bool CanAcceptInput() const override;
	virtual bool ConsumesTopLevelBack() const override { return true; }

	/** 타이틀로 이동할 시작화면 맵. */
	UPROPERTY(EditDefaultsOnly, Category = "Clear")
	FString TitleMap = TEXT("/Game/Project_CX/Maps/Map_Title");

	/** 진입 시 딤이 깔리는 시간(이후 패널 페이드인). 게임모드의 월드 정지 지연과 같은 값. */
	UPROPERTY(EditDefaultsOnly, Category = "Clear")
	float DimInTime = 0.5f;

	/** 타이틀로 이동 전 검정 페이드 시간(음악 페이드아웃도 같은 시간). */
	UPROPERTY(EditDefaultsOnly, Category = "Clear")
	float QuitFadeTime = 0.4f;

private:
	void BeginQuitToTitle();

	UPROPERTY(Transient) TObjectPtr<UOverlay> RootOverlay;
	UPROPERTY(Transient) TObjectPtr<UCXPauseDimWidget> Dim;
	UPROPERTY(Transient) TObjectPtr<USizeBox> PanelBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TObjectPtr<UImage> FadeImage;

	/** 진입 후 경과(실시간 — 월드 정지와 무관). */
	float Elapsed = 0.0f;
	bool bQuitting = false;
	float QuitElapsed = 0.0f;
};
