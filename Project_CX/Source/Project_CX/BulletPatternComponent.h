#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BulletPatternComponent.generated.h"

class ABulletBase;

UENUM(BlueprintType)
enum class EBulletPattern : uint8
{
	Ring    UMETA(DisplayName = "원형 (360도 방사)"),
	Fan     UMETA(DisplayName = "부채꼴 (고정 방향)"),
	Spiral  UMETA(DisplayName = "나선 (회전하는 원형)"),
	Aimed   UMETA(DisplayName = "조준 (플레이어 방향 부채꼴)")
};

/**
 * 보스, 포탑 등 아무 액터에 붙이면 탄막을 쏘는 컴포넌트.
 * 패턴 여러 개를 쓰고 싶으면 컴포넌트를 여러 개 붙이거나, 블루프린트에서 Pattern 값을 바꿔가며 StartFiring 하면 된다.
 */
UCLASS(ClassGroup = (Bullet), meta = (BlueprintSpawnableComponent))
class PROJECT_CX_API UBulletPatternComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBulletPatternComponent();

	UFUNCTION(BlueprintCallable, Category = "Bullet Pattern")
	void StartFiring();

	UFUNCTION(BlueprintCallable, Category = "Bullet Pattern")
	void StopFiring();

	/** 한 번만 발사 (타이머 없이 직접 호출하고 싶을 때) */
	UFUNCTION(BlueprintCallable, Category = "Bullet Pattern")
	void FireOnce();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern")
	TSubclassOf<ABulletBase> BulletClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern")
	EBulletPattern Pattern = EBulletPattern::Ring;

	/** 한 번 발사할 때 나가는 탄 개수 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern", meta = (ClampMin = "1"))
	int32 BulletCount = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern")
	float BulletSpeed = 500.f;

	/** 발사 간격 (초) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern", meta = (ClampMin = "0.02"))
	float FireInterval = 0.5f;

	/** Fan, Aimed 패턴의 퍼지는 각도 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern")
	float SpreadAngle = 60.f;

	/** 기준 각도. Fan 의 발사 방향, Ring/Spiral 의 시작 각도 (0=오른쪽, 90=위, 180=왼쪽, 270=아래) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern")
	float BaseAngle = 180.f;

	/** Spiral 패턴에서 한 번 쏠 때마다 돌아가는 각도 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern")
	float RotatePerShot = 12.f;

	/** 액터 위치 기준 발사 지점 오프셋 (Y는 0으로 둘 것) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern")
	FVector SpawnOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet Pattern")
	bool bAutoStart = true;

	/** 시작할 때 미리 만들어 둘 탄 개수 */
	UPROPERTY(EditAnywhere, Category = "Bullet Pattern")
	int32 PrewarmCount = 200;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FTimerHandle FireTimer;
	float CurrentAngle = 0.f;

	void SpawnBullet(const FVector& Origin, float AngleDeg);
	void FireRing(const FVector& Origin, float StartAngle);
	void FireFan(const FVector& Origin, float CenterAngle);
	float GetAngleToPlayer(const FVector& Origin) const;
};
