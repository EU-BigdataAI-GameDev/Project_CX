// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

const FString UCXSaveGame::SlotName = TEXT("CX_Slot0");

bool UCXSaveGame::DoesSaveExist()
{
	return UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex);
}

ECXSaveLoadResult UCXSaveGame::LoadSlot(UCXSaveGame*& OutSave)
{
	OutSave = nullptr;
	if (!DoesSaveExist())
	{
		return ECXSaveLoadResult::NoSave;
	}
	// 파일은 있는데 역직렬화 실패/다른 클래스면 손상으로 본다.
	OutSave = Cast<UCXSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
	return OutSave ? ECXSaveLoadResult::Loaded : ECXSaveLoadResult::Corrupt;
}

bool UCXSaveGame::DeleteSlot()
{
	return !DoesSaveExist() || UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex);
}

FText UCXSaveGame::GetSummaryText() const
{
	const FString Zone = ZoneName.IsEmpty() ? TEXT("UNKNOWN") : ZoneName;
	return FText::FromString(FString::Printf(TEXT("%s  ·  %s"), *Zone, *SaveTime.ToString(TEXT("%Y-%m-%d %H:%M"))));
}

UCXSaveGame* UCXSaveGame::LoadSlotForMap(const FString& MapPackageName)
{
	UCXSaveGame* Save = nullptr;
	if (LoadSlot(Save) != ECXSaveLoadResult::Loaded || !Save || MapPackageName.IsEmpty())
	{
		return nullptr;
	}
	return Save->MapName.Equals(MapPackageName, ESearchCase::IgnoreCase) ? Save : nullptr;
}

FString UCXSaveGame::GetWorldMapPackageName(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) : FString();
}

FString UCXSaveGame::GetSlotDebugState()
{
	UCXSaveGame* Save = nullptr;
	switch (LoadSlot(Save))
	{
	case ECXSaveLoadResult::NoSave:
		return TEXT("no save");
	case ECXSaveLoadResult::Corrupt:
		return TEXT("corrupt save");
	default:
		break;
	}
	return FString::Printf(TEXT("v%d map=%s point=%s zone=%s loc=%s yaw=%.0f time=%s"), Save->SaveVersion, *Save->MapName, *Save->SavePointId.ToString(),
		*Save->ZoneName, *Save->SpawnLocation.ToCompactString(), Save->SpawnYaw, *Save->SaveTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
}
