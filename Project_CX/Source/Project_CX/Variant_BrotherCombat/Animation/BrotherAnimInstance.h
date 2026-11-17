// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "BrotherAnimInstance.generated.h"

/**
 *  오빠 전용 네이티브 AnimInstance — AnimBP 없이 C++만으로 상태 전환과 포즈 평가를 한다(USisterAnimInstance와 같은 설계).
 *  UBrotherCombatComponent가 BeginPlay에서 설치한다(bUseNativeAnim).
 *
 *  상태: Idle / Run / Jump(점프 클립 하나뿐)
 *   - 땅에서 떨어지는 매 순간(연속 점프, 착지 직후 재점프 포함) Jump를 처음부터 다시 재생한다. 클립 끝에서는 마지막 프레임 유지.
 *   - 상태가 바뀔 때마다 이전 포즈와 짧게 크로스페이드.
 *  사격 조준 자세: 컴포넌트의 FireAimAlpha > 0이면 오른팔 체인을 Idle 포즈로 블렌드한 뒤
 *  RightArm/RightForeArm에 본 공간(Add) 회전을 알파 가중으로 더한다(ABP_Brother의 Transform(Modify) Bone 두 개와 같은 수식).
 */
UCLASS(Transient, NotBlueprintable)
class UBrotherAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
