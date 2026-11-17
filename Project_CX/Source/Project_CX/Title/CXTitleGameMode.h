// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CXTitleGameMode.generated.h"

/**
 *  시작화면 맵(Map_Title) 전용 게임모드 — 폰 없음, 협동 HUD 없음, 로컬 플레이어 1명(분할 화면 없음).
 *
 *  ALocalCoopGameMode를 상속하지 않으므로 2P 로컬 플레이어를 만들지 않는다. 인게임에서 타이틀로 돌아온 경우
 *  남아 있는 2P 로컬 플레이어는 여기서 제거하고, 인게임 맵(LocalCoopGameMode)이 다시 만든다.
 */
UCLASS()
class ACXTitleGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACXTitleGameMode();

	/** 폰을 스폰하지 않는다(메뉴만 띄움). */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

protected:
	virtual void BeginPlay() override;
};
