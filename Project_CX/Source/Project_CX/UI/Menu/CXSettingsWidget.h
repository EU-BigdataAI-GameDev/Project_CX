// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CXMenuWidgetBase.h"
#include "CXGameUserSettings.h"
#include "CXSettingsWidget.generated.h"

class UTextBlock;
class UImage;
class UBorder;
class UOverlay;
class USizeBox;
class UVerticalBox;
class UWidgetSwitcher;
class UCXMenuItemWidget;
class UCXSettingsTabWidget;

DECLARE_DELEGATE_OneParam(FCXSettingsBarValueEvent, int32 /*NewValue 0~100*/);
DECLARE_DELEGATE_OneParam(FCXSettingsTabEvent, int32 /*TabIndex*/);

/** 10칸 음량 바(설정 메뉴 기획 4.2). 직접 그리고, 마우스 클릭/드래그로 값 설정(10단위 스냅). */
UCLASS()
class UCXSettingsBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetBarState(int32 InValue, const FLinearColor& InColor, bool bInHighlighted);

	/** 클릭/드래그로 새 값(0~100, 10단위). */
	FCXSettingsBarValueEvent OnValueSet;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void SetFromPointer(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent);

	int32 Value = 0;
	FLinearColor Color = FLinearColor::White;
	bool bHighlighted = false;
	bool bDragging = false;
};

/** 설정 메뉴 작은 아이콘(자물쇠 / 화면 분할 미리보기). 직접 그린다. */
UENUM()
enum class ECXSettingsGlyph : uint8
{
	Lock,
	SplitTopBottom,
	SplitLeftRight
};

UCLASS()
class UCXSettingsGlyphWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetGlyph(ECXSettingsGlyph InGlyph, const FLinearColor& InColor) { Glyph = InGlyph; Color = InColor; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	ECXSettingsGlyph Glyph = ECXSettingsGlyph::Lock;
	FLinearColor Color = FLinearColor::White;
};

/** 상단 탭 하나(현재 탭 = 밑줄, 탭 줄 포커스 = 플레이어 색 바탕). */
UCLASS()
class UCXSettingsTabWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(int32 InTabIndex, const FText& InLabel, float Width);
	void SetTabState(bool bInSelected, bool bInFocused, const FLinearColor& InColor);

	FCXSettingsTabEvent OnHovered;
	FCXSettingsTabEvent OnClicked;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	UPROPERTY(Transient) TObjectPtr<USizeBox> RootBox;
	UPROPERTY(Transient) TObjectPtr<UBorder> Background;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LabelText;
	UPROPERTY(Transient) TObjectPtr<UImage> Underline;

	int32 TabIndex = 0;
};

/** 설정 메뉴 한 줄의 값 표시 부품(위젯 트리 소유 — 여기서는 참조만). */
USTRUCT()
struct FCXSettingsRow
{
	GENERATED_BODY()

	UPROPERTY() FName Id;
	UPROPERTY() TObjectPtr<UCXMenuItemWidget> Item;
	UPROPERTY() TObjectPtr<UTextBlock> LeftArrow;
	UPROPERTY() TObjectPtr<UTextBlock> RightArrow;
	UPROPERTY() TObjectPtr<UTextBlock> ValueText;
	UPROPERTY() TObjectPtr<UTextBlock> NumberText;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<UCXSettingsBarWidget> Bar;
	UPROPERTY() TObjectPtr<UCXSettingsGlyphWidget> Lock;
	UPROPERTY() TObjectPtr<UCXSettingsGlyphWidget> Preview;

	FText Description;
};

/** 탭 한 페이지의 항목(포커스 목록). */
USTRUCT()
struct FCXSettingsPage
{
	GENERATED_BODY()

	UPROPERTY() TArray<TObjectPtr<UCXMenuItemWidget>> Items;
};

/**
 *  설정 메뉴(.claude/Plans/설정 메뉴 기획.md) — 시작화면과 ESC 메뉴가 같은 화면을 연다.
 *
 *  - 탭 4개: AUDIO / SPLIT SCREEN / INPUT DEVICES / CONTROLS. Q/E·LB/RB는 어디서나 탭 전환, 위/아래로 탭 줄 ↔ 항목 리스트(순환).
 *  - 값은 바꾸는 즉시 적용(음량 믹스, 화면 분할), 닫을 때 UCXGameUserSettings::SaveSettings. 조작 장치는 즉시 저장.
 *  - INPUT DEVICES: 시작화면에서만 변경 가능(ESC 메뉴에서는 자물쇠 + 거부음 + 안내), 둘 다 키보드 불가(자동 스왑 없음), 행마다 연결 상태.
 *  - CONTROLS: 캐릭터(BROTHER/SISTER) × 장치(키보드/게임패드) 조작표. 장치는 현재 조작 장치 설정을 먼저 보여준다.
 *  - RESET TO DEFAULT: 확인창 후 현재 탭만 기본값.
 *  - 기본 포커스: 첫 진입 AUDIO/MASTER, 같은 세션에서 다시 열면 마지막 탭.
 */
UCLASS()
class UCXSettingsWidget : public UCXMenuWidgetBase
{
	GENERATED_BODY()

public:
	/**
	 *  설정 메뉴를 Opener 위에 띄운다. bAllowDeviceChange = 시작화면(조작 장치 변경 가능).
	 *  OnClosed는 설정 메뉴가 닫히고(저장 후) 스택에서 내려간 뒤 호출된다.
	 */
	static UCXSettingsWidget* Open(UCXMenuWidgetBase* Opener, bool bAllowDeviceChange, TFunction<void()> OnClosed);

	UCXSettingsWidget(const FObjectInitializer& ObjectInitializer);

	virtual FString GetDebugState() const override;

protected:
	virtual void BuildMenu() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual bool OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex) override;
	virtual bool OnBack() override;
	virtual bool ConsumesTopLevelBack() const override { return true; }

	/** SFX 미리듣기 큐(비어 있으면 UI_Move 슬롯 사용, 그것도 없으면 무음). */
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Sound")
	TObjectPtr<USoundBase> SfxPreviewSound;

	/** 음량 바(전체/배경음) 한 칸 변경음(비어 있으면 UI_Move 슬롯). */
	UPROPERTY(EditDefaultsOnly, Category = "Settings|Sound")
	TObjectPtr<USoundBase> SliderStepSound;

private:
	void Configure(bool bInAllowDeviceChange, TFunction<void()> InOnClosed);

	// --- 구성 ---
	void BuildPage(int32 TabIndex, UVerticalBox* List);
	FCXSettingsRow& AddRow(UVerticalBox* List, FName Id, const TCHAR* Label, const TCHAR* Description, bool bSlider, bool bChoice);
	void BuildControlsTable(UVerticalBox* List);
	FCXSettingsRow* FindRow(FName Id);
	const FCXSettingsRow* FindRow(FName Id) const;

	// --- 탭 / 포커스 ---
	void SwitchTab(int32 NewTab, bool bFocusTabRow, bool bPlaySound = true);
	void FocusTabRow();
	void FocusFirstItem();
	void MoveVertical(int32 Delta);
	int32 FindVisibleIndex(int32 Start, int32 Delta, bool bWrap) const;
	void HandleTabHovered(int32 TabIndex);
	void HandleTabClicked(int32 TabIndex);

	// --- 값 ---
	void ChangeValue(FName Id, int32 Direction, bool bRepeat);
	void SetVolume(FName Id, int32 NewValue);
	void ActivateRow(FName Id);
	void ResetCurrentTab();
	void TryChangeDevice(int32 PlayerIndex);
	void CloseSettings();

	// --- 표시 ---
	void RefreshAll();
	void RefreshRows();
	void RefreshTabs();
	void RefreshControlsTable();
	void RefreshDescription();
	void RefreshHint();
	void FlashMessage(const FString& Message);
	bool IsSlotConnected(int32 PlayerIndex) const;

	UPROPERTY(Transient) TObjectPtr<UOverlay> RootOverlay;
	UPROPERTY(Transient) TObjectPtr<USizeBox> PanelBox;
	UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> PageSwitcher;
	UPROPERTY(Transient) TArray<TObjectPtr<UCXSettingsTabWidget>> Tabs;
	UPROPERTY(Transient) TArray<FCXSettingsPage> Pages;
	UPROPERTY(Transient) TArray<FCXSettingsRow> Rows;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TabHintLeft;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TabHintRight;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescriptionText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DeviceNoteText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ControlsTipText;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ControlActionTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ControlKeyTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ControlNoteTexts;
	UPROPERTY(Transient) TObjectPtr<UOverlay> ModalLayer;

	TFunction<void()> OnClosedCallback;
	bool bAllowDeviceChange = true;
	bool bClosed = false;

	int32 CurrentTab = 0;
	bool bTabFocused = false;
	int32 LastFocusIndex = INDEX_NONE;
	bool bLastInputGamepad = false;

	/** CONTROLS 페이지: 0 오빠 / 1 여동생, 보고 있는 장치. */
	int32 ControlsCharacter = 0;
	ECXInputDeviceType ControlsDevice = ECXInputDeviceType::KeyboardMouse;

	float OpenAlpha = 0.0f;
	float StatusPollTimer = 0.0f;
	FString FlashText;
	float FlashRemaining = 0.0f;
};
