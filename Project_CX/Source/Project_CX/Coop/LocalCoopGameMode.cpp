// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalCoopGameMode.h"
#include "EngineUtils.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "CoopHUD.h"

ALocalCoopGameMode::ALocalCoopGameMode()
{
	// 분할 화면 각 칸에 자기 정보를 그리는 협동 HUD
	HUDClass = ACoopHUD::StaticClass();
}

void ALocalCoopGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (SecondPlayerPawnClass && !UGameplayStatics::GetPlayerController(this, 1))
	{
		// ControllerId 1 = 두 번째 입력 장치. bOffsetPlayerGamepadIds가 켜져 있으면 첫 번째 게임패드가 여기로 온다.
		UGameplayStatics::CreatePlayer(this, 1, true);
	}
}

UClass* ALocalCoopGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (SecondPlayerPawnClass && IsSecondLocalPlayer(InController))
	{
		return SecondPlayerPawnClass;
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

AActor* ALocalCoopGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (!SecondPlayerStartTag.IsNone())
	{
		// 태그로 1P/2P 시작 지점을 나눈다. 기본 구현은 빈 PlayerStart 중 아무거나 고르므로 1P가 2P 자리에 설 수 있다.
		const bool bWantSecondStart = IsSecondLocalPlayer(Player);
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if ((It->PlayerStartTag == SecondPlayerStartTag) == bWantSecondStart)
			{
				return *It;
			}
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

bool ALocalCoopGameMode::IsSecondLocalPlayer(const AController* Controller)
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController)
	{
		return false;
	}
	if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		return LocalPlayer->GetLocalPlayerIndex() == 1;
	}
	// Login 중(InitNewPlayer → 시작 지점 캐시)에는 아직 LocalPlayer가 붙지 않는다.
	// 이 시점엔 먼저 접속한 1P 컨트롤러가 이미 월드에 있으므로, 첫 컨트롤러가 아니면 2P로 본다.
	const UWorld* World = PlayerController->GetWorld();
	const APlayerController* FirstPlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return FirstPlayerController && FirstPlayerController != PlayerController;
}
