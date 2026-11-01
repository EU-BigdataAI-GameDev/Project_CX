// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "CXAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

/**
 *  배경음 재생(.claude/Plans/효과음 적용 기획.md 6.4).
 *
 *  수노로 만든 곡은 인트로와 끝 페이드가 있어 파일 전체를 반복하면 끊긴다.
 *  그래서 처음 한 번은 0초부터 재생하고, 이후에는 [LoopStart, LoopEnd] 구간만 오디오 컴포넌트 2개를 번갈아 쓰며 크로스페이드로 반복한다.
 *  시간은 실시간으로 잰다 — 배경음은 UI 사운드라 일시정지 중에도 계속 재생되기 때문.
 *  컴포넌트는 월드 소속이라 맵이 바뀌면 같이 사라진다(맵마다 새로 PlayMusic).
 */
UCLASS()
class UCXAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UCXAudioSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 곡 재생. LoopEnd <= 0 이면 곡 끝까지. 같은 곡이 이미 재생 중이면 무시. */
	UFUNCTION(BlueprintCallable, Category = "CX|Audio", meta = (WorldContext = "WorldContext"))
	void PlayMusic(const UObject* WorldContext, USoundBase* Music, float LoopStart, float LoopEnd, float FadeInTime = 1.5f, float CrossfadeTime = 2.0f);

	UFUNCTION(BlueprintCallable, Category = "CX|Audio")
	void StopMusic(float FadeOutTime = 0.5f);

	/** 현재 곡 재생 위치(초). 반복 구간 확인용. */
	UFUNCTION(BlueprintPure, Category = "CX|Audio")
	float GetMusicTime() const { return CurrentTime; }

private:
	bool Tick(float DeltaTime);
	UAudioComponent* MakeComponent(UWorld* World) const;

	TWeakObjectPtr<UAudioComponent> Decks[2];
	int32 ActiveDeck = 0;

	TWeakObjectPtr<USoundBase> CurrentMusic;
	float CurrentTime = 0.0f;
	float LoopStartTime = 0.0f;
	float LoopEndTime = 0.0f;
	float Crossfade = 2.0f;
	bool bPlaying = false;

	FTSTicker::FDelegateHandle TickHandle;
};
