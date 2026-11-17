// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "CXPlayerAudioComponent.generated.h"

class ACharacter;
class APawn;
class APlayerController;
class USoundBase;
class USoundAttenuation;
class USoundConcurrency;
class USoundClass;

/**
 *  두 캐릭터 공용 플레이어 소리(.claude/Plans/효과음 적용 기획.md 2.1, 6.1, 6.3).
 *
 *  - 발소리: 애니메이션 에셋을 고치지 않고, 땅 위에서 움직인 수평 거리가 StrideLength를 넘을 때마다 발소리 큐를 재생한다.
 *    (기획 6.1의 AnimNotify 대신 — 오빠는 BP(부모 Character), 여동생은 네이티브 AnimInstance라 같은 방식으로 붙일 수 있게 거리 기반으로 한다)
 *  - 점프/착지: ACharacter::MovementModeChangedDelegate로 감지(BP 수정 없음). 착지는 낙하 속도로 강/약.
 *    발소리·점프·착지는 오빠와 여동생이 같은 에셋·같은 설정을 쓴다(SoulCity Footstep_Dirt_Cue를 피치·음량만 바꿔 재사용).
 *  - 리스너: 이 폰을 조작하는 로컬 플레이어 컨트롤러의 오디오 리스너를 "폰 위치 + 위로 ListenerHeight, 회전은 카메라"로 덮어쓴다(6.3).
 *    사이드뷰 카메라가 캐릭터 약 13m 뒤에 있어 모든 소리가 멀게 들리는 문제를 막는다. 컨트롤러별 설정이라 분할 화면 2인에서도 각자 적용된다.
 *    카메라 자체는 건드리지 않는다.
 *
 *  ALocalCoopGameMode::SetPlayerDefaults가 두 폰에 런타임으로 붙인다(UCoopInteractComponent::EnsureOn과 같은 방식).
 *  복제 컴포넌트라 listen server의 원격 머신에도 생기고, 발소리는 각 머신이 복제된 이동 상태로 직접 재생한다(RPC 없음, 기획 6.5).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UCXPlayerAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXPlayerAudioComponent();

	/** 폰에 이 컴포넌트가 없으면 붙인다(서버/스탠드얼론에서 호출). */
	static UCXPlayerAudioComponent* EnsureOn(APawn* Pawn);

	/** 테스트용: 지금까지 재생한 발소리/점프/착지 횟수와 현재 리스너 위치. */
	UFUNCTION(BlueprintPure, Category = "CX|Audio|Debug")
	FString GetDebugState() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ------------------------------------------------------------ 사운드 슬롯(비어 있으면 무음)

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Player")
	TObjectPtr<USoundBase> FootstepSound;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Player")
	TObjectPtr<USoundBase> JumpSound;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Player")
	TObjectPtr<USoundBase> LandSound;

	/** 플레이어 소리에 적용할 감쇠/동시 재생/사운드 클래스(원본 큐 설정 대신 — SoulCity 에셋은 고치지 않는다). */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Player")
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Player")
	TObjectPtr<USoundConcurrency> FootstepConcurrency;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Player")
	TObjectPtr<USoundClass> SoundClass;

	// ------------------------------------------------------------ 발소리 튜닝

	/** 발소리 한 번 사이의 이동 거리(cm). 최고 속도 450cm/s 기준 약 초당 2.8걸음. */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Footstep")
	float StrideLength = 160.0f;

	/** 이 수평 속도(cm/s) 미만이면 걷는 것으로 보지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Footstep")
	float MinFootstepSpeed = 120.0f;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Footstep")
	float FootstepVolume = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Jump")
	float JumpVolume = 1.4f;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Jump")
	float JumpPitch = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Land")
	float LandPitch = 0.85f;

	/** 이 낙하 속도(cm/s) 이상이면 강한 착지(LandVolumeHard), 아니면 LandVolumeSoft. */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Land")
	float HardLandingSpeed = 800.0f;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Land")
	float LandVolumeHard = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Land")
	float LandVolumeSoft = 0.6f;

	/** 점프 없이 떨어졌을 때 이 낙하 속도 미만이면 착지음을 내지 않는다(계단·작은 턱, 스폰 직후 착지). */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Land")
	float MinLandingSpeed = 300.0f;

	// ------------------------------------------------------------ 리스너(6.3)

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Listener")
	bool bOverrideListener = true;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Audio|Listener")
	float ListenerHeight = 100.0f;

private:
	UFUNCTION()
	void HandleMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode);

	void UpdateFootsteps(ACharacter* Character, float DeltaTime);
	void UpdateListener(ACharacter* Character);
	void ClearListener();
	void PlayPlayerSound(USoundBase* Sound, const ACharacter* Character, float Volume, float Pitch, USoundConcurrency* Concurrency) const;

	TWeakObjectPtr<APlayerController> ListenerController;

	float StrideAccum = 0.0f;
	float PeakFallSpeed = 0.0f;
	bool bJumped = false;

	int32 FootstepCount = 0;
	int32 JumpCount = 0;
	int32 LandCount = 0;
	float LastLandSpeed = 0.0f;
};
