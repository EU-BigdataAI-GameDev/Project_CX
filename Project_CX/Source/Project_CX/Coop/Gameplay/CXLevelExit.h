// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXLevelExit.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UWorld;

/**
 *  맵 출구(.claude/Plans/세이브포인트·맵 이동 기획.md 2장).
 *
 *  - 서버가 0.1초마다 확인: 살아 있는 플레이어 **전원**이 구역(ZoneExtent) 안이면 ALocalCoopGameMode::TravelToMap(NextMap).
 *    한 명이라도 사망/부활 대기면 막힌다. 구역 안에 있는 사람 칸에는 HUD가 WAITING FOR PARTNER를 그린다(GetWaitText).
 *  - NextMap 패키지가 아직 없으면(세이브 복도/Map3_Final 제작 전) 이동하지 않고 두 칸에 NEXT AREA OFFLINE 3초 + 네온 빨강.
 *    모두 구역을 나갔다가 다시 들어오면 재시도.
 *  - 이동 중/오프라인 알림 횟수는 복제(listen server). 이동 자체(페이드·음악·OpenLevel/ServerTravel)는 게임모드 담당.
 *
 *  비주얼: 엔진 큐브 + M_NeonEmissive — 바닥 구역 테두리, 뒤쪽 네온 프레임(bShowFrame), 진행 방향 화살표 3개(순차 점멸), 라벨.
 *  로컬 좌표: 원점 = 바닥 중앙, -X = 카메라 쪽, Y = 화면 가로(ArrowDirection 쪽이 다음 맵), 프레임/화살표는 +X FrameDepth(뒤).
 */
UCLASS()
class ACXLevelExit : public AActor
{
	GENERATED_BODY()

public:
	ACXLevelExit();

	/** 다음 맵 롱 패키지 이름(예: /Game/Project_CX/Maps/Map2_SoulCity2). 비어 있으면 빈 문자열. */
	UFUNCTION(BlueprintPure, Category = "CX|LevelExit")
	FString GetNextMapPackage() const;

	/** 에디터/Python 배치용: 다음 맵을 롱 패키지 이름으로 지정(아직 없는 맵도 가능 — 예: /Game/Project_CX/Maps/Map3_Final). */
	UFUNCTION(BlueprintCallable, Category = "CX|LevelExit")
	void SetNextMapPackage(const FString& MapPackageName);

	/** 다음 맵 패키지가 디스크(또는 pak)에 있는지. */
	UFUNCTION(BlueprintPure, Category = "CX|LevelExit")
	bool IsNextMapAvailable() const;

	UFUNCTION(BlueprintPure, Category = "CX|LevelExit")
	bool IsTravelling() const { return bTravelling; }

	bool IsPawnInZone(const APawn* Pawn) const;

	/** 이 머신에서 마지막 NEXT AREA OFFLINE 알림 후 지난 실시간(초). 없으면 -1. */
	float GetOfflineNoticeAge() const;

	/** HUD: 이 폰이 구역 안에서 파트너를 기다리는 중이면 안내 문구(WAITING FOR PARTNER / PARTNER REBOOTING), 아니면 빈 문자열. */
	FString GetWaitText(const APawn* ForPawn) const;

	/** 서버: 조건 무시하고 이동 시도(디버그). 다음 맵이 없으면 오프라인 알림. 이동을 시작하면 true. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "CX|LevelExit")
	bool ForceTravel();

	/** 테스트용: 첫 로컬 플레이어 앞(OffsetY)에 런타임 스폰(레벨에 저장 안 함). */
	UFUNCTION(BlueprintCallable, Category = "CX|LevelExit|Debug", meta = (WorldContext = "WorldContextObject"))
	static ACXLevelExit* DebugSpawnLevelExit(const UObject* WorldContextObject, const FString& NextMapPackage, float OffsetY = 400.0f);

	UFUNCTION(BlueprintPure, Category = "CX|LevelExit|Debug")
	FString GetDebugState() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 다음 맵(월드 에셋 소프트 경로). 아직 없는 맵도 경로만 넣어 둘 수 있다 → 실행 시 OFFLINE. */
	UPROPERTY(EditAnywhere, Category = "CX|LevelExit")
	TSoftObjectPtr<UWorld> NextMap;

	/** 판정 구역 반 크기(cm, X 깊이 / Y 가로 / Z 높이의 절반 — 바닥부터 2*Z까지). 기본 = 레인 전체 깊이. */
	UPROPERTY(EditAnywhere, Category = "CX|LevelExit")
	FVector ZoneExtent = FVector(260.0f, 170.0f, 180.0f);

	/** 다음 맵 방향(로컬 Y 부호, +1 = 화면 오른쪽). 화살표 방향. */
	UPROPERTY(EditAnywhere, Category = "CX|LevelExit")
	float ArrowDirection = 1.0f;

	/** 뒤쪽 네온 프레임 표시(레벨에 이미 출구 아트가 있으면 끈다). */
	UPROPERTY(EditAnywhere, Category = "CX|LevelExit")
	bool bShowFrame = true;

	/** 프레임·화살표·라벨의 깊이(로컬 +X, 레인 뒤 벽 앞). */
	UPROPERTY(EditAnywhere, Category = "CX|LevelExit")
	float FrameDepth = 200.0f;

	/** 화살표/라벨 높이(바닥에서). */
	UPROPERTY(EditAnywhere, Category = "CX|LevelExit")
	float SignHeight = 260.0f;

	UPROPERTY(EditAnywhere, Category = "CX|LevelExit")
	FString Label = TEXT("EXIT");

	/** NEXT AREA OFFLINE 표시 시간(초, HUD). */
	UPROPERTY(EditAnywhere, Category = "CX|LevelExit")
	float OfflineNoticeTime = 3.0f;

	// ------------------------------------------------------------ 비주얼

	UPROPERTY(VisibleAnywhere, Category = "CX|LevelExit") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "CX|LevelExit") TObjectPtr<UBoxComponent> ZoneBox;
	UPROPERTY(VisibleAnywhere, Category = "CX|LevelExit") TArray<TObjectPtr<UStaticMeshComponent>> FloorLines;
	UPROPERTY(VisibleAnywhere, Category = "CX|LevelExit") TArray<TObjectPtr<UStaticMeshComponent>> FrameNeon;
	UPROPERTY(VisibleAnywhere, Category = "CX|LevelExit") TArray<TObjectPtr<UStaticMeshComponent>> FrameBody;
	/** 화살표 3개 × 2획. */
	UPROPERTY(VisibleAnywhere, Category = "CX|LevelExit") TArray<TObjectPtr<UStaticMeshComponent>> ArrowStrokes;
	UPROPERTY(VisibleAnywhere, Category = "CX|LevelExit") TObjectPtr<UTextRenderComponent> LabelText;

private:
	void CheckZone();
	void LayoutVisuals();
	void RefreshVisuals(float Time);
	void ShowOffline();

	UFUNCTION()
	void OnRep_Offline();

	UPROPERTY(Replicated)
	bool bTravelling = false;

	UPROPERTY(ReplicatedUsing = OnRep_Offline)
	uint8 OfflineSerial = 0;

	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> NeonMaterial;

	/** 서버: 오프라인 알림 후 모두 나가야 다시 시도. */
	bool bArmed = true;
	bool bSeenFirstRep = false;
	uint8 LastSeenOfflineSerial = 0;
	double LastOfflineRealTime = -1.0;
	FTimerHandle ZoneTimer;
};
