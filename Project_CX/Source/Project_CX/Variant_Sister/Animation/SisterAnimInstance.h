// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "SisterAnimInstance.generated.h"

/**
 *  여동생 전용 네이티브 AnimInstance — AnimBP(애니메이션 그래프) 없이 C++만으로 상태 전환과 포즈 평가를 한다.
 *
 *  상태: Idle / Run / JumpStart / JumpEnd
 *   - 이동 중 Run(루프), 정지 시 Idle(루프)
 *   - 공중에 뜨면 JumpStart를 1회 재생하고 마지막 프레임에서 멈춰 공중 자세를 유지(반복 재생하지 않음)
 *   - 착지하면 JumpEnd를 1회 재생
 *  상태가 바뀔 때마다 이전 포즈와 짧게 크로스페이드해서 튀지 않게 한다.
 *
 *  실제 로직은 워커 스레드에서 도는 FSisterAnimInstanceProxy(.cpp)에 있다.
 */
UCLASS(Transient, NotBlueprintable)
class USisterAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
};
