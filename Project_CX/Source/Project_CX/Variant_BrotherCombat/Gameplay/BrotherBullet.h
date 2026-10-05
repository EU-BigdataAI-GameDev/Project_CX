// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BrotherBullet.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

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
};
