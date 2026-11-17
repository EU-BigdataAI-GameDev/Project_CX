// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CXMenuWidgetBase.h"
#include "Brushes/SlateImageBrush.h"
#include "CXCutsceneWidget.generated.h"

class UImage;
class UTextBlock;
class USizeBox;
class UOverlay;
class UTexture2D;

/** 컷 안의 자막 하나(한/영 한 쌍). 시간은 컷 시작 기준 초. */
struct FCXCutsceneSubtitle
{
	float Start = 0.0f;
	float End = 0.0f;
	FString Ko;
	FString En;
};

/**
 *  오프닝 컷신의 컷 하나. 내용은 전부 UCXCutsceneWidget::GetOpeningCuts()(CXCutsceneWidget.cpp) 한 곳에 있다.
 *  켄 번스: Scale(1.0 = 화면을 딱 덮는 cover 배율, 항상 1.0 이상)과 Pan(-1..1, -1 = 그림 왼쪽/위 가장자리 정렬, +1 = 오른쪽/아래 정렬)을
 *  컷 시작→끝으로 선형 보간한다.
 */
struct FCXCutsceneCut
{
	/** /Game/Project_CX/Cutscene/ 아래 텍스처 이름(소문자 → 대문자 순으로 둘 다 시도). 없으면 검은 배경 + 자막만. */
	FString TextureName;
	float Duration = 8.0f;
	float ScaleStart = 1.15f;
	float ScaleEnd = 1.25f;
	FVector2f PanStart = FVector2f::ZeroVector;
	FVector2f PanEnd = FVector2f::ZeroVector;
	TArray<FCXCutsceneSubtitle> Subtitles;
	/** 컷 시작 후 이 시각에 화면 번쩍(음수 = 없음) + 색(알파 = 최대 세기). */
	float FlashTime = -1.0f;
	FLinearColor FlashColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);
	/** 이 시각부터 게임 로고 페이드인(음수 = 없음). 마지막 컷에서만 쓴다. */
	float LogoTime = -1.0f;
};

/**
 *  켄 번스 일러스트를 그리는 위젯: 화면을 꽉 채우는(cover, 레터박스 없음) 그림 + 아래쪽 가독성 그라디언트 + 추가 딤 + 번쩍임.
 *  CXTitleBackdropWidget::PaintKeyArt와 같은 방식(화면 크기·DPI와 무관하게 로컬 크기 기준).
 */
UCLASS()
class UCXCutsceneArtWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Texture가 nullptr이면 그림 없이 검정(자막만). */
	void SetArt(UTexture2D* InTexture);
	void SetKenBurns(float InScale, const FVector2f& InPan) { Scale = FMath::Max(1.0f, InScale); Pan = InPan; }
	void SetBottomShade(float InAlpha) { BottomShadeAlpha = InAlpha; }
	void SetDim(float InAlpha) { DimAlpha = InAlpha; }
	void SetFlash(const FLinearColor& InColor) { FlashColor = InColor; }

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY(Transient) TObjectPtr<UTexture2D> Texture;
	FSlateBrush ArtBrush;
	float Scale = 1.2f;
	FVector2f Pan = FVector2f::ZeroVector;
	float BottomShadeAlpha = 0.6f;
	float DimAlpha = 0.0f;
	FLinearColor FlashColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
};

/**
 *  오프닝 컷신 재생기(Docs/Guide/오프닝_컷신_가이드.md). 시작화면 위에 올라가는 오버레이 메뉴 위젯(새 맵 없음).
 *
 *  - NEW GAME 확정 직후(세이브 삭제·튜토리얼 초기화 후) UCXTitleMenuWidget이 Play()를 호출하고, 끝나면(또는 건너뛰면) 콜백으로 원래의 첫 맵 열기 흐름을 이어간다.
 *  - 5컷(약 40초): 일러스트 + 켄 번스 + 한글(크게)/영어(작게) 자막 + 컷 사이 페이드 + 마지막 로고. 건너뛰기: Esc / Start / Enter / Space / A(B 포함) — 어느 플레이어의 장치든.
 *  - UCXMenuSubsystem의 맨 위 메뉴로 올라가므로 키 입력은 모두 이 위젯이 받고 아래 시작화면 메뉴로 새지 않는다.
 *  - 안전장치: 텍스처가 전부 없거나 위젯을 못 만들면 Play()가 nullptr → 호출자가 바로 레벨을 연다. 재생 시간이 너무 길어지면(감시 타이머) 강제 종료.
 *  - 콘솔 `CXCutscene [시작컷 1~5]`: 콜백 없이 재생하고, 끝나면 검정에서 페이드아웃해 원래 화면으로 돌아온다(맵을 열지 않는다).
 */
UCLASS()
class UCXCutsceneWidget : public UCXMenuWidgetBase
{
	GENERATED_BODY()

public:
	/**
	 *  컷신 재생 시작. 성공하면 위젯을 돌려주고, 끝나면 OnFinished(비어 있을 수 있음)를 한 번 호출한다.
	 *  텍스처가 하나도 없거나 이미 재생 중이거나 위젯/컨트롤러가 없으면 nullptr(재생 안 함 — 호출자가 대체 경로로 진행).
	 *  StartCut: 0부터(테스트용, 콘솔 인수).
	 */
	static UCXCutsceneWidget* Play(const UObject* WorldContext, TFunction<void()> OnFinished, int32 StartCut = 0);

	/** 모든 컷의 데이터(내용 수정은 여기). */
	static const TArray<FCXCutsceneCut>& GetOpeningCuts();

	virtual FString GetDebugState() const override;

protected:
	virtual void BuildMenu() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	virtual bool OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex) override;
	virtual bool ConsumesTopLevelBack() const override { return true; }
	virtual bool CanAcceptInput() const override;

	// --- 조정값(코드 수정 한 곳) ---

	/** 로고 텍스처(마지막 컷). */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene")
	FString LogoPath = TEXT("/Game/Project_CX/Title/Title_logo.Title_logo");

	/** 컷 시작/끝 페이드(초). 컷 사이는 검정을 거친다. */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene")
	float CutFadeTime = 0.6f;

	/** 시작 시 시작화면에서 검정으로 덮이는 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene")
	float OpenFadeTime = 0.4f;

	/** 건너뛰기 후 검정으로 가는 시간. */
	/** 타이틀이 검정으로 가려진 뒤, 첫 컷이 나오기 전까지 검정을 유지하는 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene")
	float IntroBlackHold = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "Cutscene")
	float SkipFadeTime = 0.35f;

	/** 시작 직후 이 시간 동안은 건너뛰기 입력 무시(NEW GAME을 누른 키가 바로 건너뛰기가 되지 않게). */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene")
	float InputGraceTime = 0.5f;

	/** 자막 페이드인/아웃 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Subtitle")
	float SubtitleFadeTime = 0.4f;

	/** 한글 자막 글자 크기(1080 기준 — 실제 화면 높이에 비례해 커지고 작아진다). */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Subtitle")
	float KoFontSize = 40.0f;

	/** 영어 자막 크기 = 한글 × 이 비율(0.6~0.7). */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Subtitle")
	float EnScale = 0.65f;

	/** 영어 자막 불투명도. */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Subtitle")
	float EnOpacity = 0.85f;

	/** 자막 최대 폭 = 화면 폭 × 이 비율(넘으면 줄바꿈). */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Subtitle")
	float SubtitleMaxWidthFraction = 0.7f;

	/** 자막 아래쪽 여백 = 화면 높이 × 이 비율. */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Subtitle")
	float SubtitleBottomFraction = 0.09f;

	/** 한글/영어 사이 간격(1080 기준 px). */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Subtitle")
	float SubtitleLineGap = 8.0f;

	/** 글자 외곽선 두께(1080 기준 px). */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Subtitle")
	float OutlineSize = 2.0f;

	/** 하단 가독성 그라디언트 최대 불투명도. */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Subtitle")
	float BottomShadeAlpha = 0.65f;

	/** 로고 폭 = 화면 폭 × 이 비율(높이는 화면의 40% 이하로 제한). */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Logo")
	float LogoWidthFraction = 0.4f;

	/** 로고 페이드인 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Logo")
	float LogoFadeTime = 0.9f;

	/** 로고가 뜰 때 그림을 어둡게 하는 최대 딤. */
	UPROPERTY(EditDefaultsOnly, Category = "Cutscene|Logo")
	float LogoDimAlpha = 0.4f;

private:
	enum class EState : uint8
	{
		Playing,	// 컷 재생
		Skipping,	// 건너뛰기: 검정으로 페이드
		Holding,	// 콜백 호출 후 검정 유지(맵 전환 대기, 일정 시간 뒤 스스로 정리)
		Returning	// 콘솔 재생: 검정에서 원래 화면으로 페이드
	};

	void Init(TFunction<void()> InOnFinished, int32 StartCut);
	void RequestSkip();
	void FinishSequence();
	void ClosePlayback();
	void UpdateLayout(const FVector2D& ScreenSize);
	void SwitchCut(int32 NewIndex);
	void UpdateSubtitle(const FCXCutsceneCut& Cut, float CutTime);
	static UTexture2D* LoadCutTexture(const FString& Name);

	UPROPERTY(Transient) TObjectPtr<UOverlay> RootOverlay;
	UPROPERTY(Transient) TObjectPtr<UImage> BlackBackdrop;
	UPROPERTY(Transient) TObjectPtr<UCXCutsceneArtWidget> Art;
	UPROPERTY(Transient) TObjectPtr<USizeBox> LogoBox;
	UPROPERTY(Transient) TObjectPtr<UImage> LogoImage;
	UPROPERTY(Transient) TObjectPtr<USizeBox> SubtitleBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> KoText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EnText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TObjectPtr<UImage> FadeImage;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> CutTextures;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> LogoTexture;

	TFunction<void()> OnFinishedCallback;
	EState State = EState::Playing;
	bool bInitialized = false;
	bool bCallbackFired = false;

	/** 재생 시작 지점(초)과 현재 재생 시간(전체 기준), 전체 길이. */
	float StartOffset = 0.0f;
	float Elapsed = 0.0f;
	float TotalDuration = 0.0f;
	float OpenElapsed = 0.0f;
	/** 시작 연출(타이틀 → 검정 → 첫 컷) 동안 남은 시간. 이 동안은 컷 시간이 흐르지 않는다. */
	float IntroTimeLeft = 0.0f;
	float StateTime = 0.0f;
	/** 감시 타이머용 실시간(초). */
	double StartWallTime = 0.0;

	int32 CurrentCut = INDEX_NONE;
	int32 CurrentSubtitle = INDEX_NONE;
	FVector2D LayoutSize = FVector2D::ZeroVector;
};
