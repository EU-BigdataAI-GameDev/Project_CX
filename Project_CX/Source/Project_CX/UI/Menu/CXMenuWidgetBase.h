// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CXMenuStyle.h"
#include "CXConfirmDialogWidget.h"
#include "CXMenuWidgetBase.generated.h"

class UPanelWidget;
class UOverlay;
class USoundBase;
class UCXMenuItemWidget;

/**
 *  전체 화면 메뉴 공통 베이스(설정 메뉴 기획 0장 공통 UI 규칙) — 시작화면, 이후 ESC 메뉴/설정/게임 오버가 상속한다.
 *
 *  - 입력은 UCXMenuSubsystem의 입력 프리프로세서가 장치와 상관없이 모아서 HandleMenuInput으로 넣어 준다.
 *    (두 플레이어 누구나 조작, 먼저 들어온 입력 우선, 전환 직후 0.15초 잠금)
 *  - 세로 항목 목록 포커스: 위/아래 순환, 숨긴 항목은 건너뜀, 마우스 호버 = 포커스 이동, 하이라이트 색 = 마지막 입력 플레이어 색
 *  - 확인창/알림창: ShowConfirm / ShowNotice. 열려 있으면 입력은 전부 확인창으로 간다. 닫히면 원래 항목 포커스 유지.
 *  - UI 사운드 큐(Move/Confirm/Back/Open/Denied) 슬롯. 에셋이 없으면 조용히 넘어간다.
 *
 *  하위 클래스는 BuildMenu()에서 위젯 트리를 만들고, AddMenuItem으로 항목을 추가하고, SetModalHost로 확인창이 들어갈 레이어를 지정한다.
 */
UCLASS(Abstract)
class UCXMenuWidgetBase : public UUserWidget
{
	GENERATED_BODY()

public:
	/** UI 사운드 슬롯 기본값(효과음 적용 기획 2.5 — Kenney UI Audio, SCl_SFX_UI). */
	UCXMenuWidgetBase(const FObjectInitializer& ObjectInitializer);

	/** 메뉴 입력 하나 처리. 소비했으면 true(엔진/에디터로 넘기지 않음). */
	bool HandleMenuInput(const FCXMenuInput& Input, int32 PlayerIndex);

	/** 매핑 여부와 상관없이 아무 키/클릭이 들어옴(진입 연출 스킵 등). 소비해야 하면 true. */
	virtual bool HandleAnyInput() { return false; }

	/** 되돌릴 수 없는 동작 확인창. 기본 포커스 CANCEL. */
	void ShowConfirm(const FText& Title, const FText& Body, TFunction<void()> OnConfirm);

	/** 버튼 하나(BACK)짜리 알림창. */
	void ShowNotice(const FText& Title, const FText& Body);

	bool IsModalOpen() const;

	/** 화면 전환 직후 입력 잠금. */
	void LockInput(float Seconds = CXMenuStyle::InputLockTime);

	/** 마지막으로 메뉴를 조작한 플레이어(서브 화면이 포커스 색을 이어받을 때). */
	int32 GetFocusPlayer() const { return FocusPlayer; }

	/** 테스트/디버그용 상태 문자열(포커스 항목, 모달 여부 등). */
	virtual FString GetDebugState() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 위젯 트리 생성(NativeOnInitialized에서 한 번). */
	virtual void BuildMenu() {}

	/** 포커스 항목 선택(Enter/A/클릭). */
	virtual void OnItemActivated(FName ItemId) {}

	/** 최상위에서 ESC/B. 처리했으면 true. 기본: 아무 동작 없음(소비 안 함). */
	virtual bool OnBack() { return false; }

	/** 최상위 ESC/B를 OnBack 결과/입력 잠금과 상관없이 항상 소비할지(ESC 메뉴: 엔진/PIE로 ESC가 새지 않게). 기본 false. */
	virtual bool ConsumesTopLevelBack() const { return false; }

	/** 하위 클래스 선처리(Start 등). 소비했으면 true. 확인창이 열려 있으면 호출되지 않는다. */
	virtual bool OnMenuInput(const FCXMenuInput& Input, int32 PlayerIndex) { return false; }

	/** 연출/화면 전환 중이면 false → 입력 무시(소비만). */
	virtual bool CanAcceptInput() const;

	UCXMenuItemWidget* AddMenuItem(UPanelWidget* Parent, FName ItemId, const FText& Label);
	void SetItemShown(FName ItemId, bool bShown);
	UCXMenuItemWidget* FindItem(FName ItemId) const;
	void SetFocusItem(FName ItemId);
	void SetFocusIndex(int32 NewIndex);
	void MoveFocus(int32 Delta);
	FName GetFocusedItemId() const;
	void RefreshFocusVisuals();

	void SetModalHost(UOverlay* Host) { ModalHost = Host; }

	/** 마우스 커서가 보이는 중(마우스를 실제로 움직임) — 호버로 포커스를 옮겨도 되는지. */
	bool IsPointerActive() const;
	/** 키보드/마우스를 쓰는 플레이어 슬롯(마우스 입력의 포커스 색). */
	int32 GetKeyboardPlayer() const;
	void PlayUISound(ECXMenuSound Sound) const;

	/** 마지막으로 메뉴를 조작한 플레이어(포커스 색). */
	int32 FocusPlayer = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCXMenuItemWidget>> Items;

	int32 FocusIndex = INDEX_NONE;

	UPROPERTY(EditDefaultsOnly, Category = "Menu|Sound") TObjectPtr<USoundBase> SoundMove;
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Sound") TObjectPtr<USoundBase> SoundConfirm;
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Sound") TObjectPtr<USoundBase> SoundBack;
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Sound") TObjectPtr<USoundBase> SoundOpen;
	UPROPERTY(EditDefaultsOnly, Category = "Menu|Sound") TObjectPtr<USoundBase> SoundDenied;

private:
	void OpenDialog(const FText& Title, const FText& Body, const FText& ConfirmLabel, const FText& CancelLabel, bool bSingle, TFunction<void()> OnConfirm);
	void HandleDialogResult(ECXDialogResult Result);
	void HandleItemHovered(UCXMenuItemWidget* Item);
	void HandleItemClicked(UCXMenuItemWidget* Item);
	bool IsItemShown(int32 Index) const;

	UPROPERTY(Transient) TObjectPtr<UOverlay> ModalHost;
	UPROPERTY(Transient) TObjectPtr<UCXConfirmDialogWidget> Dialog;

	TFunction<void()> PendingConfirm;
	float InputLockRemaining = 0.0f;
};
