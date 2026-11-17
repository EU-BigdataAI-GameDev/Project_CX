// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXSoloSubsystem.h"
#include "CXSoloFollowerComponent.h"
#include "LocalCoopGameMode.h"
#include "CoopInteractComponent.h"
#include "CXPlayerLifeComponent.h"
#include "CXJeminiInputComponent.h"
#include "CXMenuSubsystem.h"
#include "SisterHackComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CoreMisc.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXSoloMode, Log, All);

bool UCXSoloSubsystem::bActive = false;
int32 UCXSoloSubsystem::ControlledSlot = 0;
bool UCXSoloSubsystem::bPendingRestore = false;
int32 UCXSoloSubsystem::PendingSlot = 0;

UCXSoloSubsystem* UCXSoloSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UCXSoloSubsystem>() : nullptr;
}

UCXSoloSubsystem* UCXSoloSubsystem::GetForGameInstance(const UGameInstance* GameInstance)
{
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UCXSoloSubsystem>() : nullptr;
}

void UCXSoloSubsystem::NotifyTravel()
{
	bPendingRestore = bActive;
	PendingSlot = ControlledSlot;
}

bool UCXSoloSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCXSoloSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bWorldBegun = true;
	// 맵 이동 직후: 협동 맵이면 이어서 켠다. 시작화면 등 다른 맵이면 버린다.
	const bool bRestore = bPendingRestore;
	bPendingRestore = false;
	if (bRestore && InWorld.GetAuthGameMode<ALocalCoopGameMode>())
	{
		Enable(PendingSlot);
	}
}

void UCXSoloSubsystem::Deinitialize()
{
	// 월드가 사라지면 상태도 끝(맵 이동은 NotifyTravel이 따로 기억한다). 설정은 건드린 적이 없어 복구할 것이 없다.
	bActive = false;
	bWorldBegun = false;
	Super::Deinitialize();
}

TStatId UCXSoloSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCXSoloSubsystem, STATGROUP_Tickables);
}

// ---------------------------------------------------------------- 켜기/끄기

bool UCXSoloSubsystem::Enable(int32 Slot)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	const int32 NewSlot = Slot == 1 ? 1 : 0;
	if (!World->GetAuthGameMode<ALocalCoopGameMode>())
	{
		// 시작화면 등 협동 맵이 아닌 곳: 미리 켜 둔다 — 장치 준비 검사를 통과시키고, 협동 맵이 열리면 이어서 적용(OnWorldBeginPlay)
		bActive = true;
		ControlledSlot = NewSlot;
		bPendingRestore = true;
		PendingSlot = NewSlot;
		UE_LOG(LogCXSoloMode, Log, TEXT("CXSolo: armed before game start (controlling %s)"), NewSlot == 1 ? TEXT("SISTER") : TEXT("BROTHER"));
		return true;
	}
	const bool bWasActive = bActive;
	bActive = true;
	ApplyControl(NewSlot);
	UE_LOG(LogCXSoloMode, Log, TEXT("CXSolo: %s (controlling %s)"), bWasActive ? TEXT("changed") : TEXT("ON"), NewSlot == 1 ? TEXT("SISTER") : TEXT("BROTHER"));
	return true;
}

void UCXSoloSubsystem::Disable()
{
	bPendingRestore = false;
	if (!bActive)
	{
		return;
	}
	// 컴포넌트를 없애기 전에 자동 동작(해킹 세션·조준)을 정리
	RemoveFollowers();
	bActive = false;
	FlushAllPressedKeys();
	UE_LOG(LogCXSoloMode, Log, TEXT("CXSolo: OFF (normal 2P)"));
}

void UCXSoloSubsystem::RemoveFollowers()
{
	APawn* Brother = nullptr;
	APawn* Sister = nullptr;
	FindRolePawns(Brother, Sister);
	for (APawn* Pawn : { Brother, Sister })
	{
		if (!Pawn)
		{
			continue;
		}
		if (UCXSoloFollowerComponent* Follower = Pawn->FindComponentByClass<UCXSoloFollowerComponent>())
		{
			Follower->ResetAutomation();
			Follower->DestroyComponent();
		}
	}
}

void UCXSoloSubsystem::ApplyControl(int32 NewSlot)
{
	ControlledSlot = NewSlot;
	// 새로 AI가 되는 쪽 / 새로 조작하게 되는 쪽의 이전 상태를 비운다
	APawn* Brother = nullptr;
	APawn* Sister = nullptr;
	FindRolePawns(Brother, Sister);
	APawn* Leader = NewSlot == 1 ? Sister : Brother;
	APawn* Follower = NewSlot == 1 ? Brother : Sister;
	if (UCXSoloFollowerComponent* Comp = UCXSoloFollowerComponent::EnsureOn(Leader))
	{
		Comp->Configure(false, nullptr);
	}
	if (UCXSoloFollowerComponent* Comp = UCXSoloFollowerComponent::EnsureOn(Follower))
	{
		Comp->Configure(true, Leader);
	}
	// 조작 대상이 바뀌면 눌려 있던 키가 엉뚱한 쪽에 끼지 않게 모두 뗀다
	FlushAllPressedKeys();
	if (ACharacter* LeaderChar = Cast<ACharacter>(Leader))
	{
		LeaderChar->StopJumping();
	}
}

void UCXSoloSubsystem::FlushAllPressedKeys() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PC->FlushPressedKeys();
		}
	}
}

void UCXSoloSubsystem::FindRolePawns(APawn*& OutBrother, APawn*& OutSister) const
{
	OutBrother = nullptr;
	OutSister = nullptr;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		switch (UCoopInteractComponent::GetPawnRole(Pawn))
		{
		case ECoopCharacterRole::Brother: OutBrother = Pawn; break;
		case ECoopCharacterRole::Sister: OutSister = Pawn; break;
		default: break;
		}
	}
}

// ---------------------------------------------------------------- 전환

bool UCXSoloSubsystem::IsSwitchBlocked(FString& OutReason) const
{
	const UWorld* World = GetWorld();
	const ALocalCoopGameMode* GameMode = World ? World->GetAuthGameMode<ALocalCoopGameMode>() : nullptr;
	if (!GameMode || GameMode->IsGameOver() || GameMode->IsGameCleared() || GameMode->IsTravelling())
	{
		OutReason = TEXT("game flow");
		return true;
	}
	if (const UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(World); (Menus && Menus->HasActiveMenu()) || World->IsPaused())
	{
		OutReason = TEXT("menu/pause");
		return true;
	}
	if (UCXJeminiInputComponent::IsAnyChatOpen())
	{
		OutReason = TEXT("chat open");
		return true;
	}
	APawn* Brother = nullptr;
	APawn* Sister = nullptr;
	FindRolePawns(Brother, Sister);
	APawn* Leader = ControlledSlot == 1 ? Sister : Brother;
	APawn* Other = ControlledSlot == 1 ? Brother : Sister;
	if (!Leader || !Other)
	{
		OutReason = TEXT("pawn missing");
		return true;
	}
	if (!UCXPlayerLifeComponent::IsPawnAlive(Other))
	{
		OutReason = TEXT("partner down");
		return true;
	}
	// 조작 중인 여동생이 해킹 중이면 막는다(AI가 연 세션은 전환할 때 닫힌다)
	if (const USisterHackComponent* Hack = Leader->FindComponentByClass<USisterHackComponent>(); Hack && Hack->IsHacking())
	{
		OutReason = TEXT("hack session");
		return true;
	}
	// 마이크 녹음 중이면 막는다(말하는 도중에 조작 대상이 바뀌지 않게)
	if (const UCXJeminiInputComponent* Input = Leader->FindComponentByClass<UCXJeminiInputComponent>(); Input && Input->IsRecording())
	{
		OutReason = TEXT("recording");
		return true;
	}
	return false;
}

bool UCXSoloSubsystem::TrySwitchControl(bool bForce)
{
	if (!bActive)
	{
		return false;
	}
	FString Reason;
	if (!bForce && IsSwitchBlocked(Reason))
	{
		UE_LOG(LogCXSoloMode, Log, TEXT("CXSolo: switch blocked (%s)"), *Reason);
		return false;
	}
	ApplyControl(1 - ControlledSlot);
	UE_LOG(LogCXSoloMode, Log, TEXT("CXSolo: now controlling %s"), ControlledSlot == 1 ? TEXT("SISTER") : TEXT("BROTHER"));
	return true;
}

// ---------------------------------------------------------------- 틱

void UCXSoloSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!bActive || !bWorldBegun || !World || World->GetNetMode() == NM_Client || !World->GetAuthGameMode<ALocalCoopGameMode>())
	{
		return;
	}

	APawn* Brother = nullptr;
	APawn* Sister = nullptr;
	FindRolePawns(Brother, Sister);
	APawn* Leader = ControlledSlot == 1 ? Sister : Brother;
	APawn* Other = ControlledSlot == 1 ? Brother : Sister;

	// 조작 캐릭터가 쓰러졌고 다른 쪽이 살아 있으면 생존자로 자동 전환(부활하면 그대로 유지)
	if (Leader && Other && !UCXPlayerLifeComponent::IsPawnAlive(Leader) && UCXPlayerLifeComponent::IsPawnAlive(Other))
	{
		const ALocalCoopGameMode* GameMode = World->GetAuthGameMode<ALocalCoopGameMode>();
		if (!GameMode->IsGameOver() && !GameMode->IsGameCleared())
		{
			TrySwitchControl(true);
			return;
		}
	}

	// 부활·맵 이동으로 폰이 새로 생기면 AI 컴포넌트를 다시 붙인다(Configure는 값이 같으면 가볍다)
	if (Leader)
	{
		UCXSoloFollowerComponent::EnsureOn(Leader)->Configure(false, nullptr);
	}
	if (Other)
	{
		UCXSoloFollowerComponent::EnsureOn(Other)->Configure(true, Leader);
	}
}

FString UCXSoloSubsystem::GetDebugState() const
{
	return FString::Printf(TEXT("solo=%d controlling=%s"), bActive ? 1 : 0, ControlledSlot == 1 ? TEXT("SISTER") : TEXT("BROTHER"));
}

// 협동 게임 모드가 없는 맵(시작화면)에서도 CXSolo를 받는다 — 게임 모드 Exec가 먼저 처리하면 여기까지 오지 않는다.
namespace
{
	struct FCXSoloGlobalExec : public FSelfRegisteringExec
	{
		virtual bool Exec_Runtime(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override
		{
			if (!FParse::Command(&Cmd, TEXT("CXSolo")))
			{
				return false;
			}
			UCXSoloSubsystem* Solo = InWorld ? InWorld->GetSubsystem<UCXSoloSubsystem>() : nullptr;
			if (!Solo)
			{
				return false;
			}
			const FString Arg = FString(Cmd).TrimStartAndEnd().ToLower();
			if (Arg == TEXT("off") || Arg == TEXT("false") || Arg == TEXT("-1"))
			{
				Solo->Disable();
			}
			else
			{
				Solo->Enable((Arg == TEXT("1") || Arg == TEXT("sister") || Arg == TEXT("s")) ? 1 : 0);
			}
			Ar.Logf(TEXT("CXSolo: %s"), *Solo->GetDebugState());
			return true;
		}
	};
	FCXSoloGlobalExec GCXSoloGlobalExec;
}
