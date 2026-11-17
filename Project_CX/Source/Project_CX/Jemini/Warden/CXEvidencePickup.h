// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXEvidencePickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UStaticMesh;
class UMaterialInterface;
class USoundBase;

/**
 *  기능 B. 증거 픽업(WARDEN 설득 퍼즐). 오빠가 닿으면 획득 → ACXWardenTerminal::AwardEvidence. 여동생은 못 줍는다(오빠의 탐험 몫).
 *
 *  - 분할 화면에서도 멀리서 보이게: 금색 데이터 칩(회전·부유) + 위로 솟는 빛기둥 + 주위를 도는 반짝이 8개(깜빡임) + 맥동하는 포인트 라이트.
 *    모두 엔진 큐브 + M_NeonEmissive(에셋 추가 없음, Niagara 모듈 의존 없음).
 *  - 서버 권위: 겹침 판정·획득·제거는 서버. 비주얼 연출은 각 머신 로컬 틱.
 *  - 거미로봇 드롭: ASpiderBot::DropEvidenceId가 있으면 죽을 때 SpawnDrop(바닥으로 내려 놓는다).
 */
UCLASS()
class ACXEvidencePickup : public AActor
{
	GENERATED_BODY()

public:
	ACXEvidencePickup();

	/** 서버: Location 아래 바닥을 찾아 그 위에 픽업을 스폰한다. */
	static ACXEvidencePickup* SpawnDrop(UWorld* World, FName EvidenceId, const FVector& Location);

	FName GetEvidenceId() const { return EvidenceId; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;

	/** 이 픽업이 주는 증거("E1"~"E3"). */
	UPROPERTY(EditAnywhere, Replicated, Category = "Evidence")
	FName EvidenceId = TEXT("E1");

	/** 바닥 위 칩 높이(cm). */
	UPROPERTY(EditAnywhere, Category = "Evidence")
	float HoverHeight = 75.0f;

	UPROPERTY(EditAnywhere, Category = "Evidence")
	FLinearColor GlowColor = FLinearColor(1.0f, 0.78f, 0.18f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Evidence|Sound")
	TObjectPtr<USoundBase> PickupSound;

	UPROPERTY(VisibleAnywhere, Category = "Evidence") TObjectPtr<USphereComponent> Trigger;
	UPROPERTY(VisibleAnywhere, Category = "Evidence") TObjectPtr<USceneComponent> Visual;
	UPROPERTY(VisibleAnywhere, Category = "Evidence") TObjectPtr<UStaticMeshComponent> Chip;
	UPROPERTY(VisibleAnywhere, Category = "Evidence") TObjectPtr<UStaticMeshComponent> ChipFrame;
	UPROPERTY(VisibleAnywhere, Category = "Evidence") TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY(VisibleAnywhere, Category = "Evidence") TObjectPtr<UStaticMeshComponent> Ring;
	UPROPERTY(VisibleAnywhere, Category = "Evidence") TArray<TObjectPtr<UStaticMeshComponent>> Sparkles;
	UPROPERTY(VisibleAnywhere, Category = "Evidence") TObjectPtr<UPointLightComponent> Glow;

private:
	void TryCollect(AActor* OtherActor);

	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> NeonMaterial;

	bool bCollected = false;
	float Age = 0.0f;
	TArray<float> SparklePhase;
};
