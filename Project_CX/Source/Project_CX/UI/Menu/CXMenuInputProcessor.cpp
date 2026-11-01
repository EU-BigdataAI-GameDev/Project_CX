// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXMenuInputProcessor.h"
#include "CXMenuSubsystem.h"
#include "CXMenuStyle.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"

namespace CXMenuInput
{
	constexpr float StickDeadZone = 0.5f;
	constexpr float FirstRepeatDelay = 0.35f;
	constexpr float RepeatInterval = 0.1f;

	ECXMenuAction MapKey(const FKey& Key)
	{
		if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)			return ECXMenuAction::Up;
		if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)		return ECXMenuAction::Down;
		if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left)		return ECXMenuAction::Left;
		if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right)	return ECXMenuAction::Right;
		if (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom)	return ECXMenuAction::Accept;
		if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)	return ECXMenuAction::Back;
		if (Key == EKeys::Gamepad_Special_Right)						return ECXMenuAction::Start;
		return ECXMenuAction::None;
	}

	bool IsStickDigitalKey(const FKey& Key)
	{
		// XInput이 스틱 기울기로 만들어 내는 디지털 키 — 아날로그 쪽에서 직접 처리하므로 삼킨다.
		return Key == EKeys::Gamepad_LeftStick_Up || Key == EKeys::Gamepad_LeftStick_Down
			|| Key == EKeys::Gamepad_LeftStick_Left || Key == EKeys::Gamepad_LeftStick_Right;
	}
}

FCXMenuInputProcessor::FCXMenuInputProcessor(UCXMenuSubsystem* InOwner)
	: Owner(InOwner)
{
}

bool FCXMenuInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	UCXMenuSubsystem* Subsystem = Owner.Get();
	if (!Subsystem || !Subsystem->IsGameViewportFocused())
	{
		return false;
	}

	const FKey Key = InKeyEvent.GetKey();
	const bool bGamepad = Key.IsGamepadKey();

	if (!Subsystem->HasActiveMenu())
	{
		// ESC 메뉴 재개 직후 0.15초: 게임플레이 입력 무시(A/Enter가 점프·사격으로 새지 않게)
		if (Subsystem->IsGameplayInputBlocked())
		{
			return true;
		}
		// 메뉴가 없을 때는 ESC / 게임패드 Start만 본다 → 인게임이면 일시정지 메뉴 열기
		if (!InKeyEvent.IsRepeat() && (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right))
		{
			return Subsystem->HandlePauseKey(bGamepad, InKeyEvent.GetInputDeviceId());
		}
		return false;
	}
	// 키/패드 입력이 오면 마우스 커서를 숨긴다(0.2)
	Subsystem->NotifyInputDeviceUsed(false);

	if (CXMenuInput::IsStickDigitalKey(Key))
	{
		return true;
	}

	FCXMenuInput Input;
	Input.Action = CXMenuInput::MapKey(Key);
	Input.bFromGamepad = bGamepad;
	Input.DeviceId = InKeyEvent.GetInputDeviceId();
	Input.bRepeat = InKeyEvent.IsRepeat();

	if (Input.Action == ECXMenuAction::None)
	{
		// 매핑 없는 키도 연출 스킵에는 쓴다(소비는 연출 스킵일 때만).
		return !Input.bRepeat && Subsystem->RouteAnyInput();
	}
	return Subsystem->RouteMenuInput(Input);
}

bool FCXMenuInputProcessor::HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	if (Key != EKeys::Gamepad_LeftX && Key != EKeys::Gamepad_LeftY)
	{
		return false;
	}
	FStickState& State = Sticks.FindOrAdd(InAnalogInputEvent.GetInputDeviceId().GetId());
	if (Key == EKeys::Gamepad_LeftX)
	{
		State.Value.X = InAnalogInputEvent.GetAnalogValue();
	}
	else
	{
		State.Value.Y = InAnalogInputEvent.GetAnalogValue();
	}
	// 실제 방향 판정/반복은 Tick에서
	UCXMenuSubsystem* Subsystem = Owner.Get();
	return Subsystem && Subsystem->HasActiveMenu() && Subsystem->IsGameViewportFocused();
}

void FCXMenuInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	UCXMenuSubsystem* Subsystem = Owner.Get();
	const bool bActive = Subsystem && Subsystem->HasActiveMenu() && Subsystem->IsGameViewportFocused();

	for (TPair<int32, FStickState>& Pair : Sticks)
	{
		FStickState& State = Pair.Value;
		const FVector2f V = State.Value;

		// 우세 축 방향(데드존 0.5)
		ECXMenuAction Dir = ECXMenuAction::None;
		if (FMath::Abs(V.Y) >= CXMenuInput::StickDeadZone && FMath::Abs(V.Y) >= FMath::Abs(V.X))
		{
			Dir = V.Y > 0.0f ? ECXMenuAction::Up : ECXMenuAction::Down;
		}
		else if (FMath::Abs(V.X) >= CXMenuInput::StickDeadZone)
		{
			Dir = V.X > 0.0f ? ECXMenuAction::Right : ECXMenuAction::Left;
		}

		bool bFire = false;
		bool bRepeat = false;
		if (Dir == ECXMenuAction::None)
		{
			State.HeldDirection = 0;
		}
		else if (State.HeldDirection != static_cast<int32>(Dir))
		{
			State.HeldDirection = static_cast<int32>(Dir);
			State.RepeatTimer = CXMenuInput::FirstRepeatDelay;
			bFire = true;
		}
		else
		{
			State.RepeatTimer -= DeltaTime;
			if (State.RepeatTimer <= 0.0f)
			{
				State.RepeatTimer += CXMenuInput::RepeatInterval;
				bFire = bRepeat = true;
			}
		}

		if (bFire && bActive)
		{
			FCXMenuInput Input;
			Input.Action = Dir;
			Input.bFromGamepad = true;
			Input.DeviceId = FInputDeviceId::CreateFromInternalId(Pair.Key);
			Input.bRepeat = bRepeat;
			Subsystem->NotifyInputDeviceUsed(false);
			Subsystem->RouteMenuInput(Input);
		}
	}
}

bool FCXMenuInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	if (UCXMenuSubsystem* Subsystem = Owner.Get())
	{
		if (Subsystem->HasActiveMenu() && !MouseEvent.GetCursorDelta().IsNearlyZero())
		{
			Subsystem->NotifyInputDeviceUsed(true);
		}
	}
	return false;
}

bool FCXMenuInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	UCXMenuSubsystem* Subsystem = Owner.Get();
	if (Subsystem && !Subsystem->HasActiveMenu() && Subsystem->IsGameplayInputBlocked() && Subsystem->IsGameViewportFocused())
	{
		// RESUME 클릭이 사격으로 새지 않게
		return true;
	}
	if (Subsystem && Subsystem->HasActiveMenu() && Subsystem->IsGameViewportFocused())
	{
		Subsystem->NotifyInputDeviceUsed(true);
		// 연출 중 클릭은 스킵만 하고 소비
		return Subsystem->RouteAnyInput();
	}
	return false;
}
