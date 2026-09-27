// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SisterCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UAnimSequence;
struct FInputActionValue;

/**
 *  여동생 캐릭터. 로컬 2인 플레이에서 두 번째 로컬 플레이어(게임패드)가 조작한다.
 *
 *  애니메이션은 AnimBP 없이 USisterAnimInstance(네이티브 C++)가 Idle / Run / JumpStart / JumpEnd 애니메이션을
 *  상태에 따라 직접 재생·크로스페이드한다. 애니메이션 에셋은 BP_Sister 디폴트에서 지정한다.
 */
UCLASS(abstract)
class ASisterCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASisterCharacter();

	const UAnimSequence* GetIdleAnim() const { return IdleAnim; }
	const UAnimSequence* GetRunAnim() const { return RunAnim; }
	const UAnimSequence* GetJumpStartAnim() const { return JumpStartAnim; }
	const UAnimSequence* GetJumpEndAnim() const { return JumpEndAnim; }

protected:
	virtual void NotifyControllerChanged() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void Move(const FInputActionValue& Value);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** 이 캐릭터를 조작하는 로컬 플레이어에게 추가할 입력 매핑 컨텍스트. */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	/** 지면에서 가만히 있을 때 루프 재생. */
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> IdleAnim;

	/** 지면 이동 중 루프 재생. */
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> RunAnim;

	/** 점프 순간 1회 재생 후, 공중에 있는 동안 마지막 프레임을 유지한다. */
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> JumpStartAnim;

	/** 착지 시 1회 재생. */
	UPROPERTY(EditDefaultsOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> JumpEndAnim;
};
