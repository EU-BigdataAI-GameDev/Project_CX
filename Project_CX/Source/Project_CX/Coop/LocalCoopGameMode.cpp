// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalCoopGameMode.h"
#include "EngineUtils.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "CoopHUD.h"
#include "CXGameUserSettings.h"
#include "CXSaveGame.h"
#include "CXMenuSubsystem.h"
#include "CXGameClearWidget.h"
#include "CoopInteractComponent.h"
#include "CXPlayerAudioComponent.h"
#include "CXCoopExitDoor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

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
	// 2026-11-02 맵 이름 변경: 실제 Map1 레벨은 Map1_SoulCity1(Map1_SoulCity는 리디렉터) — 둘 다 둔다
	MapMusic.Add(TEXT("Map1_SoulCity1"), Map1Track);
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

void ALocalCoopGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);
	UCoopInteractComponent::EnsureOn(PlayerPawn);
	// 공용 발소리·점프·착지 + 리스너 위치(효과음 적용 기획 6.1, 6.3)
	UCXPlayerAudioComponent::EnsureOn(PlayerPawn);
}

// ---------------------------------------------------------------- 게임 클리어

void ALocalCoopGameMode::NotifyGameCleared()
{
	if (bGameCleared)
	{
		return;
	}
	bGameCleared = true;

	// 클리어하면 세이브 초기화 → 다음 시작화면에는 NEW GAME만
	UCXSaveGame::DeleteSlot();

	UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this);
	if (Menus)
	{
		// 클리어화면(진입 연출 포함) 동안 ESC/Start로 일시정지 메뉴가 열리지 않게. 맵 이동 시 서브시스템이 자동 해제.
		Menus->SetMenuBlocked(true);
	}

	// 게임플레이 입력 차단(두 폰 모두) + 제자리 정지
	APlayerController* FirstLocal = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}
		if (!FirstLocal || (PC->GetLocalPlayer() && PC->GetLocalPlayer()->GetLocalPlayerIndex() == 0))
		{
			FirstLocal = PC;
		}
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->DisableInput(PC);
			if (ACharacter* Character = Cast<ACharacter>(Pawn))
			{
				Character->GetCharacterMovement()->StopMovementImmediately();
			}
		}
		PC->FlushPressedKeys();
	}

	// 인게임 BGM을 내려 정지 + 클리어 징글 슬롯
	if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
	{
		Audio->StopMusic(ClearMusicFadeTime);
	}
	if (ClearJingle)
	{
		UGameplayStatics::PlaySound2D(this, ClearJingle, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
	}

	// 클리어화면: 화면 전체에 하나(1P 컨트롤러 소유, 누구나 조작). 위젯이 딤 0.5초 → 패널 페이드인을 직접 연출한다.
	if (Menus && FirstLocal)
	{
		Menus->PushMenu(UCXGameClearWidget::StaticClass(), FirstLocal, 200);
	}
	// 딤이 다 깔리면 월드 정지(부활 카운트다운·해킹 타이머 포함)
	GetWorldTimerManager().SetTimer(ClearPauseTimer, this, &ALocalCoopGameMode::PauseForGameClear, FMath::Max(0.01f, ClearPauseDelay), false);
}

void ALocalCoopGameMode::PauseForGameClear()
{
	UGameplayStatics::SetGamePaused(this, true);
}

void ALocalCoopGameMode::CXSpawnExitDoor(float OffsetY, float OffsetX)
{
	const ACXCoopExitDoor* Door = ACXCoopExitDoor::DebugSpawnExitDoor(this, OffsetY, OffsetX);
	UE_LOG(LogTemp, Log, TEXT("CXSpawnExitDoor: %s"), Door ? *Door->GetActorLocation().ToString() : TEXT("failed"));
}
