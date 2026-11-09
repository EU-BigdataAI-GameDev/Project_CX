// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXPlayerAudioComponent.h"
#include "CXSfx.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"

namespace CXPlayerAudio
{
	/** 단일 발소리(바닥 구분 없음) — SoulCity 원본을 옮기지 않고 그대로 참조(기획 5.1). */
	const TCHAR* FootstepPath = TEXT("/Game/SoulCity/Sound/Cue/Footstep_Dirt_Cue.Footstep_Dirt_Cue");
	const TCHAR* AttenuationPath = TEXT("/Game/Project_CX/Audio/Attenuation/ATT_Small.ATT_Small");
	const TCHAR* ConcurrencyPath = TEXT("/Game/Project_CX/Audio/Concurrency/SCon_Footstep.SCon_Footstep");
	const TCHAR* SoundClassPath = TEXT("/Game/Project_CX/Audio/Classes/SCl_SFX.SCl_SFX");

	/** 멈췄다 다시 움직일 때 첫 발소리를 조금 빨리 내기 위한 시작 누적값(보폭 비율). */
	constexpr float RestartStrideRatio = 0.6f;
	/** 걷기 → 낙하 전환 순간 위 속도가 이 값보다 크면 점프로 본다(턱에서 떨어진 것과 구분). */
	constexpr float JumpDetectZ = 50.0f;
}

UCXPlayerAudioComponent::UCXPlayerAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 이동이 끝난 뒤 위치로 리스너를 맞춘다
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	// 런타임에 붙는 컴포넌트라 원격 머신에도 생기도록 복제(복제 변수는 없음)
	SetIsReplicatedByDefault(true);

	FootstepSound = CXSfx::Find<USoundBase>(CXPlayerAudio::FootstepPath);
	JumpSound = FootstepSound;
	LandSound = FootstepSound;
	Attenuation = CXSfx::Find<USoundAttenuation>(CXPlayerAudio::AttenuationPath);
	FootstepConcurrency = CXSfx::Find<USoundConcurrency>(CXPlayerAudio::ConcurrencyPath);
	SoundClass = CXSfx::Find<USoundClass>(CXPlayerAudio::SoundClassPath);
}

UCXPlayerAudioComponent* UCXPlayerAudioComponent::EnsureOn(APawn* Pawn)
{
	if (!Pawn)
	{
		return nullptr;
	}
	if (UCXPlayerAudioComponent* Existing = Pawn->FindComponentByClass<UCXPlayerAudioComponent>())
	{
		return Existing;
	}
	// BP_Brother처럼 C++ 부모가 없는 폰도 에셋을 고치지 않고 런타임에 붙인다
	UCXPlayerAudioComponent* Component = NewObject<UCXPlayerAudioComponent>(Pawn, TEXT("CXPlayerAudio"));
	Pawn->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

void UCXPlayerAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	StrideAccum = StrideLength * CXPlayerAudio::RestartStrideRatio;
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->MovementModeChangedDelegate.AddUniqueDynamic(this, &UCXPlayerAudioComponent::HandleMovementModeChanged);
	}
}

void UCXPlayerAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->MovementModeChangedDelegate.RemoveDynamic(this, &UCXPlayerAudioComponent::HandleMovementModeChanged);
	}
	ClearListener();
	Super::EndPlay(EndPlayReason);
}

void UCXPlayerAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
	UpdateFootsteps(Character, DeltaTime);
	UpdateListener(Character);
}

// ---------------------------------------------------------------- 발소리 / 점프 / 착지

void UCXPlayerAudioComponent::UpdateFootsteps(ACharacter* Character, float DeltaTime)
{
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement)
	{
		return;
	}
	const FVector Velocity = Character->GetVelocity();

	if (Movement->IsFalling())
	{
		PeakFallSpeed = FMath::Max(PeakFallSpeed, -Velocity.Z);
		return;
	}

	const float Speed = Velocity.Size2D();
	if (!Movement->IsMovingOnGround() || Speed < MinFootstepSpeed)
	{
		// 멈춤(또는 해킹 중 이동 잠금) — 다시 움직이면 첫 발이 금방 나오게
		StrideAccum = FMath::Max(StrideAccum, StrideLength * CXPlayerAudio::RestartStrideRatio);
		return;
	}

	StrideAccum += Speed * DeltaTime;
	if (StrideAccum >= StrideLength)
	{
		StrideAccum = FMath::Fmod(StrideAccum, StrideLength);
		PlayPlayerSound(FootstepSound, Character, FootstepVolume, 1.0f, FootstepConcurrency);
		++FootstepCount;
	}
}

void UCXPlayerAudioComponent::HandleMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	const bool bWasGrounded = PrevMovementMode == MOVE_Walking || PrevMovementMode == MOVE_NavWalking;
	if (Movement->IsFalling() && bWasGrounded)
	{
		// DoJump는 위 속도를 넣은 뒤 Falling으로 바꾸므로, 이 순간 위로 움직이면 점프(턱에서 떨어진 건 0 근처)
		PeakFallSpeed = 0.0f;
		bJumped = Character->GetVelocity().Z > CXPlayerAudio::JumpDetectZ;
		if (bJumped)
		{
			// 발소리 한 번과 구분되도록 두 겹: 디딤(본음) + 높은 스침(위층)
			PlayPlayerSound(JumpSound, Character, JumpVolume, JumpPitch, nullptr);
			PlayPlayerSound(JumpSound, Character, JumpVolume * 0.7f, JumpPitch * 1.35f, nullptr);
			++JumpCount;
		}
		return;
	}

	if (PrevMovementMode == MOVE_Falling && Movement->IsMovingOnGround())
	{
		// 착지 시점엔 엔진이 이미 Z 속도를 0으로 만들었으므로 낙하 중 기록한 최대 낙하 속도를 쓴다
		const float FallSpeed = PeakFallSpeed;
		if (bJumped || FallSpeed >= MinLandingSpeed)
		{
			const float Volume = FallSpeed >= HardLandingSpeed ? LandVolumeHard : LandVolumeSoft;
			PlayPlayerSound(LandSound, Character, Volume, LandPitch, FootstepConcurrency);
			++LandCount;
			LastLandSpeed = FallSpeed;
		}
		// 착지음이 발 한 번을 대신 — 착지 직후 발소리가 바로 겹치지 않게
		StrideAccum = 0.0f;
		PeakFallSpeed = 0.0f;
		bJumped = false;
	}
}

void UCXPlayerAudioComponent::PlayPlayerSound(USoundBase* Sound, const ACharacter* Character, float Volume, float Pitch, USoundConcurrency* Concurrency) const
{
	if (!Sound || !Character)
	{
		return;
	}
	// 발 위치(캡슐 바닥)
	const float HalfHeight = Character->GetCapsuleComponent() ? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
	const FVector FootLocation = Character->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
	CXSfx::PlayAtLocation(this, Sound, FootLocation, Volume, Pitch, Attenuation, Concurrency, SoundClass);
}

// ---------------------------------------------------------------- 리스너

void UCXPlayerAudioComponent::UpdateListener(ACharacter* Character)
{
	APlayerController* PC = Cast<APlayerController>(Character->GetController());
	if (!bOverrideListener || !PC || !PC->IsLocalController())
	{
		ClearListener();
		return;
	}
	if (ListenerController.IsValid() && ListenerController.Get() != PC)
	{
		ClearListener();
	}
	// 위치 = 자기 폰 + 위로 ListenerHeight, 회전 = 카메라(좌우 패닝이 화면 방향과 맞도록)
	const FRotator ViewRotation = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation() : PC->GetControlRotation();
	PC->SetAudioListenerOverride(nullptr, Character->GetActorLocation() + FVector(0.0f, 0.0f, ListenerHeight), ViewRotation);
	ListenerController = PC;
}

void UCXPlayerAudioComponent::ClearListener()
{
	if (APlayerController* PC = ListenerController.Get())
	{
		PC->ClearAudioListenerOverride();
	}
	ListenerController.Reset();
}

FString UCXPlayerAudioComponent::GetDebugState() const
{
	FVector ListenerLocation = FVector::ZeroVector;
	FVector Front = FVector::ZeroVector;
	FVector Right = FVector::ZeroVector;
	if (const APlayerController* PC = ListenerController.Get())
	{
		PC->GetAudioListenerPosition(ListenerLocation, Front, Right);
	}
	return FString::Printf(TEXT("steps=%d jumps=%d lands=%d lastLandSpeed=%.0f listener=%s front=%s override=%d"),
		FootstepCount, JumpCount, LandCount, LastLandSpeed, *ListenerLocation.ToCompactString(), *Front.ToCompactString(), ListenerController.IsValid() ? 1 : 0);
}
