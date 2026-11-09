// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXMenuWidgetBase.h"
#include "CXMenuItemWidget.h"
#include "CXGameInstance.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "GameFramework/PlayerController.h"
#include "CXSfx.h"

namespace CXMenuSfx
{
	USoundBase* Find(const TCHAR* Name)
	{
		return CXSfx::Find<USoundBase>(*FString::Printf(TEXT("/Game/Project_CX/Audio/SFX/UI/%s.%s"), Name, Name));
	}
}

UCXMenuWidgetBase::UCXMenuWidgetBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// UI 사운드는 PlaySound2D(bIsUISound = true)로 재생 — 일시정지(ESC 메뉴) 중에도 들린다. 에셋 사운드 클래스는 SCl_SFX_UI.
	SoundMove = CXMenuSfx::Find(TEXT("SW_UI_Move"));
	SoundConfirm = CXMenuSfx::Find(TEXT("SW_UI_Confirm"));
	SoundBack = CXMenuSfx::Find(TEXT("SW_UI_Back"));
	SoundOpen = CXMenuSfx::Find(TEXT("SW_UI_Open"));
	SoundDenied = CXMenuSfx::Find(TEXT("SW_UI_Denied"));
}

void UCXMenuWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		BuildMenu();
	}
	// SetInputMode_UIOnly의 포커스 대상이 될 수 있게
	SetIsFocusable(true);
}

void UCXMenuWidgetBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	InputLockRemaining = FMath::Max(0.0f, InputLockRemaining - InDeltaTime);
}

bool UCXMenuWidgetBase::CanAcceptInput() const
{
	return InputLockRemaining <= 0.0f;
}

void UCXMenuWidgetBase::LockInput(float Seconds)
{
	InputLockRemaining = FMath::Max(InputLockRemaining, Seconds);
}

bool UCXMenuWidgetBase::HandleMenuInput(const FCXMenuInput& Input, int32 PlayerIndex)
{
	// 최상위 ESC/B는 화면별 정의 — 아무것도 안 하는 화면이면 소비하지 않는다(에디터 PIE의 ESC 종료 등).
	const bool bTopLevelBack = Input.Action == ECXMenuAction::Back && !IsModalOpen();

	if (!CanAcceptInput())
	{
		return !bTopLevelBack || ConsumesTopLevelBack();
	}

	if (IsModalOpen())
	{
		// 확인창 쪽은 키 반복 무시(실수로 연속 확인 방지)
		if (!Input.bRepeat || Input.Action == ECXMenuAction::Left || Input.Action == ECXMenuAction::Right)
		{
			FocusPlayer = PlayerIndex;
			Dialog->HandleAction(Input.Action, PlayerIndex);
		}
		return true;
	}

	if (OnMenuInput(Input, PlayerIndex))
	{
		return true;
	}

	switch (Input.Action)
	{
	case ECXMenuAction::Up:
	case ECXMenuAction::Down:
		FocusPlayer = PlayerIndex;
		MoveFocus(Input.Action == ECXMenuAction::Up ? -1 : 1);
		return true;

	case ECXMenuAction::Accept:
		if (Input.bRepeat)
		{
			return true;
		}
		FocusPlayer = PlayerIndex;
		RefreshFocusVisuals();
		if (Items.IsValidIndex(FocusIndex))
		{
			OnItemActivated(Items[FocusIndex]->GetItemId());
		}
		return true;

	case ECXMenuAction::Back:
		if (OnBack())
		{
			PlayUISound(ECXMenuSound::Back);
			return true;
		}
		return ConsumesTopLevelBack();

	default:
		return false;
	}
}

// ---------------------------------------------------------------------------------------------
// 항목 / 포커스

UCXMenuItemWidget* UCXMenuWidgetBase::AddMenuItem(UPanelWidget* Parent, FName ItemId, const FText& Label)
{
	UCXMenuItemWidget* Item = WidgetTree->ConstructWidget<UCXMenuItemWidget>(UCXMenuItemWidget::StaticClass(), ItemId);
	Item->Setup(ItemId, Label);
	Item->OnHovered.BindUObject(this, &UCXMenuWidgetBase::HandleItemHovered);
	Item->OnClicked.BindUObject(this, &UCXMenuWidgetBase::HandleItemClicked);
	if (Parent)
	{
		Parent->AddChild(Item);
	}
	Items.Add(Item);
	return Item;
}

UCXMenuItemWidget* UCXMenuWidgetBase::FindItem(FName ItemId) const
{
	for (UCXMenuItemWidget* Item : Items)
	{
		if (Item && Item->GetItemId() == ItemId)
		{
			return Item;
		}
	}
	return nullptr;
}

void UCXMenuWidgetBase::SetItemShown(FName ItemId, bool bShown)
{
	if (UCXMenuItemWidget* Item = FindItem(ItemId))
	{
		// 비활성(조건 미충족) 항목은 목록에서 숨긴다(0.3).
		Item->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (!bShown && Items.IsValidIndex(FocusIndex) && Items[FocusIndex] == Item)
		{
			MoveFocus(1);
		}
	}
}

bool UCXMenuWidgetBase::IsItemShown(int32 Index) const
{
	return Items.IsValidIndex(Index) && Items[Index] && Items[Index]->GetVisibility() != ESlateVisibility::Collapsed;
}

void UCXMenuWidgetBase::SetFocusItem(FName ItemId)
{
	SetFocusIndex(Items.IndexOfByPredicate([ItemId](const UCXMenuItemWidget* Item) { return Item && Item->GetItemId() == ItemId; }));
}

void UCXMenuWidgetBase::SetFocusIndex(int32 NewIndex)
{
	FocusIndex = IsItemShown(NewIndex) ? NewIndex : FocusIndex;
	RefreshFocusVisuals();
}

void UCXMenuWidgetBase::MoveFocus(int32 Delta)
{
	const int32 Num = Items.Num();
	if (Num == 0)
	{
		return;
	}
	int32 Index = Items.IsValidIndex(FocusIndex) ? FocusIndex : 0;
	for (int32 Step = 0; Step < Num; ++Step)
	{
		// 끝에서 순환(wrap-around), 숨긴 항목은 건너뜀
		Index = (Index + Delta + Num) % Num;
		if (IsItemShown(Index))
		{
			if (Index != FocusIndex)
			{
				FocusIndex = Index;
				PlayUISound(ECXMenuSound::Move);
			}
			break;
		}
	}
	RefreshFocusVisuals();
}

FName UCXMenuWidgetBase::GetFocusedItemId() const
{
	return Items.IsValidIndex(FocusIndex) ? Items[FocusIndex]->GetItemId() : NAME_None;
}

void UCXMenuWidgetBase::RefreshFocusVisuals()
{
	const FLinearColor Color = CXMenuStyle::PlayerColor(FocusPlayer);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (Items[i])
		{
			Items[i]->SetFocused(i == FocusIndex, Color);
		}
	}
}

int32 UCXMenuWidgetBase::GetKeyboardPlayer() const
{
	// 마우스 = 키보드 사용자 입력(8장)
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	return GI ? GI->ResolvePlayerForInput(false, INPUTDEVICEID_NONE) : 0;
}

bool UCXMenuWidgetBase::IsPointerActive() const
{
	// 커서는 마우스를 실제로 움직였을 때만 보인다(0.2). 커서가 숨어 있을 때 Slate가 합성한 호버로 포커스가 튀지 않게 한다.
	const APlayerController* PC = GetOwningPlayer();
	return PC && PC->ShouldShowMouseCursor();
}

void UCXMenuWidgetBase::HandleItemHovered(UCXMenuItemWidget* Item)
{
	if (!CanAcceptInput() || IsModalOpen() || !IsPointerActive())
	{
		return;
	}
	const int32 Index = Items.IndexOfByKey(Item);
	if (Index != INDEX_NONE && Index != FocusIndex)
	{
		FocusPlayer = GetKeyboardPlayer();
		FocusIndex = Index;
		PlayUISound(ECXMenuSound::Move);
		RefreshFocusVisuals();
	}
}

void UCXMenuWidgetBase::HandleItemClicked(UCXMenuItemWidget* Item)
{
	if (!CanAcceptInput() || IsModalOpen())
	{
		return;
	}
	FCXMenuInput Input;
	Input.Action = ECXMenuAction::Accept;
	HandleMenuInput(Input, GetKeyboardPlayer());
}

// ---------------------------------------------------------------------------------------------
// 확인창

void UCXMenuWidgetBase::ShowConfirm(const FText& Title, const FText& Body, TFunction<void()> OnConfirm)
{
	OpenDialog(Title, Body, FText::FromString(TEXT("CONFIRM")), FText::FromString(TEXT("CANCEL")), false, MoveTemp(OnConfirm));
}

void UCXMenuWidgetBase::ShowNotice(const FText& Title, const FText& Body)
{
	OpenDialog(Title, Body, FText::FromString(TEXT("BACK")), FText::GetEmpty(), true, nullptr);
}

void UCXMenuWidgetBase::OpenDialog(const FText& Title, const FText& Body, const FText& ConfirmLabel, const FText& CancelLabel, bool bSingle, TFunction<void()> OnConfirm)
{
	if (!ModalHost)
	{
		return;
	}
	if (!Dialog)
	{
		Dialog = WidgetTree->ConstructWidget<UCXConfirmDialogWidget>(UCXConfirmDialogWidget::StaticClass(), TEXT("ConfirmDialog"));
		if (UOverlaySlot* S = ModalHost->AddChildToOverlay(Dialog))
		{
			S->SetHorizontalAlignment(HAlign_Fill);
			S->SetVerticalAlignment(VAlign_Fill);
		}
		Dialog->OnResult.BindUObject(this, &UCXMenuWidgetBase::HandleDialogResult);
		Dialog->OnFocusMoved.BindWeakLambda(this, [this]() { PlayUISound(ECXMenuSound::Move); });
		Dialog->CanAcceptPointer.BindWeakLambda(this, [this]() { return CanAcceptInput() && IsPointerActive(); });
	}
	PendingConfirm = MoveTemp(OnConfirm);
	Dialog->Open(Title, Body, ConfirmLabel, CancelLabel, bSingle, FocusPlayer);
	PlayUISound(ECXMenuSound::Open);
	LockInput();
}

bool UCXMenuWidgetBase::IsModalOpen() const
{
	return Dialog && Dialog->GetVisibility() != ESlateVisibility::Collapsed;
}

void UCXMenuWidgetBase::HandleDialogResult(ECXDialogResult Result)
{
	LockInput();
	// 서브 화면에서 돌아오면 들어갔던 항목 포커스 그대로(0.3)
	RefreshFocusVisuals();

	TFunction<void()> Callback = MoveTemp(PendingConfirm);
	PendingConfirm = nullptr;
	if (Result == ECXDialogResult::Confirmed && Callback)
	{
		PlayUISound(ECXMenuSound::Confirm);
		Callback();
	}
	else
	{
		PlayUISound(ECXMenuSound::Back);
	}
}

void UCXMenuWidgetBase::PlayUISound(ECXMenuSound Sound) const
{
	USoundBase* Asset = nullptr;
	switch (Sound)
	{
	case ECXMenuSound::Move:	Asset = SoundMove; break;
	case ECXMenuSound::Confirm:	Asset = SoundConfirm; break;
	case ECXMenuSound::Back:	Asset = SoundBack; break;
	case ECXMenuSound::Open:	Asset = SoundOpen; break;
	case ECXMenuSound::Denied:	Asset = SoundDenied; break;
	}
	if (Asset)
	{
		// bIsUISound: 일시정지 중에도 재생
		UGameplayStatics::PlaySound2D(this, Asset, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
	}
}

FString UCXMenuWidgetBase::GetDebugState() const
{
	return FString::Printf(TEXT("focus=%s modal=%d player=%d lock=%.2f"), *GetFocusedItemId().ToString(), IsModalOpen() ? 1 : 0, FocusPlayer, InputLockRemaining);
}
