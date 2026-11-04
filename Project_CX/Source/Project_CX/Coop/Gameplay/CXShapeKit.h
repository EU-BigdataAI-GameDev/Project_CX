// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

/**
 *  엔진 기본 큐브 + 프로젝트 네온 머티리얼(M_NeonEmissive: Color/Intensity)로 C++ 비주얼을 조립하는 공용 도우미.
 *  ACXSavePoint / ACXLevelExit가 쓴다(ACXCoopExitDoor와 같은 방식 — 에셋 없음).
 */
namespace CXShapeKit
{
	inline const TCHAR* CubePath = TEXT("/Engine/BasicShapes/Cube.Cube");
	inline const TCHAR* BodyMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	inline const TCHAR* NeonMaterialPath = TEXT("/Game/Project_CX/Environment/Materials/M_NeonEmissive.M_NeonEmissive");

	/** 생성자 전용: 충돌 없는 큐브(기본 큐브 = 100cm → Size cm). */
	inline UStaticMeshComponent* MakeCube(AActor* Owner, FName Name, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location, const FVector& Size,
		const FRotator& Rotation = FRotator::ZeroRotator, bool bShadow = false)
	{
		UStaticMeshComponent* Comp = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Comp->SetupAttachment(Parent);
		Comp->SetStaticMesh(Mesh);
		Comp->SetRelativeLocation(Location);
		Comp->SetRelativeRotation(Rotation);
		Comp->SetRelativeScale3D(Size / 100.0f);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->SetCastShadow(bShadow);
		Comp->SetCanEverAffectNavigation(false);
		return Comp;
	}

	inline UMaterialInstanceDynamic* GetOrCreateMID(UStaticMeshComponent* Mesh, UMaterialInterface* Parent)
	{
		if (!Mesh || !Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
		if (!MID || MID->Parent != Parent)
		{
			MID = UMaterialInstanceDynamic::Create(Parent, Mesh);
			Mesh->SetMaterial(0, MID);
		}
		return MID;
	}

	inline void SetNeon(UStaticMeshComponent* Mesh, UMaterialInterface* NeonMaterial, const FLinearColor& Color, float Intensity)
	{
		if (UMaterialInstanceDynamic* MID = GetOrCreateMID(Mesh, NeonMaterial))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		}
	}

	inline void SetBody(UStaticMeshComponent* Mesh, UMaterialInterface* BodyMaterial, const FLinearColor& Color)
	{
		if (UMaterialInstanceDynamic* MID = GetOrCreateMID(Mesh, BodyMaterial))
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}

	/** 로컬 상자(원점 = 바닥 중앙, Extent = 반 크기, Z는 0~2*Extent.Z) 안에 월드 점이 있는지. */
	inline bool IsInFloorBox(const FTransform& ActorTransform, const FVector& Extent, const FVector& WorldPoint)
	{
		const FVector Local = ActorTransform.InverseTransformPositionNoScale(WorldPoint);
		return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && Local.Z >= -60.0f && Local.Z <= Extent.Z * 2.0f;
	}
}
