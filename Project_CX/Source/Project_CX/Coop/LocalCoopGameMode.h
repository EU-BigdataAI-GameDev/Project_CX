// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CXAudioSubsystem.h"
#include "CXPlayerLifeComponent.h"
#include "LocalCoopGameMode.generated.h"

class USoundBase;

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
 *
 *  게임 클리어(게임 클리어 화면 기획): 출구 문(ACXCoopExitDoor)이 다 열리면 NotifyGameCleared — 서버 권위, 1회만.
 *
 *  사망/부활/게임 오버(게임 오버화면 기획 2장, 9장): NotifyPlayerDied 하나로 판정 — 서버 권위.
 *  - 한 명 사망 + 파트너 생존 → 10초 부활 대기(월드 타이머 — 일시정지 중 멈춤). 1초 뒤 사망자 카메라는 파트너를 따라간다.
 *  - 부활 대기 중 나머지도 사망 → 게임 오버(슬로우 0.5초 → 월드 정지 + 분할 끔 → UCXGameOverWidget).
 *  - 클리어 후에는 사망 무시, 게임 오버 후에는 클리어 무시.
 */
UCLASS()
class ALocalCoopGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ALocalCoopGameMode();

	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** 스폰된 플레이어 폰에 공용 상호작용(F/X)·플레이어 소리 컴포넌트를 붙인다(오빠 BP는 C++ 부모가 없어 런타임에 추가). */
	virtual void SetPlayerDefaults(APawn* PlayerPawn) override;

	/**
	 *  게임 클리어 판정(서버에서만, 1회). 세이브 삭제 → ESC 메뉴 차단 → 게임플레이 입력 차단 + BGM 0.5초 페이드아웃
	 *  → 클리어화면 표시(딤 0.5초) → 0.5초 뒤 월드 일시정지(게임 클리어 화면 기획 2.2, 9장).
	 */
	UFUNCTION(BlueprintCallable, Category = "Local Coop|Clear")
	void NotifyGameCleared();

	UFUNCTION(BlueprintPure, Category = "Local Coop|Clear")
	bool IsGameCleared() const { return bGameCleared; }

	/**
	 *  플레이어 사망 판정(서버에서만). 체력 시스템·낙사(UCXPlayerLifeComponent)·디버그 명령이 모두 여기로 온다.
	 *  파트너가 살아 있으면 RespawnDelay초 뒤 파트너 근처에 부활, 아니면 게임 오버.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Local Coop|Life")
	void NotifyPlayerDied(AController* Controller, ECXDeathCause Cause = ECXDeathCause::Generic);

	UFUNCTION(BlueprintPure, Category = "Local Coop|Life")
	bool IsGameOver() const { return bGameOver; }

	/** 테스트용: 로컬 플레이어 인덱스(0/1)로 사망 처리. 성공하면 true. */
	UFUNCTION(BlueprintCallable, Category = "Local Coop|Debug")
	bool DebugKillPlayer(int32 LocalPlayerIndex);

	/** 테스트용: 두 플레이어 생존 상태 문자열. */
	UFUNCTION(BlueprintPure, Category = "Local Coop|Debug")
	FString GetLifeDebugState() const;

	/** 테스트용 콘솔 명령: 플레이어 사망. 인자 0/1(로컬 플레이어) 또는 brother/sister. 예) CXKillPlayer sister */
	UFUNCTION(Exec)
	void CXKillPlayer(const FString& Who = TEXT("0"));

	/** 테스트용 콘솔 명령: 첫 플레이어 근처에 출구 문을 런타임 스폰(레벨에 저장하지 않음). 예) CXSpawnExitDoor 500 150 */
	UFUNCTION(Exec)
	void CXSpawnExitDoor(float OffsetY = 500.0f, float OffsetX = 150.0f);

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

	/** 클리어 시 BGM 페이드아웃 시간 / 딤이 깔린 뒤 월드를 멈추기까지의 시간. */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|Clear")
	float ClearMusicFadeTime = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|Clear")
	float ClearPauseDelay = 0.5f;

	/** 클리어 전용 짧은 징글(에셋이 없으면 무음). */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|Clear")
	TObjectPtr<USoundBase> ClearJingle;

	/** 사망 후 부활까지(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|Life")
	float RespawnDelay = 10.0f;

	/** 부활 직후 무적(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|Life")
	float RespawnInvulnerableTime = 2.0f;

	/** 사망자 카메라가 그 자리에 머무는 시간 → 이후 파트너로 전환(블렌드 시간). */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|Life")
	float DeadViewHoldTime = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|Life")
	float DeadViewBlendTime = 0.5f;

	/** 이 시간(초) 안에 두 명이 쓰러지면 동시 사망(게임 오버화면에 두 이름). */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|Life")
	float SimultaneousDeathWindow = 0.1f;

	/** 게임 오버 진입 슬로우(실시간 초)와 타임 딜레이션. */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|GameOver")
	float GameOverSlowMoTime = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|GameOver")
	float GameOverTimeDilation = 0.3f;

	/** 게임 오버 진입 시 BGM 컷(페이드 시간). */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|GameOver")
	float GameOverMusicFadeTime = 0.2f;

	/** 저음 신호 끊김 효과(에셋 없음 → 무음). */
	UPROPERTY(EditDefaultsOnly, Category = "Local Coop|GameOver")
	TObjectPtr<USoundBase> GameOverSound;

private:
	void PauseForGameClear();

	void RespawnPlayer(TWeakObjectPtr<AController> WeakController);
	void SwitchDeadView(TWeakObjectPtr<AController> WeakController);
	void TriggerGameOver(bool bBrotherFell, bool bSisterFell);
	void EnterGameOverPause();
	bool IsControllerAlive(const AController* Controller) const;
	void ClearLifeTimers();

	bool bGameOver = false;
	FTimerHandle GameOverPauseTimer;

	/** 부활 대기 중인 컨트롤러 → 부활/카메라 전환 타이머. */
	TMap<TWeakObjectPtr<AController>, FTimerHandle> RespawnTimers;
	TMap<TWeakObjectPtr<AController>, FTimerHandle> DeadViewTimers;

	/** 폰이 파괴된 채 사망한 컨트롤러(안전망 경로) — 부활 시 새 폰을 스폰한다. */
	TSet<TWeakObjectPtr<AController>> DeadWithoutPawn;

	TWeakObjectPtr<AController> LastDeathController;
	double LastDeathTime = -1000.0;

	bool bGameCleared = false;
	FTimerHandle ClearPauseTimer;

	static bool IsSecondLocalPlayer(const AController* Controller);

	/** 이 컨트롤러가 여동생(2P) 폰을 맡는지 — 기본은 2번째 로컬 플레이어, 장치 스왑 조합이면 반대. */
	static bool IsSisterController(const AController* Controller);
};
