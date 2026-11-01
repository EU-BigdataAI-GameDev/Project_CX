// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CXAudioSubsystem.h"
#include "LocalCoopGameMode.generated.h"

/**
 *  로컬 2인 협동 게임모드 — 한 대의 PC에서 1P(키보드/마우스, 오빠)와 2P(게임패드, 여동생)가 동시에 플레이한다.
 *
 *  - BeginPlay에서 2P 로컬 플레이어를 자동 생성한다(화면은 엔진 기본 스플릿스크린).
 *  - 1P는 DefaultPawnClass, 2P는 SecondPlayerPawnClass로 스폰된다.
 *  - 2P는 PlayerStartTag가 SecondPlayerStartTag인 PlayerStart에서, 1P는 그 외 PlayerStart에서 시작한다.
 *
 *  게임패드가 1P가 아닌 2P로 가려면 프로젝트 설정 Maps & Modes > Local Multiplayer의
 *  "Skip Assigning Gamepad to Player 1"(bOffsetPlayerGamepadIds)이 켜져 있어야 한다.
 *
 *  조작 장치 설정(설정 메뉴 기획 8장)이 "오빠 게임패드 / 여동생 키보드"면 키보드가 항상 LocalPlayer 0으로 들어오므로
 *  LocalPlayer 0이 여동생 폰, LocalPlayer 1(게임패드)이 오빠 폰을 받는다(분할 칸 순서도 바뀐다 — 기획상 허용).
 */
UCLASS()
class ALocalCoopGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALocalCoopGameMode();

	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

protected:
	virtual void BeginPlay() override;

	/** 2P(게임패드) 로컬 플레이어가 조작할 폰. 비어 있으면 2P를 만들지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop")
	TSubclassOf<APawn> SecondPlayerPawnClass;

	/** 2P 전용 PlayerStart를 구분하는 태그. */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop")
	FName SecondPlayerStartTag = TEXT("Sister");

	/** 맵 이름(접두사 없는 짧은 이름, 예: Map1_SoulCity) → 그 맵의 배경음. 없는 맵은 배경음 없이 시작. */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|Music")
	TMap<FString, FCXMusicTrack> MapMusic;

private:
	static bool IsSecondLocalPlayer(const AController* Controller);

	/** 이 컨트롤러가 여동생(2P) 폰을 맡는지 — 기본은 2번째 로컬 플레이어, 장치 스왑 조합이면 반대. */
	static bool IsSisterController(const AController* Controller);
};
