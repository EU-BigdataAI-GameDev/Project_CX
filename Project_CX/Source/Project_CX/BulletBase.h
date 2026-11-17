#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BulletBase.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 * 탄막용 탄환. X-Z 평면(좌우 X, 상하 Z)에서만 움직이고 Y는 발사 위치에 고정된다.
 * 각도 기준: 0도 = 오른쪽(+X), 90도 = 위(+Z), 180도 = 왼쪽, 270도 = 아래
 * 직접 Spawn/Destroy 하지 말고 UBulletPoolSubsystem 을 통해 꺼내 쓴다.
 */
UCLASS()
class PROJECT_CX_API ABulletBase : public AActor
{
	GENERATED_BODY()

public:
	ABulletBase();

	virtual void Tick(float DeltaSeconds) override;

	/** 풀에서 꺼낸 직후 호출. 위치, 각도(도), 속도를 받아 발사 */
	void Fire(const FVector& StartLocation, float InAngleDeg, float InSpeed);

	/** 화면에서 숨기고 풀로 반환 */
	void Deactivate();

	bool IsBulletActive() const { return bActive; }

	/** 플레이어에게 주는 데미지 */
	UPROPERTY(EditDefaultsOnly, Category = "Bullet")
	float Damage = 1.f;

	/** 이 시간이 지나면 자동으로 풀로 반환 (초) */
	UPROPERTY(EditDefaultsOnly, Category = "Bullet")
	float LifeTime = 8.f;

	/** 초당 속도 증가량. 음수면 점점 느려짐 */
	UPROPERTY(EditDefaultsOnly, Category = "Bullet")
	float Acceleration = 0.f;

	/** 초당 회전 각도. 0이 아니면 곡선으로 휘어 나감 */
	UPROPERTY(EditDefaultsOnly, Category = "Bullet")
	float AngularSpeed = 0.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Bullet")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "Bullet")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

private:
	bool bActive = false;
	float AngleDeg = 0.f;
	float Speed = 0.f;
	float Age = 0.f;
	float FixedY = 0.f;

	void SetVisualActive(bool bNewActive);
};
