// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoopInteractable.h"
#include "CXCoopExitDoor.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class USoundBase;

/**
 *  마지막 맵 출구 문 — 두 버튼 문(.claude/Plans/게임 클리어 화면 기획.md 2.1).
 *
 *  - 문 양옆 버튼 2개: 왼쪽 오빠(청록) / 오른쪽 여동생(마젠타). 각 버튼은 자기 캐릭터만 누를 수 있다(공용 상호작용 F/X).
 *  - 눌린 버튼은 켜진 채 유지(동시 입력 불필요). 버튼 등과 문 위 표시등 2칸이 회색 → 자기 색.
 *  - 두 버튼이 모두 켜지면 약 1.5초 셔터가 말려 올라가며 열리고, 다 열리면 문 충돌을 끄고 ALocalCoopGameMode::NotifyGameCleared.
 *  - 버튼 상태/열림은 서버에서만 바꾸는 복제 변수(listen server 전제). 클리어는 1회만.
 *
 *  비주얼은 엔진 기본 큐브 + 프로젝트 네온 머티리얼(M_NeonEmissive)로 C++에서 조립한다(에셋 없음).
 *  로컬 좌표: -X가 카메라 쪽(사이드뷰 카메라가 +X를 본다), Y = 화면 가로, 원점 = 문 아래 가운데 바닥.
 */
UCLASS()
class ACXCoopExitDoor : public AActor, public ICoopInteractable
{
	GENERATED_BODY()

public:
	ACXCoopExitDoor();

	// ICoopInteractable
	virtual bool CanInteract(const APawn* Pawn) const override;
	virtual void Interact(APawn* Pawn) override;
	virtual FVector GetInteractLocation(const APawn* Pawn) const override;
	virtual FString GetInteractPrompt(const APawn* Pawn) const override { return TEXT("PRESS"); }

	UFUNCTION(BlueprintPure, Category = "Coop|ExitDoor")
	bool IsButtonPressed(ECoopCharacterRole InRole) const;

	UFUNCTION(BlueprintPure, Category = "Coop|ExitDoor")
	bool IsOpening() const { return bOpening; }

	UFUNCTION(BlueprintPure, Category = "Coop|ExitDoor")
	bool IsFullyOpen() const { return OpenAlpha >= 1.0f; }

	/** 역할 버튼의 월드 위치(버튼 등 중심). */
	UFUNCTION(BlueprintPure, Category = "Coop|ExitDoor")
	FVector GetButtonLocation(ECoopCharacterRole InRole) const;

	/**
	 *  테스트용: 첫 로컬 플레이어 폰 기준(OffsetY = 화면 가로, OffsetX = 안쪽 깊이)에 문을 런타임 스폰한다(서버/스탠드얼론).
	 *  레벨에 저장하지 않는다 — 마지막 맵(Map3_Final)이 생기면 거기에 배치.
	 */
	UFUNCTION(BlueprintCallable, Category = "Coop|ExitDoor|Debug", meta = (WorldContext = "WorldContextObject"))
	static ACXCoopExitDoor* DebugSpawnExitDoor(const UObject* WorldContextObject, float OffsetY = 500.0f, float OffsetX = 150.0f);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

	/** 버튼 앞 상호작용 범위(로컬 좌표): 버튼 중심에서 가로(Y) ± 이 값. */
	UPROPERTY(EditAnywhere, Category = "Coop|ExitDoor")
	float ButtonReachHalfWidth = 120.0f;

	/** 버튼 앞 상호작용 범위: 카메라 쪽(-X)으로 이 깊이까지. */
	UPROPERTY(EditAnywhere, Category = "Coop|ExitDoor")
	float ButtonReachDepth = 400.0f;

	/** 버튼 앞 상호작용 범위: 버튼 높이에서 위아래 ± 이 값. */
	UPROPERTY(EditAnywhere, Category = "Coop|ExitDoor")
	float ButtonReachHeight = 250.0f;

	/** 두 버튼이 모두 켜진 뒤 문이 다 열리기까지(초). 끝나면 클리어화면. */
	UPROPERTY(EditAnywhere, Category = "Coop|ExitDoor")
	float OpenDuration = 1.5f;

	/** 사운드 슬롯(에셋이 없으면 무음). */
	UPROPERTY(EditAnywhere, Category = "Coop|ExitDoor|Sound")
	TObjectPtr<USoundBase> ButtonSound;

	UPROPERTY(EditAnywhere, Category = "Coop|ExitDoor|Sound")
	TObjectPtr<USoundBase> OpenSound;

	// ------------------------------------------------------------ 비주얼(기본 큐브 조립)

	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<USceneComponent> Root;
	/** 문 뒤 어두운 통로(열리면 보인다). */
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> PassageBack;
	/** 셔터 피벗(문 위쪽). 열릴 때 세로 스케일을 줄여 위로 말려 올라가게 한다. */
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<USceneComponent> ShutterPivot;
	/** 셔터 본체 — 충돌(BlockAll, 카메라 무시). 다 열리면 충돌 끔. */
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> DoorPanel;
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TArray<TObjectPtr<UStaticMeshComponent>> ShutterSlats;
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TArray<TObjectPtr<UStaticMeshComponent>> FramePieces;
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> FrameNeonLeft;
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> FrameNeonRight;
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> FrameNeonTop;
	/** 문 위 표시등 2칸(왼쪽 오빠 / 오른쪽 여동생). */
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> BrotherLamp;
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> SisterLamp;
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UTextRenderComponent> ExitLabel;
	/** 버튼 등(눌리면 자기 색으로 켜짐). */
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> BrotherButton;
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> SisterButton;
	/** 버튼 아래 담당 색 띠(항상 약하게 켜짐 — 누구 버튼인지 표시). */
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> BrotherRim;
	UPROPERTY(VisibleAnywhere, Category = "Coop|ExitDoor") TObjectPtr<UStaticMeshComponent> SisterRim;

private:
	UStaticMeshComponent* MakeCube(FName Name, USceneComponent* Parent, const FVector& Location, const FVector& Size, bool bShadow = true);
	void ApplyMaterials();
	void SetNeon(UStaticMeshComponent* Mesh, const FLinearColor& Color, float Intensity);
	void SetBody(UStaticMeshComponent* Mesh, const FLinearColor& Color);
	void RefreshLights();
	void PressButton(ECoopCharacterRole InRole);
	void StartOpening();
	void FinishOpening();
	void ApplyOpenAlpha();

	UFUNCTION()
	void OnRep_Buttons();

	UFUNCTION()
	void OnRep_Opening();

	UPROPERTY(ReplicatedUsing = OnRep_Buttons)
	bool bBrotherPressed = false;

	UPROPERTY(ReplicatedUsing = OnRep_Buttons)
	bool bSisterPressed = false;

	UPROPERTY(ReplicatedUsing = OnRep_Opening)
	bool bOpening = false;

	UPROPERTY(Transient) TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> BodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> NeonMaterial;

	float OpenAlpha = 0.0f;
	bool bClearNotified = false;

	/** 버튼음 중복 방지 — 이 머신에서 이미 소리를 낸 버튼. */
	bool bBrotherButtonSounded = false;
	bool bSisterButtonSounded = false;
};
