// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "CXGameViewportClient.generated.h"

/**
 *  프로젝트 게임 뷰포트 클라이언트(DefaultEngine.ini [/Script/Engine.Engine] GameViewportClientClassName).
 *
 *  - 화면 분할(설정 메뉴 기획 4.3): 2인일 때 UCXGameUserSettings::SplitLayout(상하/좌우)을 읽는다.
 *    LayoutPlayers가 매 프레임 이 값을 다시 읽으므로 인게임에서 바꾸면 즉시 반영된다. 카메라 설정은 건드리지 않는다.
 *  - 조작 장치(설정 메뉴 기획 8장): 둘 다 게임패드인 조합에서만 엔진의 bOffsetPlayerGamepadIds(모든 패드 → 2P) 대신
 *    시작화면에서 배정된 패드 → 해당 슬롯 플레이어로 보낸다(배정이 없으면 패드0 → P1, 패드1 → P2).
 *    나머지 조합은 엔진 기본 동작 그대로(키보드 = LocalPlayer 0, 게임패드 = LocalPlayer 1).
 */
UCLASS()
class UCXGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	virtual void UpdateActiveSplitscreenType() override;
	virtual void RemapControllerInput(FInputKeyEventArgs& InOutKeyEvent) override;

	/**
	 *  테스트용: 이 게임패드 장치/ControllerId에서 들어온 버튼 입력이 실제로 어느 로컬 플레이어로 가는지(실제 입력 경로와 같은 Remap 사용).
	 *  반환: LocalPlayer 인덱스, 없으면 -1.
	 */
	UFUNCTION(BlueprintCallable, Category = "CX|Debug", meta = (WorldContext = "WorldContext"))
	static int32 DebugResolveGamepadPlayer(const UObject* WorldContext, int32 InputDeviceId, int32 ControllerId);

private:
	/** 둘 다 게임패드 조합에서 이 패드 입력을 받을 로컬 플레이어 인덱스. 해당 없으면 INDEX_NONE(엔진 기본 동작). */
	int32 ResolveGamepadLocalPlayer(FInputDeviceId InputDevice, int32 ControllerId) const;
};
