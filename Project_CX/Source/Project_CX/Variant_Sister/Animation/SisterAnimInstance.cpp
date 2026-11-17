// Copyright Epic Games, Inc. All Rights Reserved.

#include "SisterAnimInstance.h"
#include "SisterCharacter.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace SisterAnim
{
	enum class EState : uint8
	{
		Idle,
		Run,
		JumpStart,
		JumpEnd
	};

	constexpr float BlendTime = 0.15f;
	constexpr float RunSpeedThreshold = 10.0f;

	// 착지 직후 바로 달리기로 넘어가면 JumpEnd가 안 보이므로, 최소한 이만큼은 착지 모션을 보여준다.
	constexpr float MinLandingTimeBeforeRun = 0.15f;

	struct FSequencePlayer
	{
		const UAnimSequence* Anim = nullptr;
		float Time = 0.0f;
		bool bLoop = false;

		void Advance(float DeltaSeconds)
		{
			if (!Anim)
			{
				return;
			}

			const float Length = Anim->GetPlayLength();
			Time += DeltaSeconds;

			// 루프가 아니면 마지막 프레임에서 멈춘다 — JumpStart의 "공중 자세 유지"가 이걸로 구현된다.
			Time = (bLoop && Length > 0.0f) ? FMath::Fmod(Time, Length) : FMath::Min(Time, Length);
		}

		void Sample(FPoseContext& Output) const
		{
			FAnimationPoseData PoseData(Output);
			Anim->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Time), false, FDeltaTimeRecord(), bLoop));
		}
	};
}

struct FSisterAnimInstanceProxy : public FAnimInstanceProxy
{
	explicit FSisterAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	// 게임 스레드: 캐릭터 상태를 복사해 둔다(워커 스레드에서 UObject를 직접 건드리지 않기 위함).
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

		const ASisterCharacter* Sister = Cast<ASisterCharacter>(InAnimInstance->TryGetPawnOwner());
		if (!Sister)
		{
			return;
		}

		IdleAnim = Sister->GetIdleAnim();
		RunAnim = Sister->GetRunAnim();
		JumpStartAnim = Sister->GetJumpStartAnim();
		JumpEndAnim = Sister->GetJumpEndAnim();
		Speed = Sister->GetVelocity().Size2D();
		bIsFalling = Sister->GetCharacterMovement()->IsFalling();
	}

	virtual void Update(float DeltaSeconds) override
	{
		StateTime += DeltaSeconds;

		const SisterAnim::EState NextState = ChooseNextState();
		if (!bStarted || NextState != State)
		{
			EnterState(NextState);
		}

		Current.Advance(DeltaSeconds);
		Previous.Advance(DeltaSeconds);

		if (BlendAlpha < 1.0f)
		{
			BlendAlpha = FMath::Min(1.0f, BlendAlpha + DeltaSeconds / SisterAnim::BlendTime);
		}
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		if (!Current.Anim)
		{
			Output.ResetToRefPose();
			return true;
		}

		if (BlendAlpha >= 1.0f || !Previous.Anim)
		{
			Current.Sample(Output);
			return true;
		}

		FPoseContext CurrentPose(Output);
		FPoseContext PreviousPose(Output);
		Current.Sample(CurrentPose);
		Previous.Sample(PreviousPose);

		const FAnimationPoseData CurrentData(CurrentPose);
		const FAnimationPoseData PreviousData(PreviousPose);
		FAnimationPoseData OutputData(Output);
		FAnimationRuntime::BlendTwoPosesTogether(CurrentData, PreviousData, BlendAlpha, OutputData);
		return true;
	}

private:
	SisterAnim::EState ChooseNextState() const
	{
		using SisterAnim::EState;

		if (bIsFalling)
		{
			return EState::JumpStart;
		}

		const bool bMoving = Speed > SisterAnim::RunSpeedThreshold;

		switch (State)
		{
		case EState::JumpStart:
			// 방금 착지함
			return EState::JumpEnd;

		case EState::JumpEnd:
			if (bMoving && StateTime >= SisterAnim::MinLandingTimeBeforeRun)
			{
				return EState::Run;
			}
			if (!JumpEndAnim || StateTime >= JumpEndAnim->GetPlayLength())
			{
				return bMoving ? EState::Run : EState::Idle;
			}
			return EState::JumpEnd;

		default:
			return bMoving ? EState::Run : EState::Idle;
		}
	}

	void EnterState(SisterAnim::EState NewState)
	{
		using SisterAnim::EState;

		Previous = Current;

		switch (NewState)
		{
		case EState::Idle:
			Current = { IdleAnim, 0.0f, true };
			break;
		case EState::Run:
			Current = { RunAnim, 0.0f, true };
			break;
		case EState::JumpStart:
			Current = { JumpStartAnim, 0.0f, false };
			break;
		case EState::JumpEnd:
			Current = { JumpEndAnim, 0.0f, false };
			break;
		}

		BlendAlpha = bStarted ? 0.0f : 1.0f;

		State = NewState;
		StateTime = 0.0f;
		bStarted = true;
	}

	const UAnimSequence* IdleAnim = nullptr;
	const UAnimSequence* RunAnim = nullptr;
	const UAnimSequence* JumpStartAnim = nullptr;
	const UAnimSequence* JumpEndAnim = nullptr;
	float Speed = 0.0f;
	bool bIsFalling = false;

	SisterAnim::EState State = SisterAnim::EState::Idle;
	float StateTime = 0.0f;
	bool bStarted = false;

	SisterAnim::FSequencePlayer Current;
	SisterAnim::FSequencePlayer Previous;
	float BlendAlpha = 1.0f;
};

FAnimInstanceProxy* USisterAnimInstance::CreateAnimInstanceProxy()
{
	return new FSisterAnimInstanceProxy(this);
}
