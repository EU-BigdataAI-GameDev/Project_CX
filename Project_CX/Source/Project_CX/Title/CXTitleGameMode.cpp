// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXTitleGameMode.h"
#include "CXTitlePlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/HUD.h"

ACXTitleGameMode::ACXTitleGameMode()
{
	DefaultPawnClass = nullptr;
	HUDClass = AHUD::StaticClass();	// 빈 HUD(ACoopHUD 아님) — 메뉴는 UMG 위젯이 그린다
	PlayerControllerClass = ACXTitlePlayerController::StaticClass();
	bStartPlayersAsSpectators = false;
}

void ACXTitleGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 인게임에서 돌아온 경우: 2P 이후 로컬 플레이어 제거 → 분할 화면 없이 전체 화면 하나
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		const TArray<ULocalPlayer*> LocalPlayers = GameInstance->GetLocalPlayers();
		for (int32 i = LocalPlayers.Num() - 1; i >= 1; --i)
		{
			GameInstance->RemoveLocalPlayer(LocalPlayers[i]);
		}
	}
}

void ACXTitleGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// Super는 RestartPlayer(폰 스폰)를 호출하므로 부르지 않는다. 메뉴 생성은 ACXTitlePlayerController::BeginPlay.
}
