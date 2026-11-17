// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXTutorialSubsystem.h"
#include "LocalCoopGameMode.h"
#include "CoopInteractComponent.h"
#include "CXPlayerLifeComponent.h"
#include "CXGameInstance.h"
#include "CXGameUserSettings.h"
#include "CXMenuSubsystem.h"
#include "CXMenuStyle.h"
#include "BrotherCombatComponent.h"
#include "SisterHackComponent.h"
#include "HackTerminal.h"
#include "SpiderBot.h"
#include "CXJeminiDrone.h"
#include "CXJeminiInputComponent.h"
#include "CXWardenTerminal.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXTutorial, Log, All);

namespace CXTutorialTuning
{
	/** 1단계(이동): 시작 위치에서 이만큼(cm) 움직이면 "이동함". */
	constexpr float MoveDistance = 120.0f;
	/** 2단계(첫 거미): 쫓아오는 거미가 오빠에게서 이 거리(cm) 안이면 발동 / 감지 전이라도 이 거리 안이면 발동. */
	constexpr float SpiderAggroRange = 1600.0f;
	constexpr float SpiderNearRange = 1000.0f;
	/** 2단계: 여동생 한 줄 안내가 떠 있는 시간(실시간 초) — 적 해킹을 시작하면 바로 사라진다. */
	constexpr float SisterSpiderHintTime = 9.0f;
	/** "조용한 때" 판정: 쫓아오는 거미가 플레이어에게서 이 거리(cm) 안에 없어야 한다. */
	constexpr float CalmRange = 2500.0f;
	/** 5단계(규칙): 제미니 대답이 온 뒤 읽을 시간 / 대답이 안 오면(서버 꺼짐) 이만큼 기다렸다가 띄운다(실시간 초). */
	constexpr float RuleAfterAnswerDelay = 3.0f;
	constexpr float RuleNoAnswerDelay = 9.0f;
	/** 예시 문장을 채팅창에 넣기 전 지연(실시간 초) — 숫자 키의 문자 입력이 입력창에 찍히지 않게. */
	constexpr float ChatPrefillDelay = 0.12f;
	constexpr float InfoDim = 0.6f;
	constexpr float ActionDim = 0.4f;
	constexpr float WaitingDim = 0.35f;
}

UCXTutorialSubsystem* UCXTutorialSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UCXTutorialSubsystem>() : nullptr;
}

UCXTutorialSubsystem* UCXTutorialSubsystem::GetForGameInstance(const UGameInstance* GameInstance)
{
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UCXTutorialSubsystem>() : nullptr;
}

bool UCXTutorialSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCXTutorialSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bWorldBegun = true;
	LastRealTime = FPlatformTime::Seconds();
	// 자동화 테스트용: -CXNoTutorial 이면 이 실행에서는 튜토리얼을 띄우지 않는다("봤음" 기록·설정은 건드리지 않음)
	bSuppressed = FParse::Param(FCommandLine::Get(), TEXT("CXNoTutorial"));
}

void UCXTutorialSubsystem::Deinitialize()
{
	// 일시정지·타임 딜레이션은 이 월드(WorldSettings)의 것이라 월드와 함께 사라진다 — 표시만 지운다
	bOwnsDilation = false;
	bOwnsPause = false;
	ActiveIndex = INDEX_NONE;
	bWorldBegun = false;
	Super::Deinitialize();
}

TStatId UCXTutorialSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCXTutorialSubsystem, STATGROUP_Tickables);
}

// ---------------------------------------------------------------- 조회 도우미

APawn* UCXTutorialSubsystem::FindRolePawn(int32 Role) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	const ECoopCharacterRole Wanted = Role == 1 ? ECoopCharacterRole::Sister : ECoopCharacterRole::Brother;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Pawn && UCoopInteractComponent::GetPawnRole(Pawn) == Wanted)
		{
			return Pawn;
		}
	}
	return nullptr;
}

bool UCXTutorialSubsystem::IsRoleGamepad(int32 Role) const
{
	// HUD의 [F]/[X] 표기와 같은 기준: 슬롯(0 오빠 / 1 여동생)의 조작 장치 설정
	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	return GI ? GI->GetSlotDeviceType(Role) == ECXInputDeviceType::Gamepad : Role == 1;
}

bool UCXTutorialSubsystem::CanRoleConfirm(int32 Role) const
{
	const APawn* Pawn = FindRolePawn(Role);
	if (!Pawn || !UCXPlayerLifeComponent::IsPawnAlive(Pawn))
	{
		return false;
	}
	if (!IsRoleGamepad(Role))
	{
		return true;
	}
	// 패드가 하나도 없으면(키보드만으로 테스트) 그 플레이어의 확인은 기다리지 않는다
	return FSlateApplication::IsInitialized() && FSlateApplication::Get().IsGamepadAttached();
}

bool UCXTutorialSubsystem::CanPauseNow() const
{
	const UWorld* World = GetWorld();
	if (!World || World->IsPaused() || UCXJeminiInputComponent::IsAnyChatOpen())
	{
		// 채팅 입력창은 전송을 다음 틱 타이머로 처리한다 — 열려 있는 동안 월드를 멈추면 전송이 밀린다
		return false;
	}
	for (int32 Role = 0; Role < NumRoles; ++Role)
	{
		const APawn* Pawn = FindRolePawn(Role);
		const UCXJeminiInputComponent* Input = Pawn ? Pawn->FindComponentByClass<UCXJeminiInputComponent>() : nullptr;
		if (Input && Input->IsRecording())
		{
			return false;
		}
	}
	return true;
}

bool UCXTutorialSubsystem::IsCalm() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<ASpiderBot> It(World); It; ++It)
	{
		const ESpiderBotState State = It->GetState();
		if (State != ESpiderBotState::Chase && State != ESpiderBotState::Attack)
		{
			continue;
		}
		for (int32 Role = 0; Role < NumRoles; ++Role)
		{
			const APawn* Pawn = FindRolePawn(Role);
			if (Pawn && FVector::Dist(Pawn->GetActorLocation(), It->GetActorLocation()) <= CXTutorialTuning::CalmRange)
			{
				return false;
			}
		}
	}
	return true;
}

bool UCXTutorialSubsystem::IsSeen(int32 StepIndex) const
{
	const TArray<FCXTutorialStepDef>& Steps = CXTutorial::GetSteps();
	return Steps.IsValidIndex(StepIndex) && UCXGameUserSettings::GetCXGameUserSettings()->IsTutorialSeen(Steps[StepIndex].Id);
}

// ---------------------------------------------------------------- 틱

void UCXTutorialSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 일시정지·슬로모션과 무관한 실시간 기준
	const double Now = FPlatformTime::Seconds();
	const float RealDelta = static_cast<float>(FMath::Clamp(Now - LastRealTime, 0.0, 0.1));
	LastRealTime = Now;

	UWorld* World = GetWorld();
	if (!bWorldBegun || !World || World->GetNetMode() == NM_Client)
	{
		// 네트워크 클라이언트: 아무것도 하지 않는다(로컬 협동 전용)
		return;
	}
	const ALocalCoopGameMode* GameMode = World->GetAuthGameMode<ALocalCoopGameMode>();
	if (!GameMode)
	{
		return;	// 시작화면 등
	}
	const bool bFlowBlocked = GameMode->IsGameOver() || GameMode->IsGameCleared() || GameMode->IsTravelling();
	const bool bEnabled = !bSuppressed && UCXGameUserSettings::GetCXGameUserSettings()->IsTutorialEnabled();
	const UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(World);
	const bool bMenu = Menus && Menus->HasActiveMenu();

	if (ActiveIndex != INDEX_NONE)
	{
		if (bFlowBlocked || (!bEnabled && !bForced))
		{
			// 게임 오버·클리어·맵 이동이 시작됐거나 꺼졌다 → 조용히 닫는다(못 본 것으로 — 다음에 다시)
			FinishStep(false);
			return;
		}
		if (bMenu)
		{
			return;	// 일시정지 메뉴(패드 연결 끊김 자동 열기 포함)가 떠 있는 동안 시계 정지
		}
		TickActive(RealDelta);
		return;
	}

	if (bMenu || World->IsPaused())
	{
		return;
	}

	// 예시 문장 → 키보드 플레이어 채팅창(카드가 닫힌 뒤 한 박자 늦게)
	if (!PendingChatText.IsEmpty())
	{
		ChatPrefillTimer -= RealDelta;
		if (ChatPrefillTimer <= 0.0f)
		{
			const FString Text = MoveTemp(PendingChatText);
			PendingChatText.Reset();
			for (int32 Role = 0; Role < NumRoles && !bFlowBlocked; ++Role)
			{
				APawn* Pawn = IsRoleGamepad(Role) ? nullptr : FindRolePawn(Role);
				if (UCXJeminiInputComponent* Input = Pawn ? Pawn->FindComponentByClass<UCXJeminiInputComponent>() : nullptr)
				{
					const bool bOpened = Input->OpenChatWithText(Text);
					UE_LOG(LogCXTutorial, Log, TEXT("Tutorial: example -> chat box (%s)"), bOpened ? TEXT("opened") : TEXT("blocked"));
					break;
				}
			}
		}
	}

	if (bFlowBlocked || !bEnabled)
	{
		return;
	}

	IdleClock += RealDelta;

	// 인사(4단계) 뒤 제미니 대답이 온 시점 기록 → 대답을 읽을 시간을 준 다음 규칙 안내(5단계)
	if (bHelloDoneThisMap && HelloAnswerIdleClock < 0.0f)
	{
		const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(World);
		if (Drone && Drone->GetDialogueSeq() != HelloBaseDialogueSeq && !Drone->IsRequestPending())
		{
			HelloAnswerIdleClock = IdleClock;
		}
	}

	PollTimer -= RealDelta;
	if (PollTimer <= 0.0f)
	{
		PollTimer = CXTutorial::PollInterval;
		TryStartNext();
	}
}

void UCXTutorialSubsystem::TryStartNext()
{
	const TArray<FCXTutorialStepDef>& Steps = CXTutorial::GetSteps();
	for (int32 i = 0; i < Steps.Num(); ++i)
	{
		if (IsSeen(i) || IdleClock < Steps[i].StartDelay)
		{
			continue;
		}
		AActor* Spot = nullptr;
		if (ShouldTrigger(Steps[i], Spot) && BeginStep(i, Spot, false))
		{
			return;
		}
	}
}

bool UCXTutorialSubsystem::ShouldTrigger(const FCXTutorialStepDef& Def, AActor*& OutSpotActor) const
{
	const UWorld* World = GetWorld();
	const APawn* Brother = FindRolePawn(0);
	const APawn* Sister = FindRolePawn(1);
	const bool bBrotherAlive = Brother && UCXPlayerLifeComponent::IsPawnAlive(Brother);
	const bool bSisterAlive = Sister && UCXPlayerLifeComponent::IsPawnAlive(Sister);
	const USisterHackComponent* Hack = Sister ? Sister->FindComponentByClass<USisterHackComponent>() : nullptr;
	const bool bMap1 = UGameplayStatics::GetCurrentLevelName(World, true).StartsWith(TEXT("Map1"));
	const bool bNearWarden = ACXWardenTerminal::FindTalkTarget(Brother) != nullptr || ACXWardenTerminal::FindTalkTarget(Sister) != nullptr;

	switch (Def.Step)
	{
	case ECXTutorialStep::Move:
		// Map1 시작 직후(스폰 뒤 StartDelay초). 다른 맵에서 이어 시작하면 띄우지 않는다.
		return bMap1 && (bBrotherAlive || bSisterAlive);

	case ECXTutorialStep::FirstSpider:
	{
		if (!bBrotherAlive)
		{
			return false;
		}
		// 플레이어를 감지해 쫓아오는 거미가 화면에 들어올 거리 / 감지 전이라도 충분히 가까운 거미
		for (TActorIterator<ASpiderBot> It(World); It; ++It)
		{
			if (It->IsDead())
			{
				continue;
			}
			const float Dist = FVector::Dist(Brother->GetActorLocation(), It->GetActorLocation());
			const bool bAggro = It->GetState() == ESpiderBotState::Chase || It->GetState() == ESpiderBotState::Attack;
			if ((bAggro && Dist <= CXTutorialTuning::SpiderAggroRange) || Dist <= CXTutorialTuning::SpiderNearRange)
			{
				OutSpotActor = *It;
				return true;
			}
		}
		return false;
	}

	case ECXTutorialStep::FirstTerminal:
		// 여동생이 해킹 가능한 단말기의 상호작용 범위에 처음 들어옴(USisterHackComponent 포커스 — HUD TERMINAL ONLINE과 같은 기준).
		// 이미 해킹을 시작했어도 띄운다(세션은 월드와 함께 멈춘다 — ESC 메뉴와 같음).
		if (bSisterAlive && Hack && Hack->GetFocusTerminal() && CanPauseNow())
		{
			OutSpotActor = Hack->GetFocusTerminal();
			return true;
		}
		return false;

	case ECXTutorialStep::JeminiHello:
	{
		// 1단계를 본 뒤(Map1이 아니면 1단계가 없으니 바로), 조용할 때
		const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(World);
		const int32 MoveIndex = static_cast<int32>(ECXTutorialStep::Move);
		return (IsSeen(MoveIndex) || !bMap1) && Drone && !Drone->IsBusy() && (bBrotherAlive || bSisterAlive) && !bNearWarden
			&& !(Hack && Hack->IsHacking()) && CanPauseNow() && IsCalm();
	}

	case ECXTutorialStep::JeminiRule:
	{
		const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(World);
		if (!IsSeen(static_cast<int32>(ECXTutorialStep::JeminiHello)) || !Drone || Drone->IsBusy() || !(bBrotherAlive || bSisterAlive) || bNearWarden
			|| (Hack && Hack->IsHacking()) || !CanPauseNow() || !IsCalm())
		{
			return false;
		}
		if (bHelloDoneThisMap)
		{
			// 이번 맵에서 인사를 했다: 대답이 왔으면 읽을 시간 뒤, 안 오면(서버 꺼짐) 조금 더 기다렸다가
			return HelloAnswerIdleClock >= 0.0f ? IdleClock >= HelloAnswerIdleClock + CXTutorialTuning::RuleAfterAnswerDelay
				: IdleClock >= CXTutorialTuning::RuleNoAnswerDelay;
		}
		return true;
	}

	case ECXTutorialStep::Warden:
		// 누군가 처음 WARDEN 대화 거리(ACXWardenTerminal::TalkRange 5.5m)에 들어옴 — 채팅·음성이 WARDEN으로 가기 시작하는 지점
		if (bNearWarden && CanPauseNow())
		{
			OutSpotActor = ACXWardenTerminal::FindFirst(World);
			return true;
		}
		return false;

	default:
		return false;
	}
}

bool UCXTutorialSubsystem::BeginStep(int32 StepIndex, AActor* InSpotActor, bool bInForced)
{
	UWorld* World = GetWorld();
	const TArray<FCXTutorialStepDef>& Steps = CXTutorial::GetSteps();
	if (!World || !Steps.IsValidIndex(StepIndex) || ActiveIndex != INDEX_NONE)
	{
		return false;
	}
	const FCXTutorialStepDef& Def = Steps[StepIndex];

	bool bActive[NumRoles] = { false, false };
	bool bAny = false, bInfo = false, bAction = false;
	for (int32 Role = 0; Role < NumRoles; ++Role)
	{
		const FCXTutorialCardDef& Card = GetCard(Def, Role);
		const APawn* Pawn = FindRolePawn(Role);
		// 쓰러진(부활 대기) 플레이어 칸에는 띄우지 않는다(SIGNAL LOST 화면)
		bActive[Role] = Card.Mode != ECXTutorialMode::None && Pawn && UCXPlayerLifeComponent::IsPawnAlive(Pawn);
		bAny |= bActive[Role];
		bInfo |= bActive[Role] && Card.Mode == ECXTutorialMode::Info;
		bAction |= bActive[Role] && Card.Mode == ECXTutorialMode::Action;
	}
	if (!bAny || (bInfo && World->IsPaused()))
	{
		return false;
	}

	ActiveIndex = StepIndex;
	bForced = bInForced;
	ActiveClock = 0.0f;
	SpotActor = InSpotActor;
	PendingChatText.Reset();
	bAnyoneCanConfirm = false;
	bool bAnyRequired = false;
	for (int32 Role = 0; Role < NumRoles; ++Role)
	{
		bCardActive[Role] = bActive[Role];
		bCardDone[Role] = !bActive[Role];
		bRequired[Role] = bActive[Role] && GetCard(Def, Role).Mode == ECXTutorialMode::Info && CanRoleConfirm(Role);
		bAnyRequired |= bRequired[Role];
		const APawn* Pawn = FindRolePawn(Role);
		StartLocation[Role] = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
		bMoved[Role] = bJumped[Role] = false;
	}
	// 확인할 수 있는 대상이 없으면(패드 미연결 등) 누가 눌러도 닫힌다
	bAnyoneCanConfirm = bInfo && !bAnyRequired;

	const APawn* Brother = FindRolePawn(0);
	const UBrotherCombatComponent* Combat = Brother ? Brother->FindComponentByClass<UBrotherCombatComponent>() : nullptr;
	BaseShotTime = Combat ? Combat->GetLastShotWorldTime() : 0.0;
	const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(World);
	BaseDialogueSeq = Drone ? Drone->GetDialogueSeq() : 0;

	if (bInfo)
	{
		UGameplayStatics::SetGamePaused(World, true);
		bOwnsPause = true;
	}
	else if (bAction)
	{
		SavedDilation = UGameplayStatics::GetGlobalTimeDilation(World);
		if (FMath::IsNearlyEqual(SavedDilation, CXTutorial::SlowDilation, 0.001f))
		{
			SavedDilation = 1.0f;
		}
		UGameplayStatics::SetGlobalTimeDilation(World, CXTutorial::SlowDilation);
		bOwnsDilation = true;
	}
	UE_LOG(LogCXTutorial, Log, TEXT("Tutorial: begin %s (forced=%d brother=%d sister=%d pause=%d slow=%d)"), Def.Id, bForced ? 1 : 0,
		bCardActive[0] ? 1 : 0, bCardActive[1] ? 1 : 0, bOwnsPause ? 1 : 0, bOwnsDilation ? 1 : 0);
	return true;
}

void UCXTutorialSubsystem::TickActive(float RealDelta)
{
	UWorld* World = GetWorld();
	const FCXTutorialStepDef& Def = CXTutorial::GetSteps()[ActiveIndex];

	if (bOwnsPause)
	{
		// Info: 월드는 멈춰 있고 이 시계만 흐른다
		ActiveClock += RealDelta;
		if (!World->IsPaused())
		{
			// 그 사이 열렸던 일시정지 메뉴가 닫히며 재개됐다 → 카드가 아직 떠 있으니 다시 멈춘다
			UGameplayStatics::SetGamePaused(World, true);
		}
		bool bAllConfirmed = !bAnyoneCanConfirm;
		for (int32 Role = 0; Role < NumRoles; ++Role)
		{
			bAllConfirmed &= !bRequired[Role] || bCardDone[Role];
		}
		if (bAllConfirmed || ActiveClock >= Def.BlockTimeout)
		{
			FinishStep(true);
		}
		return;
	}

	if (World->IsPaused())
	{
		return;	// 다른 시스템이 멈춤 — 같이 멈춘다
	}
	ActiveClock += RealDelta;

	bool bAllDone = true;
	bool bActionPending = false;
	for (int32 Role = 0; Role < NumRoles; ++Role)
	{
		if (bCardActive[Role] && !bCardDone[Role])
		{
			bCardDone[Role] = IsCardComplete(Def, Role);
		}
		bAllDone &= bCardDone[Role];
		bActionPending |= bCardActive[Role] && !bCardDone[Role] && GetCard(Def, Role).Mode == ECXTutorialMode::Action;
	}
	if (bOwnsDilation && (!bActionPending || ActiveClock >= Def.BlockTimeout))
	{
		// 행동을 했거나 시간 초과 → 슬로모션만 푼다(카드는 Timeout까지 안내로 남는다)
		ReleaseDilation();
	}
	if (bAllDone || ActiveClock >= Def.Timeout)
	{
		FinishStep(true);
	}
}

bool UCXTutorialSubsystem::IsCardComplete(const FCXTutorialStepDef& Def, int32 Role)
{
	const APawn* Pawn = FindRolePawn(Role);
	if (!Pawn || !UCXPlayerLifeComponent::IsPawnAlive(Pawn))
	{
		return true;
	}
	switch (Def.Step)
	{
	case ECXTutorialStep::Move:
	{
		// 실제로 움직이고 + 점프(공중)했을 때
		bMoved[Role] |= FVector::Dist2D(Pawn->GetActorLocation(), StartLocation[Role]) > CXTutorialTuning::MoveDistance;
		const ACharacter* Character = Cast<ACharacter>(Pawn);
		bJumped[Role] |= Character && Character->GetCharacterMovement() && Character->GetCharacterMovement()->IsFalling();
		return bMoved[Role] && bJumped[Role];
	}

	case ECXTutorialStep::FirstSpider:
		if (Role == 0)
		{
			// 오빠가 실제로 쐈다(UBrotherCombatComponent 마지막 발사 시각이 바뀜) / 그 거미가 이미 죽었다
			const UBrotherCombatComponent* Combat = Pawn->FindComponentByClass<UBrotherCombatComponent>();
			if (Combat && Combat->GetLastShotWorldTime() > BaseShotTime)
			{
				return true;
			}
			const ASpiderBot* Spider = Cast<ASpiderBot>(SpotActor.Get());
			return !bForced && (!Spider || Spider->IsDead());
		}
		else
		{
			const USisterHackComponent* Hack = Pawn->FindComponentByClass<USisterHackComponent>();
			return (Hack && Hack->IsEnemyHacking()) || ActiveClock >= CXTutorialTuning::SisterSpiderHintTime;
		}

	case ECXTutorialStep::JeminiHello:
	{
		// 누군가 제미니에게 채팅/음성을 보냈다(요청 중) 또는 대사가 바뀌었다 — LLM 응답은 기다리지 않는다
		const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(GetWorld());
		return Drone && (Drone->IsRequestPending() || Drone->GetDialogueSeq() != BaseDialogueSeq);
	}

	default:
		return false;	// Info 카드는 확인 입력(HandleKeyDown)으로만 끝난다
	}
}

void UCXTutorialSubsystem::FinishStep(bool bMarkSeen)
{
	if (ActiveIndex == INDEX_NONE)
	{
		return;
	}
	UWorld* World = GetWorld();
	const FCXTutorialStepDef& Def = CXTutorial::GetSteps()[ActiveIndex];
	const bool bWasPaused = bOwnsPause;
	ReleaseDilation();
	ReleasePause();

	if (bMarkSeen)
	{
		UCXGameUserSettings::GetCXGameUserSettings()->MarkTutorialSeen(Def.Id);
		if (Def.Step == ECXTutorialStep::JeminiHello)
		{
			// 단계 시작 때의 대사 번호를 기준으로 둔다 — 대답이 오면(이미 왔어도) 번호가 달라져 Tick이 그 시점을 기록한다
			bHelloDoneThisMap = true;
			HelloBaseDialogueSeq = BaseDialogueSeq;
			HelloAnswerIdleClock = -1.0f;
		}
	}
	else
	{
		PendingChatText.Reset();
	}
	UE_LOG(LogCXTutorial, Log, TEXT("Tutorial: end %s (%s, %.1fs)"), Def.Id, bMarkSeen ? TEXT("seen") : TEXT("aborted"), ActiveClock);

	ActiveIndex = INDEX_NONE;
	bForced = false;
	IdleClock = 0.0f;
	PollTimer = CXTutorial::PollInterval;
	SpotActor.Reset();
	ChatPrefillTimer = CXTutorialTuning::ChatPrefillDelay;
	for (int32 Role = 0; Role < NumRoles; ++Role)
	{
		bCardActive[Role] = bCardDone[Role] = bRequired[Role] = false;
	}

	if (bWasPaused && World)
	{
		// 확인 키가 눌린 채로 게임 입력에 남지 않게(ESC 메뉴 재개와 같은 처리)
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				PC->FlushPressedKeys();
			}
		}
		if (UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(World))
		{
			Menus->BlockGameplayInput(CXMenuStyle::InputLockTime);
		}
	}
}

void UCXTutorialSubsystem::ReleasePause()
{
	if (!bOwnsPause)
	{
		return;
	}
	bOwnsPause = false;
	UWorld* World = GetWorld();
	const UCXMenuSubsystem* Menus = World ? UCXMenuSubsystem::Get(World) : nullptr;
	// 일시정지 메뉴가 떠 있으면 그 메뉴가 재개를 맡는다
	if (World && !(Menus && Menus->HasActiveMenu()))
	{
		UGameplayStatics::SetGamePaused(World, false);
	}
}

void UCXTutorialSubsystem::ReleaseDilation()
{
	if (!bOwnsDilation)
	{
		return;
	}
	bOwnsDilation = false;
	UWorld* World = GetWorld();
	// 내가 건 값 그대로일 때만 되돌린다(게임 오버 슬로모션 등 다른 시스템이 바꿨으면 그쪽이 관리)
	if (World && World->GetWorldSettings(false, false)
		&& FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(World), CXTutorial::SlowDilation, 0.001f))
	{
		UGameplayStatics::SetGlobalTimeDilation(World, SavedDilation);
	}
}

// ---------------------------------------------------------------- 입력(FCXMenuInputProcessor)

bool UCXTutorialSubsystem::HandleKeyDown(const FKey& Key, bool bFromGamepad, FInputDeviceId DeviceId, bool bRepeat)
{
	if (ActiveIndex == INDEX_NONE || !IsBlocking())
	{
		return false;	// Hint 카드는 입력을 가로채지 않는다
	}
	const FCXTutorialStepDef& Def = CXTutorial::GetSteps()[ActiveIndex];

	// 건너뛰기: 누가 눌러도 단계 전체(봤음 처리). 일시정지 메뉴는 열리지 않는다.
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		if (!bRepeat)
		{
			UE_LOG(LogCXTutorial, Log, TEXT("Tutorial: %s skipped"), Def.Id);
			FinishStep(true);
		}
		return true;
	}
	if (!bOwnsPause)
	{
		return false;	// Action(슬로모션): 나머지 키는 게임으로 — 사격 등 실제 행동이 완료 조건
	}

	// Info: 확인 = Enter / Space / 패드 A, 예시 고르기 = 키보드 1~3(확인도 겸한다)
	int32 ChipIndex = INDEX_NONE;
	if (!bFromGamepad)
	{
		if (Key == EKeys::One || Key == EKeys::NumPadOne) { ChipIndex = 0; }
		else if (Key == EKeys::Two || Key == EKeys::NumPadTwo) { ChipIndex = 1; }
		else if (Key == EKeys::Three || Key == EKeys::NumPadThree) { ChipIndex = 2; }
		if (ChipIndex != INDEX_NONE && !Def.Chips[ChipIndex])
		{
			ChipIndex = INDEX_NONE;
		}
	}
	const bool bConfirm = Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom || ChipIndex != INDEX_NONE;
	if (!bConfirm)
	{
		return false;	// 콘솔 키 등은 그대로
	}
	if (bRepeat || ActiveClock < CXTutorial::MinReadTime)
	{
		return true;
	}

	const UCXGameInstance* GI = UCXGameInstance::Get(this);
	int32 Role = GI ? GI->ResolvePlayerForInput(bFromGamepad, DeviceId) : (bFromGamepad ? 1 : 0);
	Role = FMath::Clamp(Role, 0, NumRoles - 1);
	if (!(bCardActive[Role] && !bCardDone[Role]) && bFromGamepad && GI && GI->FindSlotByGamepad(DeviceId) == INDEX_NONE)
	{
		// 시작화면을 거치지 않아 패드가 슬롯에 배정되지 않았다 → 아직 확인하지 않은 게임패드 플레이어의 입력으로 본다
		for (int32 Other = 0; Other < NumRoles; ++Other)
		{
			if (bCardActive[Other] && !bCardDone[Other] && IsRoleGamepad(Other))
			{
				Role = Other;
				break;
			}
		}
	}

	if (bCardActive[Role] && !bCardDone[Role])
	{
		bCardDone[Role] = true;
		if (ChipIndex != INDEX_NONE)
		{
			PendingChatText = Def.Chips[ChipIndex];
		}
		UE_LOG(LogCXTutorial, Log, TEXT("Tutorial: %s confirmed by %s"), Def.Id, Role == 1 ? TEXT("sister") : TEXT("brother"));
	}
	else if (bAnyoneCanConfirm)
	{
		FinishStep(true);
	}
	// 모두 확인했는지는 다음 Tick(TickActive)이 판단해 닫는다
	return true;
}

// ---------------------------------------------------------------- HUD

bool UCXTutorialSubsystem::GetView(bool bSister, FCXTutorialView& OutView) const
{
	if (ActiveIndex == INDEX_NONE)
	{
		return false;
	}
	const FCXTutorialStepDef& Def = CXTutorial::GetSteps()[ActiveIndex];
	const int32 Role = bSister ? 1 : 0;
	const FCXTutorialCardDef& Card = GetCard(Def, Role);
	const bool bGamepad = IsRoleGamepad(Role);

	OutView = FCXTutorialView();
	OutView.StepNumber = ActiveIndex + 1;
	OutView.Age = ActiveClock;
	OutView.RealTime = static_cast<float>(FMath::Fmod(FPlatformTime::Seconds(), 3600.0));

	if (!bCardActive[Role] || (bCardDone[Role] && !bOwnsPause))
	{
		if (bOwnsPause)
		{
			// 대상이 아닌 칸: 월드가 멈춘 이유만 알려 준다
			OutView.bWaitingNotice = true;
			OutView.DimAlpha = CXTutorialTuning::WaitingDim;
			return true;
		}
		return false;
	}

	OutView.Mode = Card.Mode;
	OutView.Title = CXTutorial::FormatText(Card.Title, bGamepad);
	OutView.Line1 = CXTutorial::FormatText(Card.Line1, bGamepad);
	OutView.Line2 = CXTutorial::FormatText(Card.Line2, bGamepad);
	OutView.Spot = Card.Spot;
	OutView.SpotActor = SpotActor;

	switch (Card.Mode)
	{
	case ECXTutorialMode::Info:
		for (const TCHAR* Chip : Def.Chips)
		{
			if (Chip)
			{
				OutView.Chips.Add(Chip);
			}
		}
		OutView.bChipKeys = !bGamepad;
		OutView.DimAlpha = CXTutorialTuning::InfoDim;
		// 두 사람 대상이면 각자 확인해야 닫힌다
		OutView.Footer = bCardDone[Role] ? FString(TEXT("확인 완료  —  파트너를 기다리는 중…"))
			: CXTutorial::FormatText(TEXT("{confirm} 계속          {skip} 건너뛰기"), bGamepad);
		break;

	case ECXTutorialMode::Action:
		OutView.DimAlpha = bOwnsDilation ? CXTutorialTuning::ActionDim : 0.0f;
		OutView.Footer = bOwnsDilation ? CXTutorial::FormatText(TEXT("{skip} 건너뛰기"), bGamepad) : FString();
		break;

	default:
		break;
	}
	return true;
}

// ---------------------------------------------------------------- 콘솔 명령

void UCXTutorialSubsystem::ResetSeen()
{
	FinishStep(false);
	UCXGameUserSettings::GetCXGameUserSettings()->ResetTutorialSeen();
	bHelloDoneThisMap = false;
	HelloAnswerIdleClock = -1.0f;
	IdleClock = 0.0f;
	UE_LOG(LogCXTutorial, Log, TEXT("Tutorial: seen flags cleared"));
}

bool UCXTutorialSubsystem::ForceShow(const FString& StepId)
{
	UWorld* World = GetWorld();
	const int32 Index = CXTutorial::FindStepIndex(StepId);
	const UCXMenuSubsystem* Menus = World ? UCXMenuSubsystem::Get(World) : nullptr;
	if (Index == INDEX_NONE || !World || World->GetNetMode() == NM_Client || !World->GetAuthGameMode<ALocalCoopGameMode>() || (Menus && Menus->HasActiveMenu()))
	{
		return false;
	}
	FinishStep(false);

	// 강조 대상: 조건 검사 없이 가장 그럴듯한 액터
	AActor* Spot = nullptr;
	const FCXTutorialStepDef& Def = CXTutorial::GetSteps()[Index];
	if (Def.Step == ECXTutorialStep::FirstSpider)
	{
		const APawn* Brother = FindRolePawn(0);
		float Best = TNumericLimits<float>::Max();
		for (TActorIterator<ASpiderBot> It(World); It; ++It)
		{
			const float Dist = Brother ? FVector::Dist(Brother->GetActorLocation(), It->GetActorLocation()) : 0.0f;
			if (!It->IsDead() && Dist < Best)
			{
				Best = Dist;
				Spot = *It;
			}
		}
	}
	else if (Def.Step == ECXTutorialStep::FirstTerminal)
	{
		const APawn* Sister = FindRolePawn(1);
		const USisterHackComponent* Hack = Sister ? Sister->FindComponentByClass<USisterHackComponent>() : nullptr;
		Spot = Hack ? Hack->GetFocusTerminal() : nullptr;
	}
	return BeginStep(Index, Spot, true);
}

void UCXTutorialSubsystem::SetEnabled(bool bEnabled)
{
	UCXGameUserSettings::GetCXGameUserSettings()->SetTutorialEnabled(bEnabled);
	UE_LOG(LogCXTutorial, Log, TEXT("Tutorial: %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void UCXTutorialSubsystem::SuppressForThisMap(const TCHAR* Why)
{
	bSuppressed = true;
	FinishStep(false);
	UE_LOG(LogCXTutorial, Log, TEXT("Tutorial: suppressed for this map (%s)"), Why ? Why : TEXT(""));
}

FString UCXTutorialSubsystem::GetDebugState() const
{
	const UCXGameUserSettings* Settings = UCXGameUserSettings::GetCXGameUserSettings();
	const TArray<FCXTutorialStepDef>& Steps = CXTutorial::GetSteps();
	FString Seen;
	for (const FCXTutorialStepDef& Def : Steps)
	{
		Seen += FString::Printf(TEXT(" %s=%d"), Def.Id, Settings->IsTutorialSeen(Def.Id) ? 1 : 0);
	}
	const UWorld* World = GetWorld();
	return FString::Printf(TEXT("enabled=%d suppressed=%d active=%s clock=%.1f idle=%.1f pause=%d slow=%d dilation=%.2f cards=[%d%d] done=[%d%d] |%s"),
		Settings->IsTutorialEnabled() ? 1 : 0, bSuppressed ? 1 : 0, Steps.IsValidIndex(ActiveIndex) ? Steps[ActiveIndex].Id : TEXT("none"),
		ActiveClock, IdleClock, bOwnsPause ? 1 : 0, bOwnsDilation ? 1 : 0, World ? UGameplayStatics::GetGlobalTimeDilation(World) : 1.0f,
		bCardActive[0] ? 1 : 0, bCardActive[1] ? 1 : 0, bCardDone[0] ? 1 : 0, bCardDone[1] ? 1 : 0, *Seen);
}
