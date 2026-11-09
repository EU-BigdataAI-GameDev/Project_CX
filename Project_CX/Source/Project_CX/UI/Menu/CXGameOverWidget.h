// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CXMenuWidgetBase.h"
#include "CXGameOverWidget.generated.h"

class UTextBlock;
class UImage;
class UOverlay;
class USizeBox;
class UVerticalBox;
class UCXPauseDimWidget;

/**
 *  신호 끊김 노이즈: 굵은 스캔라인 + 무작위 가로 노이즈 띠(게임 오버 진입 연출 — 기획 4.1/4.2).
 *  Intensity 0~1(띠 개수/진하기), 스캔라인은 위젯 투명도만 따른다.
 */
UCLASS()
class UCXSignalNoiseWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetIntensity(float InIntensity) { Intensity = FMath::Clamp(InIntensity, 0.0f, 1.0f); }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float Intensity = 0.0f;
};

/**
 *  게임 오버화면(.claude/Plans/게임 오버화면 기획.md 4장).
 *
 *  - ALocalCoopGameMode::TriggerGameOver가 1P 컨트롤러 소유로 화면 전체(ZOrder 200)에 띄운다.
 *    판정·메뉴 차단·슬로우·월드 정지·분할 끔은 게임모드 담당, 이 위젯은 화면과 항목 동작만.
 *  - 진입 연출(실시간): 슬로우 0.5초 동안 두 칸 위로 노이즈 → 전체 화면 전환 + 딤 70%(0.5초) → 패널 페이드
 *    → 제목 글리치 2~3회 후 고정(0.4초) → 메뉴 0.2초 뒤 페이드인. 연출 중(약 1.2초) 입력 무시.
 *  - ALL SIGNALS LOST + 마지막으로 쓰러진 플레이어(그 플레이어 색, 동시 사망이면 두 이름) + RETRY / QUIT TO TITLE
 *    + 재시작 지점 안내. 주황(경고) 테두리/구분선.
 *  - RETRY(기본 포커스, 확인창 없음): 현재 맵을 다시 연다. 세이브(CX_Slot0)가 이 맵 것이면 그 세이브포인트에서,
 *    아니면 맵 시작 지점에서(세이브포인트·맵 이동 기획 4장). 안내 문구도 그에 맞춰 LAST SAVE POINT / MAP START.
 *  - QUIT TO TITLE(확인창 없음 — 기획 4.4): UCXPauseMenuWidget::BeginQuitToTitle과 같은 흐름.
 *  - ESC/B/Start는 아무 동작 없음(입력은 소비). 두 플레이어 누구나 선택, 포커스 색 = 마지막 입력 플레이어.
 */
UCLASS()
class UCXGameOverWidget : public UCXMenuWidgetBase
{
	GENERATED_BODY()

public:
	/** 마지막으로 쓰러진 플레이어(둘 다 true = 동시 사망). */
	void SetFallen(bool bBrother, bool bSister);

	bool IsLeaving() const { return bLeaving; }

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
	UPROPERTY(EditDefaultsOnly, Category = "GameOver")
	FString TitleMap = TEXT("/Game/Project_CX/Maps/Map_Title");

	/** 슬로우 구간(실시간) — 게임모드 GameOverSlowMoTime과 같은 값. 이 동안 노이즈만. */
	UPROPERTY(EditDefaultsOnly, Category = "GameOver")
	float SlowMoTime = 0.5f;

	/** 전체 화면 전환 + 딤이 깔리는 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "GameOver")
	float TransitionTime = 0.5f;

	/** 재시작/타이틀로 이동 전 검정 페이드 시간(음악 페이드아웃도 같은 시간). */
	UPROPERTY(EditDefaultsOnly, Category = "GameOver")
	float LeaveFadeTime = 0.4f;

private:
	void BeginLeave(bool bToTitle);
	float GetPanelStartTime() const { return SlowMoTime + TransitionTime; }
	float GetIntroTime() const;

	UPROPERTY(Transient) TObjectPtr<UOverlay> RootOverlay;
	UPROPERTY(Transient) TObjectPtr<UCXPauseDimWidget> Dim;
	UPROPERTY(Transient) TObjectPtr<UCXSignalNoiseWidget> Noise;
	UPROPERTY(Transient) TObjectPtr<USizeBox> PanelBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FallenNameA;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FallenAmp;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FallenNameB;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FallenSuffix;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> MenuBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> InfoText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WarningText;
	UPROPERTY(Transient) TObjectPtr<UImage> FadeImage;

	bool bFallenBrother = false;
	bool bFallenSister = false;

	/** 진입 후 경과(실시간 — 슬로우/월드 정지와 무관). */
	float Elapsed = 0.0f;
	bool bLeaving = false;
	float LeaveElapsed = 0.0f;
	FString LeaveTarget;
};
