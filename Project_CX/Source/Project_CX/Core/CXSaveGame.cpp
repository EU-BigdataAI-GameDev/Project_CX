// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXSaveGame.h"
#include "Kismet/GameplayStatics.h"

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
