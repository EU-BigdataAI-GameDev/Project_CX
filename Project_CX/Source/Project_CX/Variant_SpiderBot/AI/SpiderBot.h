// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CombatDamageable.h"
#include "CXHackTarget.h"
#include "SpiderBot.generated.h"

class UCXHealthComponent;
class UAnimSequence;
class UAudioComponent;
class UMaterialInterface;
class UParticleSystem;
class UParticleSystemComponent;
class UPointLightComponent;
class USoundAttenuation;
class USoundBase;
class USoundClass;

/** 거미로봇 상태(전투 시스템 기획 7.2). 복제 — 애님/사운드/연출용. */
UENUM(BlueprintType)
enum class ESpiderBotState : uint8
{
	Idle,
	Chase,
	Attack,
	Dead,
	/** 여동생 해킹 성공 → 그로기(이동·공격 정지, StunDuration초 뒤 Chase). 레벨에 저장된 값과 섞이지 않게 맨 뒤에 추가. */
	Stunned
};

/**
 *  거미로봇(근접 적 1종) — .claude/Plans/전투 시스템 기획.md 4.2, 7장.
 *
 *  - 판단은 서버 Tick의 단순 상태 기계: Idle(0.25초마다 감지 1500cm + LOS) → Chase(가장 가까운 생존 플레이어, 0.5초마다 재평가,
 *    2500cm 밖이면 해제) → Attack(접촉 중 톱 내려찍기 루프 — 쿨타임 없음, ChopPeriod마다 타격 프레임에 ApplyDamage 30,
 *    대상이 범위를 벗어나거나 죽으면 Chase) → Dead(체력 0, 래그돌, 3초 뒤 제거). 연속 피해는 플레이어 피격 무적 1초가 거른다.
 *  - 이동: NavMesh 없이 AddMovementInput(대상 쪽 수평 방향). 플레이어와 같은 레인(깊이 방향도 자유)이며 발판 끝에서 떨어지지 않는다.
 *    컨트롤러는 기본 AAIController(AutoPossessAI) — 판단 로직은 폰에 있다.
 *  - 피해: ICombatDamageable::ApplyDamage(총알 진입점) → 엔진 TakeDamage → UCXHealthComponent(체력 50 — 총알 10 × 5발).
 *    피격 반응은 짧은 붉은 점멸만(넉백 없음).
 *  - 애니메이션: 공격 = AS_SpiderBot_Attack(언리얼 에디터 Python으로 Idle 포즈 기반 생성 — saw_base/saw_elbow 내려찍기 +
 *    saw_blade 회전 + body 숙임) 루프 재생. 걷기는 임시(이동 중 Idle 재생 속도↑), 사망은 피직스 에셋 래그돌.
 *  - 여동생 해킹 대상(ICXHackTarget): 화살표 시퀀스 퍼즐(5칸, 4초) 성공 → 서버에서 ApplyHackStun → Stunned 3초(해킹 시스템 기획 10장).
 *    그로기 중: 이동·공격 정지, 재해킹 불가, 총알 피해는 그대로. 연출(모든 머신, 복제된 State·StunEndTime 기준) =
 *    톱 팔이 떨어진 자세로 멈춤 + 경련, 머티리얼 정전 깜빡임, 청록/자홍 전기 불빛, 스파크 파티클·소리, 끝날 때 재부팅음.
 *  - 레벨에 C++ 클래스를 바로 배치할 수 있도록 메시/애님/사운드 기본값을 생성자에서 지정한다(BP 불필요).
 */
UCLASS()
class ASpiderBot : public ACharacter, public ICombatDamageable, public ICXHackTarget
{
	GENERATED_BODY()

public:
	ASpiderBot();

	// ICombatDamageable
	virtual void ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse) override;
	virtual void HandleDeath() override;
	virtual void ApplyHealing(float Healing, AActor* Healer) override;

	// ICXHackTarget
	virtual bool IsHackTargetAvailable() const override;
	virtual float GetHackInteractRange() const override { return HackInteractRange; }
	virtual bool ApplyHackSuccess(APawn* Hacker) override { return ApplyHackStun(Hacker); }
	virtual float GetHackStunRemaining() const override;
	virtual float GetHackStunDuration() const override { return StunDuration; }
	virtual FString GetHackTargetName() const override { return TEXT("SPIDER-BOT"); }

	/** 그로기 시작(서버 전용). 사망·이미 그로기면 false. 디버그 명령 CXStunSpider도 여기로 온다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "SpiderBot|Hack")
	bool ApplyHackStun(APawn* Hacker);

	UFUNCTION(BlueprintPure, Category = "SpiderBot|Hack")
	bool IsStunned() const { return State == ESpiderBotState::Stunned; }

	/**
	 *  서버: 제미니 미끼(Decoy, 제미니 기능 기획 2-7). Duration초 동안 플레이어 대신 Lure(드론) 아래 바닥 쪽으로 이동하고
	 *  톱 공격은 하지 않는다. 끝나면 기존 감지 규칙으로 복귀. 그로기 중에 걸면 남은 시간은 그로기 뒤에 이어진다. 사망이면 false.
	 */
	bool ApplyDecoy(AActor* Lure, float Duration);

	/** 미끼에 끌려가는 중(서버 판단용). */
	bool IsDecoyed() const;

	UFUNCTION(BlueprintPure, Category = "SpiderBot")
	ESpiderBotState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "SpiderBot")
	bool IsDead() const { return State == ESpiderBotState::Dead; }

	/** 기능 B: 죽을 때 떨어뜨릴 증거 id(없으면 None). */
	FName GetDropEvidenceId() const { return DropEvidenceId; }

	UFUNCTION(BlueprintPure, Category = "SpiderBot")
	UCXHealthComponent* GetHealthComponent() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "SpiderBot")
	APawn* GetTarget() const { return Target.Get(); }

	/** 공격 루프 중 다음 타격(내려찍기)까지 남은 시간(초). 공격 중이 아니면 -1. */
	UFUNCTION(BlueprintPure, Category = "SpiderBot")
	float GetTimeToNextChop() const;

	/** 테스트용 상태 문자열. */
	UFUNCTION(BlueprintPure, Category = "SpiderBot|Debug")
	FString GetDebugState() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpiderBot")
	TObjectPtr<UCXHealthComponent> Health;

	/** 이동 루프 소리(이동 중에만 재생, 속도로 피치). */
	UPROPERTY(VisibleAnywhere, Category = "SpiderBot|Sound")
	TObjectPtr<UAudioComponent> MoveAudio;

	// ------------------------------------------------------------ 수치(전투 시스템 기획 4.2 — 제안값, 플레이 후 조정)

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float MaxHealth = 50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float AttackDamage = 30.0f;

	/**
	 *  톱 내려찍기 1회 주기(초) = 공격 애님 1루프. 쿨타임 없이 접촉 중 계속 반복한다.
	 *  플레이어 피격 무적(1.0초)보다 조금 길게 — 무적 해제와 같은 프레임에 타격이 겹쳐 한 번 건너뛰는 일이 없도록.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats", meta = (ClampMin = "0.2"))
	float ChopPeriod = 1.1f;

	/** 한 주기 안에서 피해가 들어가는 시점(0~1) — AS_SpiderBot_Attack의 내려찍기 끝 프레임 17/24. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ChopImpactFraction = 17.0f / 24.0f;

	/** 공격 해제 여유(cm): 접촉 판정 + 이 거리까지 벗어나야 공격을 멈춘다(경계에서 공격/추적 깜빡임 방지). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float AttackReleaseSlack = 30.0f;

	/** 감지 반경(cm) — LOS 필요. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float DetectRadius = 1500.0f;

	/** 추적 해제 반경(cm) — 감지보다 크게(왕복 깜빡임 방지). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float LoseRadius = 2500.0f;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float MoveSpeed = 350.0f;

	/** 접촉 판정: 두 캡슐 표면 사이가 이 거리(cm) 이하이면 접촉(기획: 몸통 캡슐 + 20cm). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float ContactReach = 20.0f;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float IdleSenseInterval = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float RetargetInterval = 0.5f;

	/** 사망 후 제거(초). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stats")
	float CorpseLifeSpan = 3.0f;

	/** 기능 B(WARDEN 설득): 이 거미가 죽으면 떨어뜨릴 증거 id("E1"~"E3", 비우면 없음). 레벨에 놓인 인스턴스에서 지정 — ACXEvidencePickup 스폰. */
	UPROPERTY(EditAnywhere, Category = "SpiderBot|Evidence")
	FName DropEvidenceId;

	/** 여동생 해킹 가능 거리(cm). 2026-11-04: 300 → 450(전투 중 접근 부담 완화). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Hack")
	float HackInteractRange = 450.0f;

	/** 해킹 성공 그로기 시간(초) — 여동생 기획서 5초 → 3초로 변경(전투 시스템 기획 13장 7). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Hack", meta = (ClampMin = "0.1"))
	float StunDuration = 3.0f;

	// ------------------------------------------------------------ 그로기 연출(기존 에셋만 — 새 머티리얼 없음)

	/** 그로기 자세: AttackAnim의 이 시점(0~1, 내려찍은 끝 프레임 17/24 = 톱 팔이 가장 낮음)에서 멈춘다. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	float StunPoseFraction = 17.0f / 24.0f;

	/** 그로기 중 몸이 주저앉는 깊이(cm, 메시만 — 캡슐 불변). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	float StunSlumpDepth = 9.0f;

	/** 경련: 이 간격(초) 범위에서 랜덤으로 짧게 떨린다. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	FVector2D StunTwitchInterval = FVector2D(0.18f, 0.5f);

	/** 정전 깜빡임: 모든 슬롯을 이 머티리얼로 잠깐 바꾼다(기본 Mat_Black — 꺼진 듯). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	TObjectPtr<UMaterialInterface> StunDropoutMaterial;

	/** 과부하 번쩍임: 모든 슬롯을 이 머티리얼로 잠깐(기본 HitFlashMaterial = Mat_EyeGlow). 비면 HitFlashMaterial. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	TObjectPtr<UMaterialInterface> StunSurgeMaterial;

	/** 전기 불빛 두 색(청록/자홍 — 해킹 색)이 번갈아 깜빡인다. 스켈레탈용 발광 머티리얼이 없어 라이트로 표현. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	FLinearColor StunLightColorA = FLinearColor(0.0f, 0.85f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	FLinearColor StunLightColorB = FLinearColor(1.0f, 0.2f, 0.65f);

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	float StunLightIntensity = 9000.0f;

	/** 스파크 파티클(SoulCity P_WeldSparks). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	TObjectPtr<UParticleSystem> StunSparksTemplate;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	float StunSparksScale = 0.35f;

	/** 그로기 시작: 전원 차단음(SW_Gate_Off). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	TObjectPtr<USoundBase> StunStartSound;

	/** 그로기 중 루프: 스파크(SoulCity SparkLoop01 — 톱날 SparkLoop02와 구분). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	TObjectPtr<USoundBase> StunLoopSound;

	/** 그로기 끝: 재부팅(SW_UI_NeonBuzz — 네온 켜지는 소리). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Stun")
	TObjectPtr<USoundBase> RebootSound;

	// ------------------------------------------------------------ 연출(임시 애니메이션)

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Visual")
	TObjectPtr<UAnimSequence> IdleAnim;

	/** 공격 루프 애님(톱 내려찍기). 재생 속도를 ChopPeriod에 맞춘다. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Visual")
	TObjectPtr<UAnimSequence> AttackAnim;

	/** 이동 속도 최대일 때 Idle 재생 속도(걷기 대용). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Visual")
	float MovePlayRate = 2.6f;

	/** 피격 점멸: 모든 머티리얼 슬롯을 이 머티리얼로 잠깐 바꾼다(기본 Mat_EyeGlow — 붉은 발광). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Visual")
	TObjectPtr<UMaterialInterface> HitFlashMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Visual")
	float HitFlashTime = 0.1f;

	// ------------------------------------------------------------ 사운드(효과음 적용 기획 2.4 — 기존 에셋 재사용, 비면 무음)

	/** SFX_Spider_Move: 이동 루프(SoulCity Shake_Metal01). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundBase> MoveLoopSound;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	float MoveLoopVolume = 0.35f;

	/** SFX_Spider_Step: 다리 금속 딸깍(이동 거리 StepLength마다 — 애님 노티파이 대신). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundBase> StepSound;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	float StepLength = 70.0f;

	/** SFX_Spider_Attack_Windup: 맞는 에셋 없음 → 빈 슬롯. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundBase> AttackWindupSound;

	/** SFX_Spider_Attack_Saw: 공격 루프(Attack 상태) 동안 반복 재생(SoulCity SparkLoop02). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundBase> AttackSawSound;

	/** SFX_Spider_Attack_Hit: 플레이어 피격음(UCXHealthComponent)과 겹쳐 빈 슬롯. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundBase> AttackHitSound;

	/** SFX_Spider_Alert: 대상 발견(SW_UI_Denied를 3D·게임플레이 사운드 클래스로). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundBase> AlertSound;

	/** SFX_Spider_Hit: 피격 금속 덜컹(SoulCity MetalPanel_ThinRattle 01~06, 랜덤). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TArray<TObjectPtr<USoundBase>> HitSounds;

	/** SFX_Spider_Destroy: 전기 꺼짐(SW_Gate_Off, 낮은 피치). */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundBase> DestroySound;

	/** SoulCity·UI 원본 에셋을 고치지 않고 게임플레이 사운드 클래스/감쇠를 적용하기 위한 덮어쓰기. */
	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundClass> SfxClass;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundAttenuation> SmallAttenuation;

	UPROPERTY(EditDefaultsOnly, Category = "SpiderBot|Sound")
	TObjectPtr<USoundAttenuation> MediumAttenuation;

private:
	// 서버 판단
	void ServerThink(float DeltaSeconds);
	void SetState(ESpiderBotState NewState);
	/** 감지 반경 안 + LOS인 가장 가까운 생존 플레이어. */
	APawn* FindDetectTarget() const;
	/** 해제 반경 안의 가장 가까운 생존 플레이어(현재 대상 유지 판단용, LOS 불필요). */
	APawn* FindNearestLivingPlayer(float Radius) const;
	bool HasLineOfSight(const APawn* Pawn) const;
	bool IsTouching(const APawn* Pawn, float ExtraReach = 0.0f) const;
	static bool IsValidTarget(const APawn* Pawn);
	void FaceTowards(const FVector& Location, float DeltaSeconds, bool bInstant);
	void StartAttack(APawn* Victim);
	/** 타격 프레임(서버): 대상에 피해 30 — 무적·사망 중이면 엔진(CanBeDamaged)이 거른다. */
	void PerformChop(APawn* Victim);

	// 체력
	void HandleHealthChanged(float NewHealth, float Delta, AActor* DamageInstigator);
	void HandleDepleted();

	// 연출(모든 머신)
	UFUNCTION()
	void OnRep_State(ESpiderBotState PreviousState);
	void ApplyStateVisuals(ESpiderBotState PreviousState);
	void EnterDeadVisuals();
	void UpdateLocomotionVisuals(float DeltaSeconds);
	void PlayStateAnimation();
	void StartHitFlash();
	void EndHitFlash();
	void EnterStunVisuals();
	/** bReboot = 정상 복귀(재부팅음 + Idle 애님). 사망으로 끝날 때는 false. */
	void ExitStunVisuals(bool bReboot);
	void UpdateStunVisuals(float DeltaSeconds);
	void SetAllMaterials(UMaterialInterface* Material);
	float GetServerTime() const;
	void PlaySfx(USoundBase* Sound, USoundAttenuation* Attenuation, float Volume = 1.0f, float Pitch = 1.0f) const;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	ESpiderBotState State = ESpiderBotState::Idle;

	/** 그로기가 끝나는 서버 월드 시각(복제 — 모든 머신의 HUD 카운트다운). */
	UPROPERTY(Replicated)
	float StunEndTime = 0.0f;

	TWeakObjectPtr<APawn> Target;

	/** 제미니 미끼(서버): 끌려갈 액터와 끝나는 서버 시각. */
	TWeakObjectPtr<AActor> DecoyLure;
	float DecoyEndTime = 0.0f;
	/** 서버: 미끼 이동. 미끼가 끝났으면 false(일반 판단으로). */
	bool TickDecoy(float DeltaSeconds);

	float SenseTimer = 0.0f;
	float RetargetTimer = 0.0f;
	float StateTime = 0.0f;
	/** 이번 공격 루프에서 타격한 횟수(서버). */
	int32 ChopsDone = 0;

	// 로컬 연출 상태
	FVector MeshBaseLocation = FVector::ZeroVector;
	float StepAccum = 0.0f;
	bool bDeadVisualsApplied = false;
	FTimerHandle HitFlashTimer;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> SawAudio;

	/** 그로기 전기 불빛(평소 꺼짐). */
	UPROPERTY(VisibleAnywhere, Category = "SpiderBot|Stun")
	TObjectPtr<UPointLightComponent> StunLight;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> StunAudio;

	UPROPERTY(Transient)
	TObjectPtr<UParticleSystemComponent> StunSparks;

	// 그로기 로컬 연출 상태
	bool bStunVisualsActive = false;
	float StunFlickerTimer = 0.0f;
	float StunTwitchTimer = 0.0f;
	float StunTwitchLeft = 0.0f;
};
