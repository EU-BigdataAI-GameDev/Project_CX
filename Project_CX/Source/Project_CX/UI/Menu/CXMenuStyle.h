// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class FSlateWindowElementList;
struct FGeometry;
struct FSlateBrush;

/** 메뉴 입력 동작(설정 메뉴 기획 0.2 입력 표). */
enum class ECXMenuAction : uint8
{
	None,
	Up,
	Down,
	Left,
	Right,
	Accept,		// Enter / 좌클릭 / A
	Back,		// ESC / B
	Start		// 게임패드 Start(장치 확인)
};

/** 입력 프리프로세서가 만든 메뉴 입력 하나. 장치 정보로 어느 플레이어 입력인지 판단한다. */
struct FCXMenuInput
{
	ECXMenuAction Action = ECXMenuAction::None;
	bool bFromGamepad = false;
	FInputDeviceId DeviceId;
	bool bRepeat = false;
};

/** UI 사운드 큐(설정 메뉴 기획 0.6). */
enum class ECXMenuSound : uint8
{
	Move,
	Confirm,
	Back,
	Open,
	Denied
};

/**
 *  공통 UI 비주얼 토큰(설정 메뉴 기획 0.5) — ACoopHUD 기본값과 동일한 색.
 *  시작화면/ESC 메뉴/설정/게임 오버 위젯이 모두 이 값을 쓴다.
 */
namespace CXMenuStyle
{
	inline const FLinearColor Panel(0.01f, 0.015f, 0.03f, 0.62f);
	inline const FLinearColor Brother(0.0f, 0.85f, 1.0f, 1.0f);
	inline const FLinearColor Sister(1.0f, 0.22f, 0.62f, 1.0f);
	inline const FLinearColor Warning(1.0f, 0.35f, 0.08f, 1.0f);
	inline const FLinearColor Text(1.0f, 1.0f, 1.0f, 0.9f);
	inline const FLinearColor TextDim(1.0f, 1.0f, 1.0f, 0.4f);
	inline const FLinearColor Dim(0.0f, 0.0f, 0.0f, 0.55f);

	/** 화면 전환(열기/닫기/확인창) 직후 입력 잠금 시간. */
	constexpr float InputLockTime = 0.15f;
	/** 열기/닫기 페이드 시간. */
	constexpr float OpenTime = 0.15f;
	/** 포커스 바 슬라이드 시간. */
	constexpr float FocusSlideTime = 0.08f;

	/** 플레이어 슬롯 색(0 = 오빠 청록, 1 = 여동생 마젠타). */
	FLinearColor PlayerColor(int32 PlayerIndex);

	/** 엔진 기본 폰트(Roboto). Typeface: Regular / Bold / Light. */
	FSlateFontInfo Font(int32 Size, FName Typeface = TEXT("Regular"), int32 LetterSpacing = 0);

	const FSlateBrush* WhiteBrush();

	/** 로컬 좌표 사각형 채우기. */
	void DrawRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& Color);

	/** 얇은 네온 테두리 + 모서리 브래킷(HUD 타겟 브래킷과 같은 문법). Geometry 전체를 감싼다. */
	void DrawNeonFrame(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FLinearColor& Color, float LineAlpha = 0.35f, float Bracket = 12.0f, float Thickness = 2.0f);
}
