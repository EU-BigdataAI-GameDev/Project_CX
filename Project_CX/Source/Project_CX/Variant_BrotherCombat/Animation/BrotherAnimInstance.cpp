// Copyright Epic Games, Inc. All Rights Reserved.

#include "BrotherAnimInstance.h"
#include "BrotherCombatComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BoneContainer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace BrotherAnim
{
	enum class EState : uint8
	{
		Idle,
		Run,
		Jump
	};

	constexpr float BlendTime = 0.15f;
	constexpr float RunSpeedThreshold = 10.0f;

	// 공중에서 위로 솟구치는(새 점프가 발사된) 것으로 보는 수직 속도 / 직전 프레임이 하강이었음을 보는 기준.
	constexpr float LaunchUpSpeed = 50.0f;

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
			Time = (bLoop && Length > 0.0f) ? FMath::Fmod(Time, Length) : FMath::Min(Time, Length);
		}

		void Sample(FPoseContext& Output) const
		{
			FAnimationPoseData PoseData(Output);
			Anim->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Time), false, FDeltaTimeRecord(), bLoop));
		}
	};
}

struct FBrotherAnimInstanceProxy : public FAnimInstanceProxy
{
	explicit FBrotherAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	// 게임 스레드: 캐릭터/컴포넌트 상태를 복사해 둔다(워커 스레드에서 UObject를 직접 건드리지 않기 위함).
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
	{
		FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

		const ACharacter* Character = Cast<ACharacter>(InAnimInstance->TryGetPawnOwner());
		if (!Character)
		{
			return;
		}

		Speed = Character->GetVelocity().Size2D();
		VerticalSpeed = Character->GetVelocity().Z;
		if (const UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			bIsFalling = Move->IsFalling();
		}

		if (const UBrotherCombatComponent* Combat = Character->FindComponentByClass<UBrotherCombatComponent>())
		{
			IdleAnim = Combat->GetNativeIdleAnim();
			RunAnim = Combat->GetNativeRunAnim();
			JumpAnim = Combat->GetNativeJumpAnim();

			AimAlpha = Combat->GetAimPoseAlpha();
			ArmRot = Combat->GetRightArmAimRotation();
			ForeArmRot = Combat->GetRightForeArmAimRotation();
		}
		else
		{
			AimAlpha = 0.0f;
		}
	}

	virtual void Update(float DeltaSeconds) override
	{
		StateTime += DeltaSeconds;

		const BrotherAnim::EState NextState = ChooseNextState();
		const bool bJustLaunched = IsNewLaunch();
		// 공중 재점프(착지와 같은 프레임 포함)는 같은 Jump 상태여도 클립을 처음부터 다시 시작한다.
		if (!bStarted || NextState != State || bJustLaunched)
		{
			EnterState(NextState);
		}

		bWasFalling = bIsFalling;
		PrevVerticalSpeed = VerticalSpeed;

		Current.Advance(DeltaSeconds);
		Previous.Advance(DeltaSeconds);
		IdleTime = IdleAnim && IdleAnim->GetPlayLength() > 0.0f ? FMath::Fmod(IdleTime + DeltaSeconds, IdleAnim->GetPlayLength()) : 0.0f;

		if (BlendAlpha < 1.0f)
		{
			BlendAlpha = FMath::Min(1.0f, BlendAlpha + DeltaSeconds / BrotherAnim::BlendTime);
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
		}
		else
		{
			FPoseContext CurrentPose(Output);
			FPoseContext PreviousPose(Output);
			Current.Sample(CurrentPose);
			Previous.Sample(PreviousPose);

			const FAnimationPoseData CurrentData(CurrentPose);
			const FAnimationPoseData PreviousData(PreviousPose);
			FAnimationPoseData OutputData(Output);
			FAnimationRuntime::BlendTwoPosesTogether(CurrentData, PreviousData, BlendAlpha, OutputData);
		}

		if (AimAlpha > KINDA_SMALL_NUMBER)
		{
			ApplyAimPose(Output);
		}
		return true;
	}

private:
	/** 지면에서 떨어졌거나, 이미 공중이라도 하강 중이던 캐릭터가 위로 솟구쳤으면(재점프/착지 동일 프레임 점프) 새 점프. */
	bool IsNewLaunch() const
	{
		if (!bIsFalling)
		{
			return false;
		}
		if (!bWasFalling)
		{
			return true;
		}
		return PrevVerticalSpeed <= 0.0f && VerticalSpeed > BrotherAnim::LaunchUpSpeed;
	}

	BrotherAnim::EState ChooseNextState() const
	{
		using BrotherAnim::EState;

		if (bIsFalling)
		{
			return EState::Jump;
		}
		return Speed > BrotherAnim::RunSpeedThreshold ? EState::Run : EState::Idle;
	}

	void EnterState(BrotherAnim::EState NewState)
	{
		using BrotherAnim::EState;

		Previous = Current;

		switch (NewState)
		{
		case EState::Idle:
			Current = { IdleAnim, 0.0f, true };
			break;
		case EState::Run:
			Current = { RunAnim, 0.0f, true };
			break;
		case EState::Jump:
			Current = { JumpAnim, 0.0f, false };
			break;
		}

		BlendAlpha = bStarted ? 0.0f : 1.0f;

		State = NewState;
		StateTime = 0.0f;
		bStarted = true;
	}

	/** 로컬 포즈에 사격 조준 자세를 얹는다. 수식은 ABP의 Transform(Modify) Bone(Bone Space / Add To Existing)과 동일. */
	void ApplyAimPose(FPoseContext& Output) const
	{
		FCompactPose& Pose = Output.Pose;
		const FBoneContainer& Bones = Pose.GetBoneContainer();

		const int32 ArmMeshIdx = Bones.GetPoseBoneIndexForBoneName(TEXT("RightArm"));
		const int32 ForeArmMeshIdx = Bones.GetPoseBoneIndexForBoneName(TEXT("RightForeArm"));
		if (ArmMeshIdx == INDEX_NONE)
		{
			return;
		}
		const FCompactPoseBoneIndex ArmIdx = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(ArmMeshIdx));
		if (!ArmIdx.IsValid())
		{
			return;
		}

		// (1) 오른팔 체인(RightArm 포함 모든 자손)을 이동 포즈 -> Idle 포즈로 알파만큼 블렌드(달리기 팔 스윙 제거).
		if (IdleAnim)
		{
			FPoseContext IdlePose(Output);
			const BrotherAnim::FSequencePlayer IdlePlayer{ IdleAnim, IdleTime, true };
			IdlePlayer.Sample(IdlePose);

			for (const FCompactPoseBoneIndex BoneIdx : Pose.ForEachBoneIndex())
			{
				if (BoneIdx == ArmIdx || Bones.BoneIsChildOf(BoneIdx, ArmIdx))
				{
					Pose[BoneIdx].BlendWith(IdlePose.Pose[BoneIdx], AimAlpha);
				}
			}
		}

		// (2) Transform(Modify) Bone 두 개. 로컬(부모 기준) 회전 = 오프셋 * 기존 회전, 알파 가중(스켈레탈 컨트롤의 Alpha 블렌드).
		//     노드의 Rotation 핀은 X=Roll, Y=Pitch, Z=Yaw이고 FRotator의 Roll/Pitch/Yaw 필드가 그대로 그 핀(X/Y/Z)에 대응한다.
		auto ApplyBoneSpaceAdd = [&](int32 MeshIdx, const FRotator& Rot)
		{
			if (MeshIdx == INDEX_NONE)
			{
				return;
			}
			const FCompactPoseBoneIndex Idx = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIdx));
			if (!Idx.IsValid())
			{
				return;
			}
			const FTransform Original = Pose[Idx];
			FTransform Modified = Original;
			Modified.SetRotation(FQuat(Rot) * Original.GetRotation());
			Pose[Idx].Blend(Original, Modified, AimAlpha);
			Pose[Idx].NormalizeRotation();
		};
		ApplyBoneSpaceAdd(ArmMeshIdx, ArmRot);
		ApplyBoneSpaceAdd(ForeArmMeshIdx, ForeArmRot);
	}

	const UAnimSequence* IdleAnim = nullptr;
	const UAnimSequence* RunAnim = nullptr;
	const UAnimSequence* JumpAnim = nullptr;
	float Speed = 0.0f;
	float VerticalSpeed = 0.0f;
	bool bIsFalling = false;

	float AimAlpha = 0.0f;
	FRotator ArmRot = FRotator::ZeroRotator;
	FRotator ForeArmRot = FRotator::ZeroRotator;

	BrotherAnim::EState State = BrotherAnim::EState::Idle;
	float StateTime = 0.0f;
	bool bStarted = false;
	bool bWasFalling = false;
	float PrevVerticalSpeed = 0.0f;
	float IdleTime = 0.0f;

	BrotherAnim::FSequencePlayer Current;
	BrotherAnim::FSequencePlayer Previous;
	float BlendAlpha = 1.0f;
};

FAnimInstanceProxy* UBrotherAnimInstance::CreateAnimInstanceProxy()
{
	return new FBrotherAnimInstanceProxy(this);
}
