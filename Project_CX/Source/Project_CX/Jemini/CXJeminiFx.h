// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXJeminiFx.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class USoundBase;

/** 제미니 기능 연출 종류(.claude/Plans/Jemini_plans/05_UE_변경.md 5-1 이펙트). */
UENUM()
enum class ECXJeminiFxType : uint8
{
	None,
	/** 펄스: 아래로 퍼지는 링 + 빛 기둥(0.3초 번쩍) — 적 위치. */
	Pulse,
	/** 반짝이: 몸 주위로 올라가는 작은 빛 입자(회복 = 초록, 공격력 강화 = 주황). Follow 대상에 붙는다. */
	Sparkle,
	/** 보호막: 대상 주위 반투명 구체(가산), 끝나기 1초 전 깜빡임. */
	Shield,
	/** 빔: 드론(Source) → 대상(Follow) 얇은 원통(적 해킹·단말기 해킹). */
	Beam,
	/** 재부팅: 빔 + 대상 발밑에서 도는 링. */
	Reboot,
};

/**
 *  제미니 드론 기능 연출 한 개(서버 스폰 → 복제, 모든 머신에서 같은 모양을 로컬로 그린다).
 *
 *  - 새 에셋 없음: 엔진 BasicShapes(구체·원통) + 드론 추진기 머티리얼 M_Drone_ThrusterBeam(가산 반투명, ThrusterColor)을
 *    다이나믹 인스턴스로 색만 바꿔 쓴다. 페이드는 가산 색을 어둡게(검정 = 보이지 않음).
 *  - 소리도 기존 SFX 재사용(로컬 재생 — 로컬 협동은 서버 머신이 화면, 원격 클라이언트는 복제된 스폰에서 재생).
 *  - 서버가 Duration 뒤 제거(LifeSpan). 위치는 Follow 대상(없으면 Origin)을 각 머신이 매 틱 따라간다(이동 복제 없음).
 */
UCLASS(NotPlaceable, Transient)
class ACXJeminiFx : public AActor
{
	GENERATED_BODY()

public:
	ACXJeminiFx();

	/** 서버: 연출 스폰. Follow = 붙을 액터(없으면 Origin 고정), Source = 빔 시작(드론). */
	static ACXJeminiFx* Spawn(UWorld* World, ECXJeminiFxType InType, const FVector& InOrigin, AActor* InFollow, AActor* InSource, const FLinearColor& InColor, float InDuration);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	ECXJeminiFxType GetFxType() const { return Type; }
	AActor* GetFollow() const { return Follow; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(Replicated)
	ECXJeminiFxType Type = ECXJeminiFxType::None;

	UPROPERTY(Replicated)
	FVector_NetQuantize Origin;

	UPROPERTY(Replicated)
	TObjectPtr<AActor> Follow;

	UPROPERTY(Replicated)
	TObjectPtr<AActor> Source;

	UPROPERTY(Replicated)
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(Replicated)
	float Duration = 1.0f;

	UPROPERTY(VisibleAnywhere, Category = "Jemini|Fx")
	TObjectPtr<USceneComponent> Root;

	/** 기둥(펄스) / 빔(해킹·재부팅). 엔진 원통. */
	UPROPERTY(VisibleAnywhere, Category = "Jemini|Fx")
	TObjectPtr<UStaticMeshComponent> Pillar;

	/** 보호막 구체. */
	UPROPERTY(VisibleAnywhere, Category = "Jemini|Fx")
	TObjectPtr<UStaticMeshComponent> Bubble;

	/** 링·반짝이 입자(작은 구체). */
	UPROPERTY(VisibleAnywhere, Category = "Jemini|Fx")
	TArray<TObjectPtr<UStaticMeshComponent>> Motes;

	UPROPERTY(VisibleAnywhere, Category = "Jemini|Fx")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	/** 시작음(기존 SFX 재사용, 비면 무음). */
	UPROPERTY(EditDefaultsOnly, Category = "Jemini|Fx")
	TObjectPtr<USoundBase> PulseSound;

	UPROPERTY(EditDefaultsOnly, Category = "Jemini|Fx")
	TObjectPtr<USoundBase> SparkleSound;

	UPROPERTY(EditDefaultsOnly, Category = "Jemini|Fx")
	TObjectPtr<USoundBase> ShieldSound;

	UPROPERTY(EditDefaultsOnly, Category = "Jemini|Fx")
	TObjectPtr<USoundBase> BeamSound;

	UPROPERTY(EditDefaultsOnly, Category = "Jemini|Fx")
	TObjectPtr<USoundBase> RebootSound;

private:
	FVector GetAnchor() const;
	void SetGlow(float Strength);
	void TickPulse(float T);
	void TickSparkle(float T);
	void TickShield(float InElapsed);
	void TickBeam(float T, bool bRing);

	float Elapsed = 0.0f;
	/** 대상의 캡슐 반높이(발밑 계산). */
	float AnchorHalfHeight = 90.0f;
};
