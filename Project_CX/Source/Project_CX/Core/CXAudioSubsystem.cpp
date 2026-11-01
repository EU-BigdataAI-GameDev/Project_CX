// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXAudioSubsystem.h"
#include "CXGameUserSettings.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

namespace CXAudio
{
	// 기본 에셋 경로(DefaultGame.ini [/Script/Project_CX.CXAudioSubsystem]에서 바꿀 수 있다)
	const TCHAR* UserVolumeMixPath = TEXT("/Game/Project_CX/Audio/Mixes/SMix_UserVolume.SMix_UserVolume");
	const TCHAR* MasterClassPath = TEXT("/Game/Project_CX/Audio/Classes/SCl_Master.SCl_Master");
	const TCHAR* MusicClassPath = TEXT("/Game/Project_CX/Audio/Classes/SCl_BGM.SCl_BGM");
	const TCHAR* SfxClassPath = TEXT("/Game/Project_CX/Audio/Classes/SCl_SFX.SCl_SFX");
}

UCXAudioSubsystem* UCXAudioSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UCXAudioSubsystem>() : nullptr;
}

void UCXAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UserVolumeMix.IsNull()) { UserVolumeMix = TSoftObjectPtr<USoundMix>(FSoftObjectPath(CXAudio::UserVolumeMixPath)); }
	if (MasterClass.IsNull()) { MasterClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(CXAudio::MasterClassPath)); }
	if (MusicClass.IsNull()) { MusicClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(CXAudio::MusicClassPath)); }
	if (SfxClass.IsNull()) { SfxClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(CXAudio::SfxClassPath)); }
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCXAudioSubsystem::Tick));
}

void UCXAudioSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

UAudioComponent* UCXAudioSubsystem::MakeComponent(UWorld* World) const
{
	UAudioComponent* Comp = UGameplayStatics::CreateSound2D(World, CurrentMusic.Get(), 1.0f, 1.0f, 0.0f, nullptr, false, false);
	if (Comp)
	{
		// 일시정지 중에도 재생(ESC 메뉴에서는 끊지 않고 덕킹만)
		Comp->bIsUISound = true;
	}
	return Comp;
}

void UCXAudioSubsystem::PlayMusic(const UObject* WorldContext, USoundBase* Music, float LoopStart, float LoopEnd, float FadeInTime, float CrossfadeTime)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || !Music)
	{
		return;
	}
	if (bPlaying && CurrentMusic.Get() == Music && Decks[ActiveDeck].IsValid())
	{
		return;
	}
	StopMusic(0.3f);

	CurrentMusic = Music;
	const float Duration = Music->GetDuration();
	LoopEndTime = (LoopEnd > 0.0f) ? FMath::Min(LoopEnd, Duration) : Duration;
	LoopStartTime = FMath::Clamp(LoopStart, 0.0f, FMath::Max(0.0f, LoopEndTime - 1.0f));
	Crossfade = FMath::Clamp(CrossfadeTime, 0.1f, (LoopEndTime - LoopStartTime) * 0.5f);

	for (TWeakObjectPtr<UAudioComponent>& Deck : Decks)
	{
		Deck = MakeComponent(World);
	}
	ActiveDeck = 0;
	CurrentTime = 0.0f;
	bPlaying = true;
	if (UAudioComponent* Comp = Decks[0].Get())
	{
		Comp->FadeIn(FadeInTime, 1.0f, 0.0f);
	}
}

void UCXAudioSubsystem::StopMusic(float FadeOutTime)
{
	for (TWeakObjectPtr<UAudioComponent>& Deck : Decks)
	{
		if (UAudioComponent* Comp = Deck.Get())
		{
			if (FadeOutTime > 0.0f && Comp->IsPlaying())
			{
				Comp->FadeOut(FadeOutTime, 0.0f);
			}
			else
			{
				Comp->Stop();
			}
		}
		Deck.Reset();
	}
	bPlaying = false;
	CurrentMusic.Reset();
}

void UCXAudioSubsystem::PlayTrack(const UObject* WorldContext, const FCXMusicTrack& Track)
{
	PlayMusic(WorldContext, Track.Music.LoadSynchronous(), Track.LoopStart, Track.LoopEnd, Track.FadeIn, Track.Crossfade);
}

UWorld* UCXAudioSubsystem::GetAudioWorld() const
{
	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	return (World && !World->bIsTearingDown) ? World : nullptr;
}

void UCXAudioSubsystem::ApplyUserVolumes()
{
	UWorld* World = GetAudioWorld();
	FAudioDeviceHandle Device = World ? World->GetAudioDevice() : FAudioDeviceHandle();
	if (!Device.IsValid())
	{
		return;
	}
	USoundMix* Mix = UserVolumeMix.LoadSynchronous();
	if (!Mix)
	{
		// 에셋이 없으면 이 장치에서는 다시 시도하지 않는다(틱마다 로드 시도 방지)
		UE_CLOG(PushedAudioDeviceId != Device.GetDeviceID(), LogTemp, Warning, TEXT("CXAudioSubsystem: 설정 음량 믹스(%s)를 찾을 수 없습니다."), *UserVolumeMix.ToString());
		PushedAudioDeviceId = Device.GetDeviceID();
		return;
	}
	const UCXGameUserSettings* Settings = UCXGameUserSettings::GetCXGameUserSettings();
	const float Volumes[3] = {
		Settings->GetMasterVolume() / 100.0f,
		Settings->GetMusicVolume() / 100.0f,
		Settings->GetSfxVolume() / 100.0f };
	USoundClass* Classes[3] = { MasterClass.LoadSynchronous(), MusicClass.LoadSynchronous(), SfxClass.LoadSynchronous() };

	for (int32 i = 0; i < 3; ++i)
	{
		if (Classes[i])
		{
			// 자식 클래스까지: 전체(Master) 값이 BGM/SFX에 곱해진다. 페이드 없이 즉시(미리듣기).
			UGameplayStatics::SetSoundMixClassOverride(World, Mix, Classes[i], Volumes[i], 1.0f, 0.0f, true);
		}
		AppliedVolumes[i] = Volumes[i];
	}
	if (PushedAudioDeviceId != Device.GetDeviceID())
	{
		// 이 장치에 처음 — 게임 시작 시 1회 Push하고 계속 유지(Pop하지 않음)
		UGameplayStatics::PushSoundMixModifier(World, Mix);
		PushedAudioDeviceId = Device.GetDeviceID();
	}
}

void UCXAudioSubsystem::PlaySfxPreview(const UObject* WorldContext, USoundBase* Sound) const
{
	if (Sound && WorldContext)
	{
		// UI 사운드: 일시정지 중에도 재생
		UGameplayStatics::PlaySound2D(WorldContext, Sound, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
	}
}

FString UCXAudioSubsystem::GetVolumeDebugState() const
{
	return FString::Printf(TEXT("master=%.2f music=%.2f sfx=%.2f device=%d mix=%s"), AppliedVolumes[0], AppliedVolumes[1], AppliedVolumes[2],
		PushedAudioDeviceId == TNumericLimits<uint32>::Max() ? -1 : static_cast<int32>(PushedAudioDeviceId),
		UserVolumeMix.IsValid() ? *UserVolumeMix->GetName() : TEXT("none"));
}

bool UCXAudioSubsystem::Tick(float DeltaTime)
{
	// 오디오 장치가 바뀌었으면(PIE 시작/맵 이동) 설정 음량 믹스를 다시 Push
	if (const UWorld* World = GetAudioWorld())
	{
		const FAudioDeviceHandle Device = World->GetAudioDevice();
		if (Device.IsValid() && Device.GetDeviceID() != PushedAudioDeviceId)
		{
			ApplyUserVolumes();
		}
	}

	if (!bPlaying)
	{
		return true;
	}
	UAudioComponent* Active = Decks[ActiveDeck].Get();
	if (!Active)
	{
		// 맵 이동으로 컴포넌트가 사라짐
		bPlaying = false;
		return true;
	}

	CurrentTime += DeltaTime;

	// 음량 0(MUTE)에서 엔진이 무음 사운드를 정지시켰다면, 음량이 돌아왔을 때 원래 위치부터 다시 재생한다.
	// (에셋은 VirtualizationMode = PlayWhenSilent로 두지만, 그렇지 않은 곡을 넣어도 끊기지 않도록 하는 안전장치)
	if (!Active->IsPlaying() && AppliedVolumes[0] > 0.0f && AppliedVolumes[1] > 0.0f)
	{
		Active->FadeIn(0.5f, 1.0f, FMath::Min(CurrentTime, LoopEndTime));
	}

	if (CurrentTime >= LoopEndTime - Crossfade)
	{
		// 다음 덱을 반복 시작점에서 페이드인, 현재 덱은 페이드아웃
		const int32 Next = 1 - ActiveDeck;
		UAudioComponent* NextComp = Decks[Next].Get();
		if (!NextComp)
		{
			NextComp = MakeComponent(Active->GetWorld());
			Decks[Next] = NextComp;
		}
		if (NextComp)
		{
			NextComp->Stop();
			NextComp->FadeIn(Crossfade, 1.0f, LoopStartTime);
		}
		Active->FadeOut(Crossfade, 0.0f);
		ActiveDeck = Next;
		CurrentTime = LoopStartTime;
	}
	return true;
}
