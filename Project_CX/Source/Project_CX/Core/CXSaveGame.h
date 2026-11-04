// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CXSaveGame.generated.h"

/** LoadSlot 결과 — 시작화면이 이어하기 표시/경고 문구를 고른다. */
enum class ECXSaveLoadResult : uint8
{
	NoSave,		// 세이브 파일 없음
	Loaded,		// 정상
	Corrupt		// 파일은 있으나 읽을 수 없음(손상/다른 클래스)
};

/**
 *  세이브포인트(체크포인트) 저장 데이터(.claude/Plans/게임 시작화면 기획.md 11장).
 *
 *  슬롯은 하나(CX_Slot0)만 쓴다. 세이브포인트(ACXSavePoint)에 살아 있는 플레이어가 들어오면 서버가 저장하고
 *  (ALocalCoopGameMode::SaveProgressAt), 이어하기/게임 오버 RETRY가 읽어 두 사람을 그 세이브포인트에 스폰한다
 *  (.claude/Plans/세이브포인트·맵 이동 기획.md). 체력은 저장하지 않는다(이어하기 = 100).
 */
UCLASS()
class UCXSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static const FString SlotName;
	static constexpr int32 UserIndex = 0;

	/** 저장 형식 버전(필드가 바뀌면 올린다). 1 = 세이브포인트 시스템 첫 버전. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	int32 SaveVersion = 1;

	/** 이어하기로 열 맵(롱 패키지 이름, PIE 접두사 없음, 예: /Game/Project_CX/Maps/Map_SaveCorridor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FString MapName;

	/** 시작화면 정보줄에 보여줄 구역명(예: SCRAP STREET). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FString ZoneName;

	/** 마지막으로 통과한 세이브포인트 ID. 리스폰 위치를 찾는 데 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FName SavePointId;

	/** 세이브포인트 기준 스폰 위치(바닥)/방향 — 이어하기 때 SavePointId 액터를 못 찾으면 이 값으로 스폰. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FVector SpawnLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	float SpawnYaw = 0.0f;

	/** 저장 시각(로컬 시간). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FDateTime SaveTime;

	/** 진행 플래그(해킹한 단말기, 연 문 등). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	TArray<FName> ProgressFlags;

	static bool DoesSaveExist();

	/** 슬롯을 읽는다. OutSave는 Loaded일 때만 채워진다. */
	static ECXSaveLoadResult LoadSlot(UCXSaveGame*& OutSave);

	static bool DeleteSlot();

	/** 정상 세이브가 있고 그 맵이 MapPackageName과 같으면 반환(아니면 nullptr). 게임 오버 RETRY 판단용. */
	static UCXSaveGame* LoadSlotForMap(const FString& MapPackageName);

	/** 월드의 맵 롱 패키지 이름(PIE 접두사 제거). 예: /Game/Project_CX/Maps/Map1_SoulCity1 */
	static FString GetWorldMapPackageName(const UObject* WorldContextObject);

	/** 테스트용: 슬롯 내용 한 줄(없으면 "no save"). */
	UFUNCTION(BlueprintCallable, Category = "Save|Debug")
	static FString GetSlotDebugState();

	/** 테스트용: 슬롯 삭제. */
	UFUNCTION(BlueprintCallable, Category = "Save|Debug")
	static bool DebugDeleteSlot() { return DeleteSlot(); }

	/** "SCRAP STREET · 2026-10-28 22:14" 형식 정보줄. */
	FText GetSummaryText() const;
};
