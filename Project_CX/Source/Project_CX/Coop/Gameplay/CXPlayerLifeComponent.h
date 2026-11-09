// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXPlayerLifeComponent.generated.h"

class APawn;
class AController;
class APlayerController;
class ACharacter;
class USoundBase;

/** 플레이어 생존 상태(게임 오버화면 기획 2장). */
UENUM(BlueprintType)
enum class ECXLifeState : uint8
{
	Alive,
	/** 사망 — 부활 대기 중(10초 카운트다운). */
	AwaitingRespawn,
	/** 사망 — 부활 없음(게임 오버). */
	Dead
};

/** 사망 원인(로그/HUD용). */
UENUM(BlueprintType)
enum class ECXDeathCause : uint8
{
	Generic,
	/** 낙사 — 월드 KillZ 도달. */
	Fall,
	/** 체력 0(UCXHealthComponent). */
	Health,
	/** 디버그 명령(CXKillPlayer). */
	Debug
};

/**
 *  플레이어 폰 생존/사망/부활 상태(.claude/Plans/게임 오버화면 기획.md 2장, 9장).
 *
 *  - 판정은 ALocalCoopGameMode(서버 권위) 한 곳 — 이 컴포넌트는 상태 보관·연출·낙사 감지·안전 지면 기록만 한다.
 *    체력 시스템(UCXHealthComponent)은 체력 0에서 Kill(ECXDeathCause::Health)를 부른다. 부활 시 체력 최대치 회복.
 *  - 사망: 폰을 파괴하지 않고 숨김 + 충돌 끔 + 이동 정지 + 입력 차단(빙의 유지 → HUD/컨트롤러 그대로). 부활 시 순간이동 후 복구.
 *  - 낙사: 서버에서 매 틱 KillZ + FallDeathMargin 아래로 내려가면 사망 처리. 이동 컴포넌트보다 먼저 틱해서
 *    엔진의 KillZ 처리(FellOutOfWorld → 폰 Destroy)보다 앞서 잡는다. 그래도 폰이 파괴되면 EndPlay에서 사망으로 넘긴다.
 *  - 안전 지면: 0.5초마다 정적 지면 위 위치를 링버퍼(5개)에 기록 — 파트너 부활 위치 계산(2.1 기본안)에 쓴다.
 *  - 부활 직후 무적 2초: SetCanBeDamaged(false) + 몸 메시 점멸.
 *  - 상태/부활 시각은 복제(listen server) — ACoopHUD가 읽어 부활 대기 화면을 그린다.
 *
 *  ALocalCoopGameMode::SetPlayerDefaults가 두 폰에 런타임으로 붙인다(UCXPlayerAudioComponent::EnsureOn과 같은 방식).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UCXPlayerLifeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXPlayerLifeComponent();

	/** 폰에 이 컴포넌트가 없으면 붙인다(서버/스탠드얼론에서 호출). */
	static UCXPlayerLifeComponent* EnsureOn(APawn* Pawn);

	/** 컴포넌트가 없거나 살아 있으면 true. */
	static bool IsPawnAlive(const APawn* Pawn);

	/** 이 폰을 죽인다(서버) — 게임모드 NotifyPlayerDied로 넘긴다. 체력 시스템/킬존 볼륨이 호출할 진입점. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "CX|Life")
	void Kill(ECXDeathCause Cause = ECXDeathCause::Generic);

	UFUNCTION(BlueprintPure, Category = "CX|Life")
	ECXLifeState GetLifeState() const { return LifeState; }

	UFUNCTION(BlueprintPure, Category = "CX|Life")
	bool IsAlive() const { return LifeState == ECXLifeState::Alive; }

	UFUNCTION(BlueprintPure, Category = "CX|Life")
	bool IsInvulnerable() const { return bInvulnerable; }

	/** 부활까지 남은 시간(초, 월드 일시정지 중 멈춤). 부활 대기 중이 아니면 0. */
	UFUNCTION(BlueprintPure, Category = "CX|Life")
	float GetRespawnRemaining() const;

	float GetRespawnDuration() const { return RespawnDuration; }

	/** 마지막 부활 후 경과(로컬 월드 시간). 부활한 적 없으면 큰 값. HUD의 REBOOTED 표시용. */
	float GetTimeSinceRevive() const;

	ECXDeathCause GetLastDeathCause() const { return LastDeathCause; }

	// --- 게임모드(서버)가 호출 ---

	/** 사망 상태로 전환. RespawnDelay < 0이면 부활 없음(게임 오버). */
	void EnterDeath(ECXDeathCause Cause, float RespawnDelay);

	/** 부활 대기 → 부활 없음(게임 오버 확정). */
	void CancelRespawn();

	/** 지정 위치에 부활 + 무적. */
	void Revive(const FVector& Location, const FRotator& Rotation, float InvulnerableTime);

	/** 새로 스폰된 폰(파괴 후 재스폰)에 무적만 건다. */
	void StartInvulnerability(float Seconds);

	/**
	 *  이 폰(살아 있는 파트너) 근처에서 Respawner가 설 수 있는 안전한 지면을 찾는다(기획 2.1).
	 *  최근 안전 지면 기록부터 파트너 뒤쪽(진행 반대, 스크롤 축) 1~2m → 앞쪽 순으로 시도, 막히면 이전 기록으로 후퇴.
	 */
	bool FindRespawnSpotFor(const APawn* Respawner, FVector& OutLocation, FRotator& OutRotation) const;

	/** 테스트용 상태 문자열. */
	UFUNCTION(BlueprintPure, Category = "CX|Life|Debug")
	FString GetDebugState() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** KillZ보다 이만큼(cm) 위에서 낙사 확정(엔진 KillZ 파괴보다 먼저). */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Life")
	float FallDeathMargin = 100.0f;

	/** 안전 지면 기록 간격(초)과 개수. */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Life|SafeGround")
	float SafeGroundInterval = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Life|SafeGround")
	int32 SafeGroundCount = 5;

	/** KillZ에서 이 높이(cm) 이상 떨어진 지면만 안전 지면으로 기록. */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Life|SafeGround")
	float SafeGroundKillZClearance = 400.0f;

	/** 파트너 뒤쪽 부활 거리 후보(cm, 스크롤 축). 음수 = 파트너 뒤(진행 반대), 양수 = 앞. 파트너와 겹치지 않게 0은 두지 않는다. */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Life|SafeGround")
	TArray<float> RespawnOffsets = { -150.0f, -100.0f, -200.0f, 150.0f, 100.0f };

	/** 무적 점멸 주기(Hz). */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Life")
	float BlinkRate = 10.0f;

	/** 사망/부활 효과음(에셋 없음 → 무음). */
	UPROPERTY(EditDefaultsOnly, Category = "CX|Life|Sound")
	TObjectPtr<USoundBase> DeathSound;

	UPROPERTY(EditDefaultsOnly, Category = "CX|Life|Sound")
	TObjectPtr<USoundBase> ReviveSound;

private:
	UFUNCTION()
	void OnRep_LifeState(ECXLifeState PreviousState);

	UFUNCTION()
	void OnRep_Invulnerable();

	/** 숨김/충돌/이동/입력 — 서버와 각 머신 공통으로 상태에 맞춘다. */
	void ApplyLifeStateVisuals(bool bDead);
	void SetBodyBlinkHidden(bool bHidden);
	void CheckFallDeath();
	/** 진행 방향(사이드뷰 스크롤 축 위 +/-) — 카메라 오른쪽 축에 바라보는 방향/속도를 투영. */
	static FVector ComputeProgressDirection(const ACharacter* Character);
	void RecordSafeGround();
	AController* ResolveController() const;
	bool IsServer() const;

	UPROPERTY(ReplicatedUsing = OnRep_LifeState)
	ECXLifeState LifeState = ECXLifeState::Alive;

	/** 부활 시각(서버 월드 시간 — GameState::GetServerWorldTimeSeconds 기준). */
	UPROPERTY(Replicated)
	double RespawnEndTime = 0.0;

	UPROPERTY(Replicated)
	float RespawnDuration = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Invulnerable)
	bool bInvulnerable = false;

	UPROPERTY(Replicated)
	ECXDeathCause LastDeathCause = ECXDeathCause::Generic;

	struct FSafeGroundEntry
	{
		FVector Location = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
	};
	TArray<FSafeGroundEntry> SafeGround;
	float SafeGroundAccum = 0.0f;

	double InvulnerableEndTime = 0.0;
	double LastReviveLocalTime = -1000.0;
	bool bBlinkHidden = false;
	bool bReportedDeath = false;
	bool bSavedCanBeDamaged = true;

	TWeakObjectPtr<AController> CachedController;
};
