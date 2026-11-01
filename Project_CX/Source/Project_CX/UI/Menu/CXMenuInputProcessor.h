// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"

class UCXMenuSubsystem;

/**
 *  메뉴용 Slate 입력 프리프로세서.
 *
 *  메뉴는 로컬 플레이어 수/컨트롤러 ID와 상관없이 "두 플레이어 누구나" 조작해야 하고(시작화면에는 로컬 플레이어가 1명뿐),
 *  2P 장치 확인을 위해 모든 게임패드의 Start를 봐야 한다. 그래서 PlayerController 입력이 아니라
 *  Slate 단계에서 키보드/모든 게임패드 입력을 가로채 메뉴 동작(ECXMenuAction)으로 바꿔 서브시스템에 넘긴다.
 *
 *  - 키: 방향키/Enter/ESC, 게임패드 D-pad/A/B/Start
 *  - 왼쪽 스틱: 데드존 0.5, 0.35초 첫 반복 후 0.1초 간격 자동 반복(0.2 입력 표)
 *  - 메뉴가 처리한 입력만 소비한다. 나머지(에디터 단축키 등)는 그대로 흘려보낸다.
 */
class FCXMenuInputProcessor : public IInputProcessor
{
public:
	explicit FCXMenuInputProcessor(UCXMenuSubsystem* InOwner);

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("CXMenuInputProcessor"); }

private:
	struct FStickState
	{
		FVector2f Value = FVector2f::ZeroVector;
		int32 HeldDirection = 0;	// ECXMenuAction 값, 0 = 없음
		float RepeatTimer = 0.0f;
	};

	TWeakObjectPtr<UCXMenuSubsystem> Owner;
	TMap<int32, FStickState> Sticks;	// 키: InputDeviceId
};
