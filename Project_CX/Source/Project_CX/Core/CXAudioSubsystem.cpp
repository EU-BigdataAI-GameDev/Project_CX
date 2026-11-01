// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

UCXAudioSubsystem* UCXAudioSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UCXAudioSubsystem>() : nullptr;
}

void UCXAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
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

bool UCXAudioSubsystem::Tick(float DeltaTime)
{
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
