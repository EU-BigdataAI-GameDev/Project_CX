// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoopInteractComponent.h"
#include "CXPlayerLifeComponent.h"
#include "SisterCharacter.h"
#include "SisterHackComponent.h"
#include "BrotherCombatComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace CoopInteract
{
	constexpr float FocusInterval = 0.2f;
	/** 런타임 스폰된 대상(테스트용 문 등)도 잡히도록 후보 목록은 1초마다 갱신. */
	constexpr float CandidateInterval = 1.0f;
	/** 해킹 IMC(10)와 같은 우선순위. */
	constexpr int32 MappingPriority = 10;
}

UCoopInteractComponent::UCoopInteractComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 클라이언트 → 서버 RPC(Server_Interact)를 쓰려면 복제 컴포넌트여야 한다
	SetIsReplicatedByDefault(true);
}

UCoopInteractComponent* UCoopInteractComponent::EnsureOn(APawn* Pawn)
{
	if (!Pawn)
	{
		return nullptr;
	}
	if (UCoopInteractComponent* Existing = Pawn->FindComponentByClass<UCoopInteractComponent>())
	{
		return Existing;
	}
	// BP_Brother처럼 C++ 부모가 없는 폰: 에셋을 고치지 않고 런타임에 붙인다
	UCoopInteractComponent* Component = NewObject<UCoopInteractComponent>(Pawn, TEXT("CoopInteract"));
	Pawn->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

ECoopCharacterRole UCoopInteractComponent::GetPawnRole(const APawn* Pawn)
{
	if (!Pawn)
	{
		return ECoopCharacterRole::None;
	}
	if (Pawn->IsA<ASisterCharacter>())
	{
		return ECoopCharacterRole::Sister;
	}
	// 오빠는 BP_Brother(부모 Character) — 전투 컴포넌트 보유 여부로 판단
	return Pawn->FindComponentByClass<UBrotherCombatComponent>() ? ECoopCharacterRole::Brother : ECoopCharacterRole::None;
}

bool UCoopInteractComponent::IsPawnAbleToInteract(const APawn* Pawn)
{
	// 사망·부활 대기 중이면 false(UCXPlayerLifeComponent — 게임 오버화면 기획 2장)
	return IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Pawn->GetController() != nullptr && !Pawn->IsHidden()
		&& UCXPlayerLifeComponent::IsPawnAlive(Pawn);
}

void UCoopInteractComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildInputAssets();
	RefreshCandidates();
}

void UCoopInteractComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bInputBound)
	{
		TryBindInput();
	}

	CandidateTimer -= DeltaTime;
	if (CandidateTimer <= 0.0f)
	{
		CandidateTimer = CoopInteract::CandidateInterval;
		RefreshCandidates();
	}

	FocusTimer -= DeltaTime;
	if (FocusTimer <= 0.0f)
	{
		FocusTimer = CoopInteract::FocusInterval;
		UpdateFocus();
	}
}

// ---------------------------------------------------------------- 입력

void UCoopInteractComponent::BuildInputAssets()
{
	InteractAction = NewObject<UInputAction>(this, TEXT("IA_Coop_Interact"));
	InteractAction->ValueType = EInputActionValueType::Boolean;

	// 키보드 F / 게임패드 X(FaceButton_Left) — 두 캐릭터 공용(IMC_Default에는 없는 키)
	InteractMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_CoopInteract"));
	InteractMappingContext->MapKey(InteractAction, EKeys::F);
	InteractMappingContext->MapKey(InteractAction, EKeys::Gamepad_FaceButton_Left);
}

void UCoopInteractComponent::TryBindInput()
{
	// 폰이 빙의되고 InputComponent가 생긴 뒤에야 바인딩할 수 있으므로 성공할 때까지 매 틱 재시도(BrotherCombatComponent와 같은 방식)
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return;
	}
	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(Pawn->InputComponent);
	if (!PC || !Input || !PC->GetLocalPlayer() || !InteractAction)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(InteractMappingContext, CoopInteract::MappingPriority);
	}
	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &UCoopInteractComponent::Interact);
	bInputBound = true;
}

// ---------------------------------------------------------------- 포커스

void UCoopInteractComponent::RefreshCandidates()
{
	Candidates.Reset();
	if (const UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Implements<UCoopInteractable>())
			{
				Candidates.Add(*It);
			}
		}
	}
}

void UCoopInteractComponent::UpdateFocus()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	AActor* Best = nullptr;
	float BestDistSq = FMath::Square(SearchRange);
	if (Pawn && IsPawnAbleToInteract(Pawn))
	{
		const FVector PawnLocation = Pawn->GetActorLocation();
		for (const TWeakObjectPtr<AActor>& Weak : Candidates)
		{
			AActor* Actor = Weak.Get();
			const ICoopInteractable* Interactable = Cast<ICoopInteractable>(Actor);
			if (!Interactable || !Interactable->CanInteract(Pawn))
			{
				continue;
			}
			const float DistSq = FVector::DistSquared(PawnLocation, Interactable->GetInteractLocation(Pawn));
			if (DistSq < BestDistSq)
			{
				Best = Actor;
				BestDistSq = DistSq;
			}
		}
	}
	FocusTarget = Best;
}

bool UCoopInteractComponent::GetFocusPrompt(FVector& OutLocation, FString& OutPrompt) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const ICoopInteractable* Interactable = Cast<ICoopInteractable>(FocusTarget.Get());
	if (!Pawn || !Interactable)
	{
		return false;
	}
	OutLocation = Interactable->GetInteractLocation(Pawn);
	OutPrompt = Interactable->GetInteractPrompt(Pawn);
	return true;
}

// ---------------------------------------------------------------- 실행

void UCoopInteractComponent::Interact()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return;
	}

	// 1)·2) 해킹 우선: 해킹 중이거나 해킹 가능한 단말기가 범위 안이면 해킹 입력으로 넘긴다
	if (USisterHackComponent* Hack = Pawn->FindComponentByClass<USisterHackComponent>())
	{
		if (Hack->WantsInteract())
		{
			Hack->Interact();
			return;
		}
	}

	// 3) 포커스 대상(눌렀을 때 기준으로 다시 계산)
	UpdateFocus();
	AActor* Target = FocusTarget.Get();
	if (!Target)
	{
		return;
	}
	if (Pawn->HasAuthority())
	{
		ExecuteInteract(Target);
	}
	else
	{
		Server_Interact(Target);
	}
}

void UCoopInteractComponent::Server_Interact_Implementation(AActor* Target)
{
	ExecuteInteract(Target);
}

void UCoopInteractComponent::ExecuteInteract(AActor* Target)
{
	// 서버에서 다시 검증(클라이언트 포커스는 최대 0.2초 늦을 수 있고, 거리/역할/상태는 서버 기준)
	APawn* Pawn = Cast<APawn>(GetOwner());
	ICoopInteractable* Interactable = Cast<ICoopInteractable>(Target);
	if (!Pawn || !Interactable || !IsPawnAbleToInteract(Pawn) || !Interactable->CanInteract(Pawn))
	{
		return;
	}
	Interactable->Interact(Pawn);
}
