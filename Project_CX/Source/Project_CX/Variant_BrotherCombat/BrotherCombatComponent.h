// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BrotherCombatComponent.generated.h"

class ABrotherBullet;
class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class UAudioComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
class APawn;
class UAnimSequence;

/**
 *  오빠 캐릭터의 자동 조준 사격 로직(.claude/Plans/자동 조준 사격 구현.md).
 *
 *  BP_Brother에 컴포넌트로 추가해서 사용한다 — BP_Brother의 부모 클래스(Character)는 건드리지 않는다.
 *  입력 바인딩도 이 컴포넌트가 직접 한다(Blueprint Event Graph 배선 불필요) — FireAction/AimAction에
 *  IA_Fire/IA_Aim 에셋만 지정해주면 된다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UBrotherCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBrotherCombatComponent();

	/** 라이브 튜닝(CXGunTune)용: 총 소품의 소켓 기준 상대 트랜스폼을 즉시 바꾼다. GunMesh가 있으면 GunComponent만 갱신. */
	void SetGunRelativeTransformLive(const FTransform& NewTransform);

	const FTransform& GetGunRelativeTransform() const { return GunRelativeTransform; }

	/** IA_Aim(Started)에서 호출. 가장 가까운 적을 찾아 조준 상태로 전환한다. 적이 없으면 조준 상태에 들어가지 않는다. */
	UFUNCTION(BlueprintCallable, Category = "Brother|Combat")
	void StartAim();

	/** IA_Aim(Completed)에서 호출. 조준 상태를 해제한다. */
	UFUNCTION(BlueprintCallable, Category = "Brother|Combat")
	void StopAim();

	/**
	 *  IA_Fire(Started)에서 호출. 조준 중이면 현재 타겟 방향으로, 아니면 그 순간 가장 가까운 적을
	 *  즉시 찾아 그 방향으로(적이 없으면 캐릭터 정면으로) 총알을 발사한다. 장전 중이면 무시된다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Brother|Combat")
	void Fire();

	UPROPERTY(BlueprintReadOnly, Category = "Brother|Combat")
	bool bIsAiming = false;

	UPROPERTY(BlueprintReadOnly, Category = "Brother|Combat")
	TObjectPtr<AActor> CurrentTarget;

	UPROPERTY(BlueprintReadOnly, Category = "Brother|Combat")
	int32 CurrentAmmo = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Brother|Combat")
	bool bIsReloading = false;

	/** HUD용: 장전 진행도(0~1). 장전 중이 아니면 0. */
	UFUNCTION(BlueprintPure, Category = "Brother|Combat")
	float GetReloadProgress() const;

	UFUNCTION(BlueprintPure, Category = "Brother|Combat")
	int32 GetMaxAmmo() const { return MaxAmmo; }

	/**
	 *  서버: 제미니 재장전 지원(.claude/Plans/제미니 AI봇 기획.md B3) — 오빠는 이것으로만 재장전한다(자동 재장전 없음).
	 *  오빠를 조작하는 머신에서 장전(ReloadTime초)을 시작한다. 이미 장전 중이거나 탄창이 가득이면 false.
	 */
	bool RequestJeminiReload();

	/** 마지막으로 쏜 월드 시각(쏜 적 없으면 음수). 튜토리얼 "첫 사격" 완료 판정용(UCXTutorialSubsystem). */
	double GetLastShotWorldTime() const { return LastShotWorldTime; }

	/** HUD: 탄약 0 + 장전 중 아님 → 제미니에게 재장전 요청 안내. */
	bool NeedsJeminiReload() const { return CurrentAmmo <= 0 && !bIsReloading; }

	// ------------------------------------------------------------ 제미니 공격력 강화(.claude/Plans/Jemini_plans/02_기능별_상세.md 2-5)

	/**
	 *  서버: 레일건 피해 x Multiplier를 Duration초. 이미 강화 중이면 남은 시간에 Duration을 더한다(연장).
	 *  피해 배율은 서버 SpawnBullet에서만 적용(클라이언트 위조 불가). 사망 시 ClearDamageBoost(UCXPlayerLifeComponent).
	 */
	void ApplyDamageBoost(float Duration, float Multiplier);

	/** 서버: 강화 즉시 해제. */
	void ClearDamageBoost();

	/** 강화 남은 시간(초, 모든 머신 — 복제된 서버 시각 기준). 0이면 비활성. */
	UFUNCTION(BlueprintPure, Category = "Brother|Combat")
	float GetDamageBoostRemaining() const;

	/** 지금 적용 중인 피해 배율(강화 아니면 1). */
	UFUNCTION(BlueprintPure, Category = "Brother|Combat")
	float GetDamageMultiplier() const { return GetDamageBoostRemaining() > 0.0f ? DamageBoostMultiplier : 1.0f; }

	// ------------------------------------------------------------ 네이티브 AnimInstance(UBrotherAnimInstance)

	/** true면 BeginPlay에서 메시의 AnimInstance를 네이티브 UBrotherAnimInstance로 교체한다(false: BP의 ABP_Brother 유지). */
	UPROPERTY(EditAnywhere, Category = "Brother|Anim")
	bool bUseNativeAnim = true;

	/** 네이티브 AnimInstance용 애니메이션(생성자에서 경로로 찾는다. 에디터에서 바꿀 수 있다). */
	UPROPERTY(EditAnywhere, Category = "Brother|Anim")
	TObjectPtr<UAnimSequence> NativeIdleAnim;

	UPROPERTY(EditAnywhere, Category = "Brother|Anim")
	TObjectPtr<UAnimSequence> NativeRunAnim;

	UPROPERTY(EditAnywhere, Category = "Brother|Anim")
	TObjectPtr<UAnimSequence> NativeJumpAnim;

	/** 사격 조준 자세(오른팔 올리기) 사용 여부. 끄면 GetFireAimAlpha와 무관하게 팔을 올리지 않는다. */
	UPROPERTY(EditAnywhere, Category = "Brother|Anim|AimPose")
	bool bEnableAimPose = true;

	/** RightArm 본 공간 회전(Add To Existing). FRotator의 Roll=X, Pitch=Y, Yaw=Z — Transform(Modify) Bone 핀과 같다. 알파(FireAimAlpha)로 가중. */
	UPROPERTY(EditAnywhere, Category = "Brother|Anim|AimPose")
	FRotator RightArmAimRotation = FRotator(50.0f, 0.0f, -90.0f);

	/** RightForeArm 본 공간 회전(Add To Existing). Roll=X, Pitch=Y, Yaw=Z. */
	UPROPERTY(EditAnywhere, Category = "Brother|Anim|AimPose")
	FRotator RightForeArmAimRotation = FRotator(70.0f, 0.0f, 0.0f);

	const UAnimSequence* GetNativeIdleAnim() const { return NativeIdleAnim; }
	const UAnimSequence* GetNativeRunAnim() const { return NativeRunAnim; }
	const UAnimSequence* GetNativeJumpAnim() const { return NativeJumpAnim; }
	bool IsAimPoseEnabled() const { return bEnableAimPose; }
	/** 애니메이션 쪽 조회용: 조준 자세 알파(비활성이면 0). */
	float GetAimPoseAlpha() const { return bEnableAimPose ? FireAimAlpha : 0.0f; }
	const FRotator& GetRightArmAimRotation() const { return RightArmAimRotation; }
	const FRotator& GetRightForeArmAimRotation() const { return RightForeArmAimRotation; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 *  소유 Pawn의 EnhancedInputComponent에 FireAction/AimAction을 바인딩한다. 최초 플레이어 Pawn은
	 *  Possess()가 BeginPlay 이후에 일어나 그 시점엔 InputComponent가 아직 없을 수 있으므로,
	 *  TickComponent에서 바인딩 전까지 매 틱 재시도한다(성공하면 bInputBound=true로 더 이상 시도하지 않음).
	 */
	void TryBindInput();

	/**
	 *  Range 이내 + 시야(LOS)를 만족하는 살아 있는 ICombatDamageable 중 가장 가까운 것을 반환한다. 없으면 nullptr.
	 *  사망한 적(체력 0, 사망 후 3초 남아 있는 시체)은 후보에서 뺀다(전투 시스템 기획 5장).
	 *  bApplyForwardCone이 true면 캐릭터 전방 AimConeHalfAngleDeg 원뿔 밖의 후보는 제외한다
	 *  (근접 자동 조준(5m)은 방향 상관없이 걸려야 하므로 false로 호출).
	 */
	AActor* FindNearestTarget(float Range, bool bApplyForwardCone) const;

	/** 조준·자동 조준 대상이 될 수 있는지(유효 + 피해 가능 + 체력 0 아님). */
	static bool IsTargetAlive(const AActor* Candidate);

	/** CurrentTarget이 있으면 그쪽으로 캐릭터 회전을 보간하고, 이동 방향 자동 정렬(OrientRotationToMovement)을 잠시 끈다. 없으면 원래 값으로 복원한다. */
	void UpdateFacing(class ACharacter* OwnerCharacter, float DeltaTime);

	void StartReload();

	UFUNCTION()
	void FinishReload();

	void SpawnBullet(const FVector& AimDirection);

	/** 원격 클라이언트가 오빠를 조작하는 경우를 위한 발사 RPC — 실제 스폰/데미지는 서버 권위(자동 조준 사격 구현.md 7장). */
	UFUNCTION(Server, Reliable)
	void Server_Fire(FVector_NetQuantizeNormal AimDirection);

	/** 원격 클라이언트가 오빠를 조작할 때: 서버가 승인한 제미니 재장전을 그 머신에서 시작. */
	UFUNCTION(Client, Reliable)
	void Client_JeminiReload();

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	TSubclassOf<ABrotherBullet> BulletClass;

	/** 좌클릭에 매핑된 Input Action(예: IA_Fire). 지정하지 않으면 발사가 바인딩되지 않는다. */
	UPROPERTY(EditAnywhere, Category = "Brother|Combat|Input")
	TObjectPtr<UInputAction> FireAction;

	/** 우클릭에 매핑된 Input Action(예: IA_Aim). 지정하지 않으면 조준이 바인딩되지 않는다. */
	UPROPERTY(EditAnywhere, Category = "Brother|Combat|Input")
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float AimRange = 3000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float AimConeHalfAngleDeg = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float AimRotationInterpSpeed = 10.0f;

	/** 이 범위(기본 5m) 안에 적이 있으면 조준 여부와 무관하게 항상 그 방향으로 캐릭터가 회전하고, 발사도 그 방향으로 나간다. WASD 이동 자체는 그대로 자유롭다. */
	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float ProximityAutoFaceRange = 500.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	int32 MaxAmmo = 30;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float ReloadTime = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	float BulletDamage = 10.0f;

	/** 연사 간격(초). 좌클릭을 빠르게 눌러도 이 간격보다 빨리 쏘지 않는다(2026-11-04 추가 — 발사 속도 완화). */
	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat", meta = (ClampMin = "0.0"))
	float FireInterval = 0.3f;

	/** 사격 중 이동속도 배율(난이도 조정). 발사 순간부터 마지막 발사 후 FireMoveSlowTail초까지 MaxWalkSpeed에 곱한다. 1이면 감속 없음. */
	UPROPERTY(EditAnywhere, Category = "Brother|Combat", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float FireMoveSpeedFactor = 0.5f;

	/** 마지막 발사 후 감속을 유지하는 시간(초). */
	UPROPERTY(EditAnywhere, Category = "Brother|Combat", meta = (ClampMin = "0.0"))
	float FireMoveSlowTail = 0.3f;

	/** 마지막 발사 시각(월드 시간). FireInterval 판정용. */
	double LastFireTime = -1000.0;

	// ------------------------------------------------------------ 임시 권총 + 조준 포즈 알파

	/** 오른손 소켓에 붙는 총 메시. 비워 두면 엔진 기본 큐브로 만든 임시 권총(몸통+총열)을 쓴다. 총은 BeginPlay에서 만들어지므로 바꾼 뒤 PIE를 다시 시작한다. */
	UPROPERTY(EditAnywhere, Category = "Brother|Gun")
	TObjectPtr<UStaticMesh> GunMesh;

	/** 총을 붙일 스켈레탈 본/소켓 이름. */
	UPROPERTY(EditAnywhere, Category = "Brother|Gun")
	FName GunSocketName = TEXT("RightHand");

	/** 소켓 기준 상대 변환. 임시 큐브일 때는 이 Scale에 임시 권총 크기가 추가로 곱해진다. */
	UPROPERTY(EditAnywhere, Category = "Brother|Gun")
	FTransform GunRelativeTransform = FTransform(FRotator::ZeroRotator, FVector(8.0f, 0.0f, 0.0f), FVector::OneVector);

	/** 마지막 발사 후 조준 자세를 유지하는 시간(초). */
	UPROPERTY(EditAnywhere, Category = "Brother|Gun", meta = (ClampMin = "0.0"))
	float AimHoldTime = 0.8f;

	/** 조준 알파가 오르는 속도(FInterpTo 속도). */
	UPROPERTY(EditAnywhere, Category = "Brother|Gun", meta = (ClampMin = "0.1"))
	float AimBlendInSpeed = 14.0f;

	/** 조준 알파가 내려가는 속도. */
	UPROPERTY(EditAnywhere, Category = "Brother|Gun", meta = (ClampMin = "0.1"))
	float AimBlendOutSpeed = 6.0f;

	/** 보간된 사격 조준 알파(0~1). 발사 중과 마지막 발사 후 AimHoldTime 동안 1을 향한다. AnimBP의 팔 본 Transform Modify Bone Alpha용. */
	UFUNCTION(BlueprintPure, Category = "Brother|Gun")
	float GetFireAimAlpha() const { return FireAimAlpha; }

	/** AnimBP 편의용: TryGetPawnOwner 결과(캐스트 불필요)에서 컴포넌트를 찾아 알파를 돌려준다. 없으면 0. */
	UFUNCTION(BlueprintPure, Category = "Brother|Gun")
	static float GetFireAimAlphaForPawn(const APawn* Pawn);


	/** 무기 메시/소켓이 없으므로 캐릭터 기준 상대 오프셋으로 스폰 지점을 계산한다(자동 조준 사격 구현.md 4.2). */
	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat")
	FVector MuzzleOffset = FVector(60.0f, 0.0f, 60.0f);

	// ------------------------------------------------------------ 사운드(효과음 적용 기획 2.2 — 비어 있으면 무음)
	// 발사음·탄 충돌음은 복제되는 총알 액터(ABrotherBullet)가 낸다.

	/** 장전(에너지 충전) 루프. 장전 진행도(GetReloadProgress)로 피치를 ReloadPitchStart → ReloadPitchEnd로 올린다. */
	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat|Sound")
	TObjectPtr<USoundBase> ReloadChargeSound;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat|Sound")
	float ReloadPitchStart = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat|Sound")
	float ReloadPitchEnd = 1.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat|Sound")
	TObjectPtr<USoundBase> ReloadDoneSound;

	/** 탄약 0(장전 중)에 사격 입력. */
	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat|Sound")
	TObjectPtr<USoundBase> DryFireSound;

	/** 탄약이 LowAmmoRatio 이하로 내려가는 순간 1회(HUD LOW CHARGE와 같은 기준). */
	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat|Sound")
	TObjectPtr<USoundBase> LowAmmoSound;

	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat|Sound")
	float LowAmmoRatio = 0.25f;

	/** 우클릭 조준(LOCK)으로 타겟을 새로 잡은 순간. */
	UPROPERTY(EditDefaultsOnly, Category = "Brother|Combat|Sound")
	TObjectPtr<USoundBase> AimLockSound;

	/** 강화가 끝나는 서버 월드 시각(GameState::GetServerWorldTimeSeconds). 0이면 없음. */
	UPROPERTY(Replicated)
	double DamageBoostEndTime = 0.0;

	UPROPERTY(Replicated)
	float DamageBoostMultiplier = 1.0f;

private:
	double GetServerTime() const;
	bool IsLocallyControlledOwner() const;
	void PlayLocal2D(USoundBase* Sound) const;
	void UpdateReloadSound();

	FTimerHandle ReloadTimerHandle;

	/** 장전 루프(재사용). */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ReloadAudio;

	/** 조준 고정음 중복 방지 — 마지막으로 LOCK한 타겟. */
	TWeakObjectPtr<AActor> LastLockTarget;
	bool bInputBound = false;

	/** 게임패드 사격 RT / 조준 LT 런타임 매핑(설정 메뉴 기획 4.5 — IMC_Default에는 마우스만 있다). */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> GamepadMappingContext;

	/** UpdateFacing이 OrientRotationToMovement를 껐다가 되돌릴 때 쓸, 최초 1회 캐싱한 원래 값. */
	bool bDefaultOrientRotationToMovement = true;
	bool bCachedDefaultOrientRotation = false;

	/** 사격 감속: 각 머신이 자기 쪽 Fire/Server_Fire에서 시각을 갱신하고 Tick에서 MaxWalkSpeed를 적용/복원한다(MaxWalkSpeed는 비복제). */
	void NotifyShotFired();
	void UpdateFireMoveSlow(ACharacter* OwnerCharacter);
	double FireSlowEndTime = 0.0;
	/** 마지막 발사 월드 시각(조준 알파 유지 판정). */
	double LastShotWorldTime = -1000.0;
	float FireAimAlpha = 0.0f;
	void UpdateFireAim(float DeltaTime);

	void CreateGunProp();
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> GunComponent;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> GunBarrelComponent;
	bool bFireSlowActive = false;
	float BaseMaxWalkSpeed = 0.0f;
};
