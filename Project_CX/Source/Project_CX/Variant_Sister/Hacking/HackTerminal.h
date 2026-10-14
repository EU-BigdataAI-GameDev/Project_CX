// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HackTerminal.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UBoxComponent;

/** 단말기가 요구하는 퍼즐 종류(.claude/Plans/해킹 시스템 기획.md 3~4장). */
UENUM(BlueprintType)
enum class EHackPuzzleType : uint8
{
	/** 화살표 시퀀스를 순서대로 입력(라운드마다 길이 증가). */
	ArrowSequence,
	/** 4자리 비밀번호 입력. 숫자 단서(AHackCodeClue)는 오빠 화면에만 보인다. */
	Passcode
};

UENUM(BlueprintType)
enum class EHackArrow : uint8
{
	Up,
	Down,
	Left,
	Right
};

UENUM(BlueprintType)
enum class EHackTerminalState : uint8
{
	Locked,
	InProgress,
	Hacked
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHackTerminalHacked, class AHackTerminal*, Terminal);

/**
 *  여동생만 해킹할 수 있는 월드 단말기.
 *
 *  퍼즐 진행(입력 판정)은 여동생의 USisterHackComponent가 하고, 단말기는 퍼즐 설정과 상태(복제)만 가진다.
 *  해킹에 성공하면 UnlockActors를 숨기고 충돌을 끄며(레이저 게이트 등), OnHacked를 브로드캐스트한다.
 */
UCLASS()
class AHackTerminal : public AActor
{
	GENERATED_BODY()

public:
	AHackTerminal();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** HUD/로그에 표시되는 이름. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack")
	FString TerminalName = TEXT("TERMINAL_01");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack")
	EHackPuzzleType PuzzleType = EHackPuzzleType::ArrowSequence;

	/** 화살표 퍼즐 라운드별 길이(각 1~7). 기존 맵2 해킹 방어전 BP의 RowLengths 규칙을 따른다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Arrow", meta = (EditCondition = "PuzzleType == EHackPuzzleType::ArrowSequence"))
	TArray<int32> ArrowRoundLengths = { 4, 5, 6 };

	/** 켜져 있으면 BeginPlay에서 4자리 비밀번호를 랜덤으로 만든다(매 플레이 다른 답). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack|Passcode", meta = (EditCondition = "PuzzleType == EHackPuzzleType::Passcode"))
	bool bRandomizePasscode = true;

	/** 비밀번호(숫자 4자리). bRandomizePasscode가 켜져 있으면 런타임에 덮어쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Hack|Passcode", meta = (EditCondition = "PuzzleType == EHackPuzzleType::Passcode"))
	FString Passcode = TEXT("0000");

	/** 여동생이 이 거리(cm) 안에 있어야 해킹을 시작할 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack")
	float InteractRange = 300.0f;

	/** 해킹 성공 시 숨기고 충돌을 끌 액터들(레이저 게이트, 문 등). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hack")
	TArray<TObjectPtr<AActor>> UnlockActors;

	UPROPERTY(BlueprintAssignable, Category = "Hack")
	FOnHackTerminalHacked OnHacked;

	UFUNCTION(BlueprintPure, Category = "Hack")
	EHackTerminalState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Hack")
	bool CanBeHacked() const { return State == EHackTerminalState::Locked; }

	/** 비밀번호 i번째(0~3) 숫자. 단서 액터가 표시용으로 읽는다. */
	int32 GetPasscodeDigit(int32 Index) const;

	/** 화살표 라운드 하나를 랜덤으로 만든다. */
	TArray<EHackArrow> MakeArrowRound(int32 Length) const;

	/** 해킹 세션 시작/취소/성공 — 권위 측에서 호출된다. */
	void SetInProgress(bool bInProgress);
	void MarkHacked();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 메시가 바뀌거나 크기가 조절될 때마다 충돌 박스를 메시 바운드에 맞춘다. */
	virtual void OnConstruction(const FTransform& Transform) override;

	UFUNCTION()
	void OnRep_State();

	void ApplyStateVisuals();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hack")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hack")
	TObjectPtr<UStaticMeshComponent> Mesh;

	/** 캐릭터가 단말기를 뚫고 지나가지 않게 하는 충돌. 메시 자체 충돌은 에셋마다 품질이 달라 쓰지 않는다(프로젝트 COL 박스 방식과 동일). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hack")
	TObjectPtr<UBoxComponent> Collision;

	/** 상태등: Locked 빨강 / InProgress 노랑 깜빡임 / Hacked 초록. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hack")
	TObjectPtr<UPointLightComponent> StatusLight;

	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "Hack")
	EHackTerminalState State = EHackTerminalState::Locked;
};
