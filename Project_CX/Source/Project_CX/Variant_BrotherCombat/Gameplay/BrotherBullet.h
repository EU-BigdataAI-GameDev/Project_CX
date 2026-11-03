// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrotherBullet.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class USoundBase;

/**
 *  Simple visible projectile fired by UBrotherCombatComponent.
 *  Travels in a straight line and applies damage to the first ICombatDamageable it overlaps.
 */
UCLASS()
class ABrotherBullet : public AActor
{
	GENERATED_BODY()

public:
	ABrotherBullet();

	/** Sets the damage this bullet deals on hit. Must be called right after SpawnActor, before it can overlap anything. */
	void SetDamage(float InDamage) { Damage = InDamage; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnBulletOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	/** 지형(WorldStatic)에 막혀 멈춤 — 충돌음 후 소멸(공격 시스템.md: 지형 충돌 시 소멸, 관통 없음). */
	UFUNCTION()
	void OnBulletStop(const FHitResult& ImpactResult);

	void PlayImpactSound(const FVector& Location);

	UPROPERTY(VisibleAnywhere, Category = "Bullet")
	TObjectPtr<USphereComponent> CollisionComp;

	UPROPERTY(VisibleAnywhere, Category = "Bullet")
	TObjectPtr<UStaticMeshComponent> MeshComp;

	UPROPERTY(VisibleAnywhere, Category = "Bullet")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	/** Damage dealt on hit. Overwritten per-shot via SetDamage(); this is just the fallback default. */
	UPROPERTY(EditDefaultsOnly, Category = "Bullet")
	float Damage = 5.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Bullet")
	float BulletSpeed = 3000.0f;

	/** Matches 공격 시스템.md 4.3: lifespan tuned so Speed * LifeSpan == weapon range (3000cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Bullet")
	float BulletLifeSpan = 1.0f;

	/**
	 *  레일건 발사음 변형들(랜덤 1개). 총알은 복제되는 액터라 BeginPlay에서 재생하면 모든 머신의 같은 위치(총구)에서 난다
	 *  (효과음 적용 기획 6.1/6.5). 감쇠 ATT_Medium·동시 재생 SCon_Railgun(3개, 가장 오래된 것 정지)은 에셋에 설정돼 있다.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Bullet|Sound")
	TArray<TObjectPtr<USoundBase>> FireSounds;

	/** 벽(데미지를 받지 않는 대상)에 맞았을 때(랜덤 1개). 적 피격음은 거미로봇 구현 후(기획 2.4 보류). */
	UPROPERTY(EditDefaultsOnly, Category = "Bullet|Sound")
	TArray<TObjectPtr<USoundBase>> ImpactWallSounds;

private:
	bool bImpactPlayed = false;
};
