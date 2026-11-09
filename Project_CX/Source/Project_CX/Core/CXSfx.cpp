// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXSfx.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

USoundBase* CXSfx::PickRandom(const TArray<TObjectPtr<USoundBase>>& Sounds)
{
	if (Sounds.Num() == 0)
	{
		return nullptr;
	}
	return Sounds[FMath::RandHelper(Sounds.Num())];
}

void CXSfx::PlayAtLocation(const UObject* WorldContext, USoundBase* Sound, const FVector& Location, float Volume, float Pitch,
	USoundAttenuation* Attenuation, USoundConcurrency* Concurrency, USoundClass* ClassOverride)
{
	if (!Sound)
	{
		return;
	}
	if (!ClassOverride)
	{
		UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.0f, Attenuation, Concurrency);
		return;
	}

	// 사운드 클래스 덮어쓰기는 재생 전에 컴포넌트에 넣어야 하므로 UGameplayStatics::SpawnSoundAtLocation과 같은 과정을 직접 밟는다
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || !GEngine->UseSound() || !World->bAllowAudioPlayback || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}
	FAudioDevice::FCreateComponentParams Params(World);
	Params.SetLocation(Location);
	Params.AttenuationSettings = Attenuation;
	if (Concurrency)
	{
		Params.ConcurrencySet.Add(Concurrency);
	}
	if (UAudioComponent* Component = FAudioDevice::CreateComponent(Sound, Params))
	{
		Component->SetWorldLocation(Location);
		Component->SetVolumeMultiplier(Volume);
		Component->SetPitchMultiplier(Pitch);
		Component->SoundClassOverride = ClassOverride;
		Component->bAllowSpatialization = Params.ShouldUseAttenuation();
		Component->bAutoDestroy = true;
		Component->bStopWhenOwnerDestroyed = false;
		Component->Play();
	}
}
