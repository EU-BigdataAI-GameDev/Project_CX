#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BulletPoolSubsystem.generated.h"

class ABulletBase;

USTRUCT()
struct FBulletList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<ABulletBase>> Items;
};

/**
 * 탄환 오브젝트 풀. 레벨(월드)마다 자동으로 하나씩 생긴다.
 * 탄을 매번 Spawn/Destroy 하면 수백 발에서 프레임이 떨어지므로 비활성 탄을 재사용한다.
 */
UCLASS()
class PROJECT_CX_API UBulletPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 비활성 탄을 하나 꺼낸다. 없으면 새로 스폰 */
	ABulletBase* GetBullet(TSubclassOf<ABulletBase> BulletClass);

	/** 탄을 풀로 돌려놓는다 (ABulletBase::Deactivate 에서 자동 호출) */
	void ReturnBullet(ABulletBase* Bullet);

	/** 게임 시작 시 미리 Count 개를 만들어 둔다 (첫 발사 때 렉 방지) */
	void Prewarm(TSubclassOf<ABulletBase> BulletClass, int32 Count);

	/** 화면에 있는 탄을 전부 지운다 (보스 페이즈 전환, 플레이어 피격 후 등) */
	UFUNCTION(BlueprintCallable, Category = "Bullet")
	void ClearAllBullets();

private:
	UPROPERTY()
	TMap<TSubclassOf<ABulletBase>, FBulletList> FreeBullets;

	UPROPERTY()
	TArray<TObjectPtr<ABulletBase>> AllBullets;

	ABulletBase* SpawnNewBullet(TSubclassOf<ABulletBase> BulletClass);
};
