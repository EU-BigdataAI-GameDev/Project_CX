// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalCoopGameMode.h"
#include "EngineUtils.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "CoopHUD.h"
#include "CXGameUserSettings.h"

ALocalCoopGameMode::ALocalCoopGameMode()
{
	// 분할 화면 각 칸에 자기 정보를 그리는 협동 HUD
	HUDClass = ACoopHUD::StaticClass();

	// Map1 배경음(수노, 96bpm — 2마디 5초). 반복 20~205초(74마디), 크로스페이드 1마디(2.5초)로 박자 정렬(Docs/배경음_제작기록.md)
	FCXMusicTrack Map1Track;
	Map1Track.Music = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Project_CX/Audio/BGM/SW_BGM_Map1.SW_BGM_Map1")));
	Map1Track.LoopStart = 20.0f;
	Map1Track.LoopEnd = 205.0f;
	Map1Track.FadeIn = 1.0f;
	Map1Track.Crossfade = 2.5f;
	MapMusic.Add(TEXT("Map1_SoulCity"), Map1Track);
}

void ALocalCoopGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (SecondPlayerPawnClass && !UGameplayStatics::GetPlayerController(this, 1))
	{
		// ControllerId 1 = 두 번째 입력 장치. bOffsetPlayerGamepadIds가 켜져 있으면 첫 번째 게임패드가 여기로 온다.
		UGameplayStatics::CreatePlayer(this, 1, true);
	}

	// 맵 배경음(PIE는 맵 이름에 UEDPIE_ 접두사가 붙으므로 제거하고 찾는다)
	const FString MapName = UGameplayStatics::GetCurrentLevelName(this, true);
	if (const FCXMusicTrack* Track = MapMusic.Find(MapName))
	{
		if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
		{
			Audio->PlayTrack(this, *Track);
		}
	}
}

UClass* ALocalCoopGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (SecondPlayerPawnClass && IsSisterController(InController))
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
		const bool bWantSecondStart = IsSisterController(Player);
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

bool ALocalCoopGameMode::IsSisterController(const AController* Controller)
{
	if (!Cast<APlayerController>(Controller))
	{
		return false;
	}
	const bool bSecond = IsSecondLocalPlayer(Controller);
	// 오빠 게임패드 / 여동생 키보드: 키보드 = LocalPlayer 0 → 여동생
	return UCXGameUserSettings::GetCXGameUserSettings()->IsCharacterSwapLayout() ? !bSecond : bSecond;
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
