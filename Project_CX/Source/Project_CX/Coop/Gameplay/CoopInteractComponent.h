// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CoopInteractable.h"
#include "CoopInteractComponent.generated.h"

class APawn;
class UInputAction;
class UInputMappingContext;

/**
 *  두 캐릭터 공용 상호작용 입력(키보드 F / 게임패드 X) — .claude/Plans/게임 클리어 화면 기획.md 2.1.
 *
 *  - 입력은 에셋 없이 런타임 Input Action/Mapping Context(IMC_CoopInteract)로 이 폰을 조작하는 로컬 플레이어에 추가한다.
 *    (SisterHackComponent·BrotherCombatComponent와 같은 방식 — 조작 장치 설정에 따라 키보드/패드가 어느 로컬 플레이어로 가든 그대로 따른다)
 *  - 0.2초마다 범위 안 ICoopInteractable 중 CanInteract가 참인 가장 가까운 대상을 포커스로 잡는다(HUD 안내용).
 *  - 실행은 서버 권위: 클라이언트면 Server RPC, 서버가 CanInteract를 다시 검증한 뒤 Interact.
 *
 *  F/X 우선순위(한 번 누르면 한 동작만):
 *    1) 여동생이 해킹 중이면 해킹 입력(비밀번호 제출 등)
 *    2) 해킹 가능한 단말기가 포커스면 해킹 시작
 *    3) 그 외에는 포커스된 상호작용 대상(문 버튼 등)
 *    4) 아무것도 없으면 적 해킹(ICXHackTarget, 거미로봇 — 화살표 5칸·4초 퍼즐 → 성공 시 그로기 3초, 해킹 시스템 기획 10장)
 *  → 해킹 키(F/X)는 이 컴포넌트 하나만 바인딩하고 USisterHackComponent::Interact로 넘긴다(같은 키 이중 바인딩 없음).
 *
 *  ASisterCharacter는 생성자에서 붙이고, 오빠(BP_Brother, 부모 Character)는 ALocalCoopGameMode::SetPlayerDefaults가 런타임에 붙인다.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UCoopInteractComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCoopInteractComponent();

	/** 폰에 이 컴포넌트가 없으면 붙인다(서버/스탠드얼론에서 호출 — 복제 컴포넌트). */
	static UCoopInteractComponent* EnsureOn(APawn* Pawn);

	/** 폰의 협동 역할. 여동생 = ASisterCharacter, 오빠 = UBrotherCombatComponent를 가진 폰. */
	UFUNCTION(BlueprintPure, Category = "Coop|Interact")
	static ECoopCharacterRole GetPawnRole(const APawn* Pawn);

	/**
	 *  살아 있고 조작 중인 폰인지. 체력/사망·부활 시스템이 생기면 여기서 사망·부활 대기 상태를 막는다
	 *  (지금은 유효성, 컨트롤러 유무, 숨김(부활 대기 연출용) 여부만 본다).
	 */
	UFUNCTION(BlueprintPure, Category = "Coop|Interact")
	static bool IsPawnAbleToInteract(const APawn* Pawn);

	/** F/X 입력. 위 우선순위대로 해킹 또는 포커스 대상과 상호작용한다(테스트용 BlueprintCallable). */
	UFUNCTION(BlueprintCallable, Category = "Coop|Interact")
	void Interact();

	/** 현재 포커스 대상(없으면 nullptr). */
	UFUNCTION(BlueprintPure, Category = "Coop|Interact")
	AActor* GetFocusTarget() const { return FocusTarget.Get(); }

	/** HUD 안내: 포커스 대상의 상호작용 지점/문구. 포커스가 없으면 false. */
	bool GetFocusPrompt(FVector& OutLocation, FString& OutPrompt) const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 이 거리(cm) 밖의 대상은 포커스 후보에서 미리 뺀다(실제 범위는 각 대상의 CanInteract가 판단). */
	UPROPERTY(EditDefaultsOnly, Category = "Coop|Interact")
	float SearchRange = 800.0f;

private:
	void BuildInputAssets();
	void TryBindInput();
	void RefreshCandidates();
	void UpdateFocus();
	void ExecuteInteract(AActor* Target);

	UFUNCTION(Server, Reliable)
	void Server_Interact(AActor* Target);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> InteractMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;

	TWeakObjectPtr<AActor> FocusTarget;
	TArray<TWeakObjectPtr<AActor>> Candidates;

	bool bInputBound = false;
	float FocusTimer = 0.0f;
	float CandidateTimer = 0.0f;
};
