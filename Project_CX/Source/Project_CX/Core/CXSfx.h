// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"

class USoundBase;
class USoundAttenuation;
class USoundConcurrency;
class USoundClass;

/**
 *  효과음 공용 유틸(.claude/Plans/효과음 적용 기획.md 6.2).
 *
 *  - 각 클래스는 UPROPERTY(EditDefaultsOnly) 사운드 슬롯을 두고, 생성자에서 Find()로 기본 에셋을 지정한다.
 *    에셋이 없으면 nullptr → 슬롯이 비면 무음(오류 없이 동작).
 *  - 기본 에셋 경로는 /Game/Project_CX/Audio/SFX/{Player,Brother,Sister,World,UI}/SW_*.
 */
namespace CXSfx
{
	/** 생성자 전용: 경로의 에셋을 찾는다(없으면 조용히 nullptr — ConstructorHelpers처럼 에러 로그를 남기지 않음). */
	template <typename T>
	T* Find(const TCHAR* Path)
	{
		return LoadObject<T>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	/** 배열에서 하나를 랜덤으로(비어 있으면 nullptr). 변형 여러 개를 번갈아 써서 반복감을 줄인다. */
	PROJECT_CX_API USoundBase* PickRandom(const TArray<TObjectPtr<USoundBase>>& Sounds);

	/**
	 *  3D 원샷. Attenuation / Concurrency / ClassOverride를 넘기면 에셋 자체 설정 대신 그것을 쓴다
	 *  (SoulCity 발소리 큐처럼 원본 에셋을 고치지 않고 프로젝트 사운드 클래스·감쇠를 적용할 때).
	 */
	PROJECT_CX_API void PlayAtLocation(const UObject* WorldContext, USoundBase* Sound, const FVector& Location, float Volume = 1.0f, float Pitch = 1.0f,
		USoundAttenuation* Attenuation = nullptr, USoundConcurrency* Concurrency = nullptr, USoundClass* ClassOverride = nullptr);
}
