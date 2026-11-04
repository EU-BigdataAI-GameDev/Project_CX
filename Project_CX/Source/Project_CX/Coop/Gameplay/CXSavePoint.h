// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXSavePoint.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UPointLightComponent;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class USoundBase;

/**
 *  세이브포인트(.claude/Plans/세이브포인트·맵 이동 기획.md 3장).
 *
 *  - 살아 있는 플레이어 아무나 구역(ZoneExtent)에 들어오면 서버가 CX_Slot0에 저장한다(맵·ID·구역명·시각·스폰 위치).
 *    방문당 1회 — 구역이 비면 다시 무장된다. 게임 오버/클리어/맵 이동 중에는 저장하지 않는다.
 *  - 저장하면 표시등 주황(대기) → 초록(저장됨), 두 칸 HUD 배너 `PROGRESS SAVED`, SW_Hack_Granted 1회.
 *    저장 상태와 저장 횟수는 복제(listen server) — 각 머신이 OnRep에서 소리/배너 시각을 기록한다.
 *  - 이어하기/RETRY 때 게임모드가 두 사람을 GetSpawnFloorFor(오빠 -Y / 여동생 +Y)에 스폰하고 MarkSpawnedHere를 부른다.
 *  - 체력은 저장하지 않는다(이어하기·부활 = 100 — 사용자 결정).
 *
 *  비주얼: 엔진 큐브 + M_NeonEmissive(단말기 기둥 + 화면 + 꼭대기 비컨 + 바닥 패드 + 포인트 라이트 + 라벨).
 *  로컬 좌표: 원점 = 바닥 중앙, -X = 카메라 쪽(사이드뷰 카메라가 +X를 본다), 단말기 몸체는 +X(뒤)에 선다.
 */
UCLASS()
class ACXSavePoint : public AActor
{
	GENERATED_BODY()

public:
	ACXSavePoint();

	/** 저장에 쓰는 ID. 비어 있으면 액터 이름. */
	UFUNCTION(BlueprintPure, Category = "CX|SavePoint")
	FName GetSavePointId() const;

	/** 시작화면 정보줄용 구역명. 비어 있으면 HUD의 현재 맵 구역명(ACoopHUD::GetZoneTitle), 그것도 없으면 UNDERCITY. */
	FString ResolveZoneName() const;

	UFUNCTION(BlueprintPure, Category = "CX|SavePoint")
	bool IsSaved() const { return bSaved; }

	/** 이 머신에서 마지막 저장 피드백 후 지난 시간(실시간 초). 저장한 적 없으면 -1. HUD 배너용. */
	float GetTimeSinceSaved() const;

	/** 폰이 저장 구역 안인지(로컬 상자, 위치 기준). */
	bool IsPawnInZone(const APawn* Pawn) const;

	/** 스폰할 바닥 지점(오빠 -Y / 여동생 +Y SpawnSpacing). 아래로 바닥을 찾아 높이를 맞춘다(캡슐 반높이는 호출자가 더함). */
	FVector GetSpawnFloorFor(bool bSister) const;

	/** 서버: 이 세이브포인트에서 스폰했다 — 저장됨 표시로 시작, 구역이 빌 때까지 재저장 안 함, 배너/소리 없음. */
	void MarkSpawnedHere();

	/** 서버: 지금 저장(조건 무시, 디버그/테스트용). 실패하면 false. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "CX|SavePoint")
	bool SaveNow();

	/** 레벨에서 ID로 찾기. */
	static ACXSavePoint* FindById(const UWorld* World, FName Id);

	/**
	 *  테스트용: 첫 로컬 플레이어 폰 기준(OffsetY = 화면 가로, OffsetX = 깊이)에 런타임 스폰(서버/스탠드얼론). 레벨에 저장하지 않는다.
	 *  런타임 세이브포인트는 다시 로드하면 없으므로 이어하기는 세이브의 SpawnLocation으로 스폰된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "CX|SavePoint|Debug", meta = (WorldContext = "WorldContextObject"))
	static ACXSavePoint* DebugSpawnSavePoint(const UObject* WorldContextObject, float OffsetY = 300.0f, float OffsetX = 0.0f);

	UFUNCTION(BlueprintPure, Category = "CX|SavePoint|Debug")
	FString GetDebugState() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 저장 ID(세이브 파일에 기록). 맵 안에서 유일하게. 예: SP_Corridor_01 */
	UPROPERTY(EditAnywhere, Category = "CX|SavePoint")
	FName SavePointId;

	/** 시작화면 CONTINUE 정보줄 구역명. 비우면 HUD 구역명. */
	UPROPERTY(EditAnywhere, Category = "CX|SavePoint")
	FString ZoneName;

	/** 저장 구역 반 크기(cm, X 깊이 / Y 가로 / Z 높이의 절반 — 바닥부터 2*Z까지). 기본 = 레인 전체 깊이. */
	UPROPERTY(EditAnywhere, Category = "CX|SavePoint")
	FVector ZoneExtent = FVector(260.0f, 160.0f, 160.0f);

	/** 스폰 위치: 원점에서 가로(Y)로 ±이 값(오빠 -, 여동생 +), 깊이(X)는 SpawnDepth. */
	UPROPERTY(EditAnywhere, Category = "CX|SavePoint")
	float SpawnSpacing = 90.0f;

	UPROPERTY(EditAnywhere, Category = "CX|SavePoint")
	float SpawnDepth = -40.0f;

	/** 저장 효과음(기존 에셋 SW_Hack_Granted). */
	UPROPERTY(EditAnywhere, Category = "CX|SavePoint|Sound")
	TObjectPtr<USoundBase> SaveSound;

	// ------------------------------------------------------------ 비주얼

	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TObjectPtr<USceneComponent> Root;
	/** 에디터에서 구역을 보여 주는 상자(게임에서는 숨김, 충돌 없음 — 판정은 위치 계산). */
	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TObjectPtr<UBoxComponent> ZoneBox;
	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TArray<TObjectPtr<UStaticMeshComponent>> BodyPieces;
	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TObjectPtr<UStaticMeshComponent> Screen;
	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TObjectPtr<UStaticMeshComponent> Beacon;
	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TArray<TObjectPtr<UStaticMeshComponent>> StatusStrips;
	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TArray<TObjectPtr<UStaticMeshComponent>> PadLines;
	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TObjectPtr<UPointLightComponent> StatusLight;
	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TObjectPtr<UTextRenderComponent> TitleLabel;
	UPROPERTY(VisibleAnywhere, Category = "CX|SavePoint") TObjectPtr<UTextRenderComponent> StatusLabel;

private:
	void CheckZone();
	void ApplyMaterials();
	void RefreshVisuals(float Time);
	/** 서버: 저장 성공 → 상태 복제 + 로컬 피드백. bFeedback=false면 배너/소리 없이 상태만. */
	void SetSavedState(bool bWithFeedback);

	UFUNCTION()
	void OnRep_SaveState();

	UPROPERTY(ReplicatedUsing = OnRep_SaveState)
	bool bSaved = false;

	/** 저장할 때마다 +1(피드백 트리거). */
	UPROPERTY(ReplicatedUsing = OnRep_SaveState)
	uint8 SaveSerial = 0;

	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> NeonMaterial;

	/** 서버: 구역이 비어야 다시 저장. */
	bool bArmed = true;
	bool bSeenFirstRep = false;
	uint8 LastSeenSerial = 0;
	double LastFeedbackRealTime = -1.0;
	FTimerHandle ZoneTimer;
};
