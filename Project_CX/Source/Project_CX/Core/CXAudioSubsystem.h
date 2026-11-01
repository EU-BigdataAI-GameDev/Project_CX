// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "CXAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class USoundMix;
class USoundClass;

/**
 *  배경음 재생(.claude/Plans/효과음 적용 기획.md 6.4).
 *
 *  수노로 만든 곡은 인트로와 끝 페이드가 있어 파일 전체를 반복하면 끊긴다.
 *  그래서 처음 한 번은 0초부터 재생하고, 이후에는 [LoopStart, LoopEnd] 구간만 오디오 컴포넌트 2개를 번갈아 쓰며 크로스페이드로 반복한다.
 *  시간은 실시간으로 잰다 — 배경음은 UI 사운드라 일시정지 중에도 계속 재생되기 때문.
 *  컴포넌트는 월드 소속이라 맵이 바뀌면 같이 사라진다(맵마다 새로 PlayMusic).
 *
 *  음량 설정(효과음 적용 기획 6.4 / 설정 메뉴 기획 4.2):
 *  UCXGameUserSettings의 음량 3개를 SMix_UserVolume에 SetSoundMixClassOverride로 넣고 Push해 둔다.
 *  SCl_Master(전체, 자식 포함) × SCl_BGM / SCl_SFX(버스) → 실제 음량 = 버스 × 전체. 0 = 완전 음소거.
 *  오디오 장치가 바뀌면(PIE 시작, 맵 이동 등) 틱에서 감지해 다시 Push한다.
 */
/** 배경음 한 곡의 재생 설정. 박동이 뚜렷한 곡은 LoopEnd - LoopStart와 Crossfade를 마디 길이의 배수로 맞춰야 전환 때 박자가 어긋나지 않는다. */
USTRUCT(BlueprintType)
struct FCXMusicTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	TSoftObjectPtr<USoundBase> Music;

	/** 반복 시작(초). 첫 재생은 0초부터, 이후 여기로 돌아온다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float LoopStart = 0.0f;

	/** 반복 끝(초). 0 이하면 곡 끝. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float LoopEnd = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float FadeIn = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Music")
	float Crossfade = 2.0f;
};

UCLASS(Config = Game)
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

	/** FCXMusicTrack으로 재생(에셋은 동기 로드). */
	UFUNCTION(BlueprintCallable, Category = "CX|Audio", meta = (WorldContext = "WorldContext"))
	void PlayTrack(const UObject* WorldContext, const FCXMusicTrack& Track);

	UFUNCTION(BlueprintCallable, Category = "CX|Audio")
	void StopMusic(float FadeOutTime = 0.5f);

	/** 현재 곡 재생 위치(초). 반복 구간 확인용. */
	UFUNCTION(BlueprintPure, Category = "CX|Audio")
	float GetMusicTime() const { return CurrentTime; }

	/** 저장된 설정 음량을 사운드 믹스에 즉시 적용(설정 메뉴 슬라이더가 바뀔 때마다 호출). */
	UFUNCTION(BlueprintCallable, Category = "CX|Audio")
	void ApplyUserVolumes();

	/** 효과음 미리듣기(설정 메뉴 SFX 조절). 에셋이 비어 있으면 아무것도 안 한다. */
	void PlaySfxPreview(const UObject* WorldContext, USoundBase* Sound) const;

	/** 테스트용: 현재 믹스에 넣은 값 문자열(마스터/배경음/효과음 0~1, Push된 장치 ID). */
	UFUNCTION(BlueprintPure, Category = "CX|Audio|Debug")
	FString GetVolumeDebugState() const;

private:
	bool Tick(float DeltaTime);
	UAudioComponent* MakeComponent(UWorld* World) const;
	UWorld* GetAudioWorld() const;

	/** 설정 음량 믹스(SetSoundMixClassOverride 대상). */
	UPROPERTY(Config)
	TSoftObjectPtr<USoundMix> UserVolumeMix;

	UPROPERTY(Config)
	TSoftObjectPtr<USoundClass> MasterClass;

	UPROPERTY(Config)
	TSoftObjectPtr<USoundClass> MusicClass;

	UPROPERTY(Config)
	TSoftObjectPtr<USoundClass> SfxClass;

	/** 믹스를 Push한 오디오 장치 ID(INDEX_NONE = 아직). 장치가 바뀌면 다시 Push. */
	uint32 PushedAudioDeviceId = TNumericLimits<uint32>::Max();
	float AppliedVolumes[3] = { -1.0f, -1.0f, -1.0f };

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
