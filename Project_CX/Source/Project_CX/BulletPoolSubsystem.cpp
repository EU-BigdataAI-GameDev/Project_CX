#include "BulletPoolSubsystem.h"
#include "BulletBase.h"
#include "Engine/World.h"

ABulletBase* UBulletPoolSubsystem::GetBullet(TSubclassOf<ABulletBase> BulletClass)
{
	if (!BulletClass)
	{
		return nullptr;
	}

	FBulletList& List = FreeBullets.FindOrAdd(BulletClass);
	while (List.Items.Num() > 0)
	{
		ABulletBase* Bullet = List.Items.Pop();
		if (IsValid(Bullet))
		{
			return Bullet;
		}
	}

	return SpawnNewBullet(BulletClass);
}

void UBulletPoolSubsystem::ReturnBullet(ABulletBase* Bullet)
{
	if (IsValid(Bullet))
	{
		FreeBullets.FindOrAdd(Bullet->GetClass()).Items.Add(Bullet);
	}
}

void UBulletPoolSubsystem::Prewarm(TSubclassOf<ABulletBase> BulletClass, int32 Count)
{
	if (!BulletClass)
	{
		return;
	}

	FBulletList& List = FreeBullets.FindOrAdd(BulletClass);
	for (int32 i = List.Items.Num(); i < Count; ++i)
	{
		if (ABulletBase* Bullet = SpawnNewBullet(BulletClass))
		{
			List.Items.Add(Bullet);
		}
	}
}

void UBulletPoolSubsystem::ClearAllBullets()
{
	for (ABulletBase* Bullet : AllBullets)
	{
		if (IsValid(Bullet) && Bullet->IsBulletActive())
		{
			Bullet->Deactivate();
		}
	}
}

ABulletBase* UBulletPoolSubsystem::SpawnNewBullet(TSubclassOf<ABulletBase> BulletClass)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ABulletBase* Bullet = World->SpawnActor<ABulletBase>(BulletClass, FTransform::Identity, Params);
	if (Bullet)
	{
		AllBullets.Add(Bullet);
	}
	return Bullet;
}
