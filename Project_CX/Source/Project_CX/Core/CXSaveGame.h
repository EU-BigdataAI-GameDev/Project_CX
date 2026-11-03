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
 *  슬롯은 하나(CX_Slot0)만 쓴다. 세이브포인트 시스템은 아직 없으므로 지금은 아무도 저장하지 않고,
 *  시작화면은 파일 유무만 보고 이어하기 표시/새 게임 덮어쓰기 확인창을 결정한다.
 */
UCLASS()
class UCXSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static const FString SlotName;
	static constexpr int32 UserIndex = 0;

	/** 이어하기로 열 맵(롱 패키지 이름, 예: /Game/Project_CX/Maps/Map1_SoulCity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FString MapName;

	/** 시작화면 정보줄에 보여줄 구역명(예: SCRAP STREET). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FString ZoneName;

	/** 마지막으로 통과한 세이브포인트 ID. 리스폰 위치를 찾는 데 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Save")
	FName SavePointId;

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

	/** "SCRAP STREET · 2026-10-28 22:14" 형식 정보줄. */
	FText GetSummaryText() const;
};
