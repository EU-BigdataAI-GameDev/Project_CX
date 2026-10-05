// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrotherCombatComponent.generated.h"

class ABrotherBullet;
class UInputAction;
class UEnhancedInputComponent;

/**
 *  오빠 캐릭터의 자동 조준 사격 로직(.claude/Plans/공격 시스템.md).
 *
 *  BP_Brother에 컴포넌트로 추가해서 사용한다 — BP_Brother의 부모 클래스(Character)는 건드리지 않는다.
 *  입력 바인딩도 이 컴포넌트가 직접 한다(Blueprint Event Graph 배선 불필요) — FireAction/AimAction에
 *  IA_Fire/IA_Aim 에셋만 지정해주면 된다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UBrotherCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBrotherCombatComponent();

	/** IA_Aim(Started)에서 호출. 가장 가까운 적을 찾아 조준 상태로 전환한다. 적이 없으면 조준 상태에 들어가지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "Brother|Combat")
	void StartAim();

	/** IA_Aim(Completed)에서 호출. 조준 상태를 해제한다. */
	UFUNCTION(BlueprintCallable, Category = "Brother|Combat")
	void StopAim();

	/**
	 *  IA_Fire(Started)에서 호출. 조준 중이면 현재 타겟 방향으로, 아니면 그 순간 가장 가까운 적을
	 *  즉시 찾아 그 방향으로(적이 없으면 캐릭터 정면으로) 총알을 발사한다. 장전 중이면 무시된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Brother|Combat")
	void Fire();

	UPROPERTY(BlueprintReadOnly, Category = "Brother|Combat")
	bool bIsAiming = false;

	UPROPERTY(BlueprintReadOnly, Category = "Brother|Combat")
	TObjectPtr<AActor> CurrentTarget;

	UPROPERTY(BlueprintReadOnly, Category = "Brother|Combat")
	int32 CurrentAmmo = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Brother|Combat")
	bool bIsReloading = false;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 *  소유 Pawn의 EnhancedInputComponent에 FireAction/AimAction을 바인딩한다. 최초 플레이어 Pawn은
	 *  Possess()가 BeginPlay 이후에 일어나 그 시점엔 InputComponent가 아직 없을 수 있으므로,
	 *  TickComponent에서 바인딩 전까지 매 틱 재시도한다(성공하면 bInputBound=true로 더 이상 시도하지 않음).
	 */
	void TryBindInput();

	/**
	 *  Range 이내 + 시야(LOS)를 만족하는 ICombatDamageable 중 가장 가까운 것을 반환한다. 없으면 nullptr.
	 *  bApplyForwardCone이 true면 캐릭터 전방 AimConeHalfAngleDeg 원뿔 밖의 후보는 제외한다
	 *  (근접 자동 조준(5m)은 방향 상관없이 걸려야 하므로 false로 호출).
	 */
	AActor* FindNearestTarget(float Range, bool bApplyForwardCone) const;

	/** CurrentTarget이 있으면 그쪽으로 캐릭터 회전을 보간하고, 이동 방향 자동 정렬(OrientRotationToMovement)을 잠시 끈다. 없으면 원래 값으로 복원한다. */
	void UpdateFacing(class ACharacter* OwnerCharacter, float DeltaTime);

	void StartReload();

	UFUNCTION()
	void FinishReload();

	void SpawnBullet(const FVector& AimDirection);

	/** 원격 클라이언트가 오빠를 조작하는 경우를 위한 발사 RPC — 실제 스폰/데미지는 서버 권위(공격 시스템.md 7장). */
	UFUNCTION(Server, Reliable)
	void Server_Fire(FVector_NetQuantizeNormal AimDirection);

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	TSubclassOf<ABrotherBullet> BulletClass;

	/** 좌클릭에 매핑된 Input Action(예: IA_Fire). 지정하지 않으면 발사가 바인딩되지 않는다. */
	UPROPERTY(EditAnywhere, Category = "Brother|Combat|Input")
	TObjectPtr<UInputAction> FireAction;

	/** 우클릭에 매핑된 Input Action(예: IA_Aim). 지정하지 않으면 조준이 바인딩되지 않는다. */
	UPROPERTY(EditAnywhere, Category = "Brother|Combat|Input")
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float AimRange = 3000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float AimConeHalfAngleDeg = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float AimRotationInterpSpeed = 10.0f;

	/** 이 범위(기본 5m) 안에 적이 있으면 조준 여부와 무관하게 항상 그 방향으로 캐릭터가 회전하고, 발사도 그 방향으로 나간다. WASD 이동 자체는 그대로 자유롭다. */
	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float ProximityAutoFaceRange = 500.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	int32 MaxAmmo = 30;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float ReloadTime = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float BulletDamage = 5.0f;

	/** 무기 메시/소켓이 없으므로 캐릭터 기준 상대 오프셋으로 스폰 지점을 계산한다(공격 시스템.md 4.2). */
	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	FVector MuzzleOffset = FVector(60.0f, 0.0f, 60.0f);

private:
	FTimerHandle ReloadTimerHandle;
	bool bInputBound = false;

	/** UpdateFacing이 OrientRotationToMovement를 껐다가 되돌릴 때 쓸, 최초 1회 캐싱한 원래 값. */
	bool bDefaultOrientRotationToMovement = true;
	bool bCachedDefaultOrientRotation = false;
};
