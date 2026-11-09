// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXHealthComponent.generated.h"

class APawn;
class AController;
class UDamageType;
class USoundBase;

/** 체력 변화(모든 머신 — 서버는 즉시, 클라이언트는 OnRep). Delta < 0 = 피해. Instigator는 서버에서만 채워진다. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FCXHealthChangedSignature, float /*NewHealth*/, float /*Delta*/, AActor* /*Instigator*/);
/** 체력 0(서버, 1회). */
DECLARE_MULTICAST_DELEGATE(FCXHealthDepletedSignature);

/**
 *  공용 체력 컴포넌트(.claude/Plans/전투 시스템 기획.md 6.2 권장안 C).
 *
 *  - 전달: 공격자는 엔진 UGameplayStatics::ApplyDamage를 부른다. APawn::TakeDamage가 서버 권위 + CanBeDamaged()를
 *    검사하므로 사망·무적(UCXPlayerLifeComponent가 SetCanBeDamaged(false)) 중에는 여기까지 오지 않는다.
 *  - 보관: 소유 액터의 OnTakeAnyDamage에 바인딩해 체력을 깎는다 → BP_Brother(C++ 부모 없음)도 에셋 수정 없이 동작.
 *  - 플레이어: ALocalCoopGameMode::SetPlayerDefaults가 EnsureOn으로 런타임 부착. 체력 0 → UCXPlayerLifeComponent::Kill(Health),
 *    피격 시 무적 1초(LifeComponent 재사용) + 움찔(메시만 흔들림, 캡슐 위치 그대로) + 피격음.
 *  - 거미로봇: 기본 서브오브젝트로 붙이고 InitMaxHealth(50) + 플레이어용 연출을 끈다(자기 반응은 델리게이트로).
 *  - CurrentHealth는 복제(OnRep에서 각 머신이 피격 연출·HUD 갱신). 회복/부활은 서버 ResetToMax.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UCXHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXHealthComponent();

	/** 폰에 이 컴포넌트가 없으면 붙인다(서버/스탠드얼론에서 호출 — 복제 컴포넌트). */
	static UCXHealthComponent* EnsureOn(APawn* Pawn);

	/** 액터의 체력 컴포넌트(없으면 nullptr). */
	static UCXHealthComponent* FindOn(const AActor* Actor);

	/** 체력 컴포넌트가 있고 0이면 true(자동 조준·총알·HUD 필터용). */
	static bool IsActorDead(const AActor* Actor);

	/** 생성자/스폰 직후용: 최대 체력을 정하고 가득 채운다. */
	void InitMaxHealth(float NewMaxHealth);

	/** 서버: 최대치로 회복(부활 시). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "CX|Health")
	void ResetToMax();

	/**
	 *  서버: 회복(제미니 HealSupport). MaxHealth로 클램프, 사망·부활 대기 중이면 무시. 실제로 오른 양을 반환.
	 *  OnHealthChanged(+Delta)가 브로드캐스트되고 CurrentHealth 복제로 HUD가 갱신된다.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "CX|Health")
	float Heal(float Amount);

	/**
	 *  서버: 보호막(제미니 Shield) — Duration초 동안 모든 피해 무시(피격 무적과 같은 판정, 점멸 없음).
	 *  이미 보호막 중이면 남은 시간을 Duration으로 다시 채운다(연장 아님). 사망 중이면 false.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "CX|Health")
	bool GrantShield(float Duration);

	/** 서버: 보호막 즉시 해제(사망 시). */
	void ClearShield();

	/** 보호막 남은 시간(초, 모든 머신 — 복제된 서버 시각 기준). */
	UFUNCTION(BlueprintPure, Category = "CX|Health")
	float GetShieldRemaining() const;

	UFUNCTION(BlueprintPure, Category = "CX|Health")
	float GetHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "CX|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "CX|Health")
	float GetHealthPercent() const { return MaxHealth > 0.0f ? FMath::Clamp(CurrentHealth / MaxHealth, 0.0f, 1.0f) : 0.0f; }

	UFUNCTION(BlueprintPure, Category = "CX|Health")
	bool IsDead() const { return CurrentHealth <= 0.0f; }

	/** 마지막 피해 이후 경과(로컬 월드 시간, 초). 피해를 받은 적 없으면 큰 값. HUD 플래시·적 HP 바 표시용. */
	UFUNCTION(BlueprintPure, Category = "CX|Health")
	float GetTimeSinceLastDamage() const;

	/** 테스트용 상태 문자열. */
	UFUNCTION(BlueprintPure, Category = "CX|Health|Debug")
	FString GetDebugState() const;

	FCXHealthChangedSignature OnHealthChanged;
	FCXHealthDepletedSignature OnDepleted;

	// ------------------------------------------------------------ 설정(기본값 = 플레이어. 거미로봇은 생성자에서 바꾼다)

	UPROPERTY(EditDefaultsOnly, Replicated, Category = "CX|Health")
	float MaxHealth = 100.0f;

	/** 피격 후 무적(초) — 소유자에게 UCXPlayerLifeComponent가 있을 때만(점멸 포함 재사용). 0이면 없음. */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Health")
	float HitInvulnerableTime = 1.0f;

	/** 체력 0 → UCXPlayerLifeComponent::Kill(ECXDeathCause::Health)(플레이어). */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Health")
	bool bKillOwnerOnDepleted = true;

	/** 움찔: 캐릭터 메시만 뒤로 살짝 흔든다(캡슐 위치 불변 — 넉백 없음, 기획 8장). */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Health|Feedback")
	bool bFlinchOnHit = true;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Health|Feedback")
	float FlinchDuration = 0.15f;

	/** 뒤로 밀리는 최대 거리(cm, 메시 상대 위치). */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Health|Feedback")
	float FlinchDistance = 7.0f;

	/** 피격음(랜덤 1개, 피해 위치 = 소유자). 비어 있으면 무음. 기본값: 레일건 금속 충돌음을 낮은 피치로 재사용. */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Health|Feedback")
	TArray<TObjectPtr<USoundBase>> HurtSounds;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Health|Feedback")
	float HurtPitch = 0.8f;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void HandleTakeAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

	UFUNCTION()
	void OnRep_Health(float OldHealth);

	/** 체력이 바뀐 뒤 공통 처리(서버 즉시 / 클라이언트 OnRep): 피격 연출 + 델리게이트. */
	void NotifyHealthChanged(float OldHealth, AActor* Instigator);
	void StartFlinch();
	void StopFlinch();

	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float CurrentHealth = 100.0f;

	/** 보호막이 끝나는 서버 월드 시각(GameState::GetServerWorldTimeSeconds). 0이면 없음. */
	UPROPERTY(Replicated)
	double ShieldEndTime = 0.0;

	double GetServerTime() const;

	bool bDepleted = false;
	bool bDamageBound = false;
	double LastDamageTime = -1000.0;

	/** 움찔 진행(로컬 연출). */
	bool bFlinching = false;
	float FlinchElapsed = 0.0f;
	FVector FlinchBaseLocation = FVector::ZeroVector;
};
