// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalCoopGameMode.h"
#include "EngineUtils.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "CoopHUD.h"
#include "CXGameUserSettings.h"
#include "CXSaveGame.h"
#include "CXMenuSubsystem.h"
#include "CXGameClearWidget.h"
#include "CXGameOverWidget.h"
#include "CXPlayerLifeComponent.h"
#include "Engine/GameViewportClient.h"
#include "CoopInteractComponent.h"
#include "CXPlayerAudioComponent.h"
#include "CXCoopExitDoor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

ALocalCoopGameMode::ALocalCoopGameMode()
{
	// 분할 화면 각 칸에 자기 정보를 그리는 협동 HUD
	HUDClass = ACoopHUD::StaticClass();

	// Map1 배경음(수노, 96bpm — 2마디 5초). 반복 20~205초(74마디), 크로스페이드 1마디(2.5초)로 박자 정렬(Docs/배경음_제작기록.md)
	FCXMusicTrack Map1Track;
	Map1Track.Music = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Project_CX/Audio/BGM/SW_BGM_Map1.SW_BGM_Map1")));
	Map1Track.LoopStart = 20.0f;
	Map1Track.LoopEnd = 205.0f;
	Map1Track.FadeIn = 1.0f;
	Map1Track.Crossfade = 2.5f;
	MapMusic.Add(TEXT("Map1_SoulCity"), Map1Track);
	// 2026-11-02 맵 이름 변경: 실제 Map1 레벨은 Map1_SoulCity1(Map1_SoulCity는 리디렉터) — 둘 다 둔다
	MapMusic.Add(TEXT("Map1_SoulCity1"), Map1Track);
}

void ALocalCoopGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 게임 오버에서 끈 분할 화면을 되돌린다(재시작 = 같은 맵 다시 열기 — 뷰포트 설정은 맵 이동 후에도 남는다)
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->SetForceDisableSplitscreen(false);
	}

	if (SecondPlayerPawnClass && !UGameplayStatics::GetPlayerController(this, 1))
	{
		// ControllerId 1 = 두 번째 입력 장치. bOffsetPlayerGamepadIds가 켜져 있으면 첫 번째 게임패드가 여기로 온다.
		UGameplayStatics::CreatePlayer(this, 1, true);
	}

	// 맵 배경음(PIE는 맵 이름에 UEDPIE_ 접두사가 붙으므로 제거하고 찾는다)
	const FString MapName = UGameplayStatics::GetCurrentLevelName(this, true);
	if (const FCXMusicTrack* Track = MapMusic.Find(MapName))
	{
		if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
		{
			Audio->PlayTrack(this, *Track);
		}
	}
}

UClass* ALocalCoopGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (SecondPlayerPawnClass && IsSisterController(InController))
	{
		return SecondPlayerPawnClass;
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

AActor* ALocalCoopGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (!SecondPlayerStartTag.IsNone())
	{
		// 태그로 1P/2P 시작 지점을 나눈다. 기본 구현은 빈 PlayerStart 중 아무거나 고르므로 1P가 2P 자리에 설 수 있다.
		const bool bWantSecondStart = IsSisterController(Player);
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if ((It->PlayerStartTag == SecondPlayerStartTag) == bWantSecondStart)
			{
				return *It;
			}
		}
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

bool ALocalCoopGameMode::IsSisterController(const AController* Controller)
{
	if (!Cast<APlayerController>(Controller))
	{
		return false;
	}
	const bool bSecond = IsSecondLocalPlayer(Controller);
	// 오빠 게임패드 / 여동생 키보드: 키보드 = LocalPlayer 0 → 여동생
	return UCXGameUserSettings::GetCXGameUserSettings()->IsCharacterSwapLayout() ? !bSecond : bSecond;
}

bool ALocalCoopGameMode::IsSecondLocalPlayer(const AController* Controller)
{
	const APlayerController* PlayerController = Cast<APlayerController>(Controller);
	if (!PlayerController)
	{
		return false;
	}
	if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		return LocalPlayer->GetLocalPlayerIndex() == 1;
	}
	// Login 중(InitNewPlayer → 시작 지점 캐시)에는 아직 LocalPlayer가 붙지 않는다.
	// 이 시점엔 먼저 접속한 1P 컨트롤러가 이미 월드에 있으므로, 첫 컨트롤러가 아니면 2P로 본다.
	const UWorld* World = PlayerController->GetWorld();
	const APlayerController* FirstPlayerController = World ? World->GetFirstPlayerController() : nullptr;
	return FirstPlayerController && FirstPlayerController != PlayerController;
}

void ALocalCoopGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);
	UCoopInteractComponent::EnsureOn(PlayerPawn);
	// 공용 발소리·점프·착지 + 리스너 위치(효과음 적용 기획 6.1, 6.3)
	UCXPlayerAudioComponent::EnsureOn(PlayerPawn);
	// 사망/부활 상태 + 낙사 감지 + 안전 지면 기록(게임 오버화면 기획 9장)
	UCXPlayerLifeComponent::EnsureOn(PlayerPawn);
}

// ---------------------------------------------------------------- 게임 클리어

void ALocalCoopGameMode::NotifyGameCleared()
{
	// 게임 오버가 먼저 떴으면 클리어 무시(판정은 서버 한 곳에서 순서대로 — 클리어 화면 기획 6장)
	if (bGameCleared || bGameOver)
	{
		return;
	}
	bGameCleared = true;

	// 클리어하면 세이브 초기화 → 다음 시작화면에는 NEW GAME만
	UCXSaveGame::DeleteSlot();

	UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this);
	if (Menus)
	{
		// 클리어화면(진입 연출 포함) 동안 ESC/Start로 일시정지 메뉴가 열리지 않게. 맵 이동 시 서브시스템이 자동 해제.
		Menus->SetMenuBlocked(true);
	}

	// 게임플레이 입력 차단(두 폰 모두) + 제자리 정지
	APlayerController* FirstLocal = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}
		if (!FirstLocal || (PC->GetLocalPlayer() && PC->GetLocalPlayer()->GetLocalPlayerIndex() == 0))
		{
			FirstLocal = PC;
		}
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->DisableInput(PC);
			if (ACharacter* Character = Cast<ACharacter>(Pawn))
			{
				Character->GetCharacterMovement()->StopMovementImmediately();
			}
		}
		PC->FlushPressedKeys();
	}

	// 인게임 BGM을 내려 정지 + 클리어 징글 슬롯
	if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
	{
		Audio->StopMusic(ClearMusicFadeTime);
	}
	if (ClearJingle)
	{
		UGameplayStatics::PlaySound2D(this, ClearJingle, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
	}

	// 클리어화면: 화면 전체에 하나(1P 컨트롤러 소유, 누구나 조작). 위젯이 딤 0.5초 → 패널 페이드인을 직접 연출한다.
	if (Menus && FirstLocal)
	{
		Menus->PushMenu(UCXGameClearWidget::StaticClass(), FirstLocal, 200);
	}
	// 딤이 다 깔리면 월드 정지(부활 카운트다운·해킹 타이머 포함)
	GetWorldTimerManager().SetTimer(ClearPauseTimer, this, &ALocalCoopGameMode::PauseForGameClear, FMath::Max(0.01f, ClearPauseDelay), false);
}

void ALocalCoopGameMode::PauseForGameClear()
{
	UGameplayStatics::SetGamePaused(this, true);
}

void ALocalCoopGameMode::CXSpawnExitDoor(float OffsetY, float OffsetX)
{
	const ACXCoopExitDoor* Door = ACXCoopExitDoor::DebugSpawnExitDoor(this, OffsetY, OffsetX);
	UE_LOG(LogTemp, Log, TEXT("CXSpawnExitDoor: %s"), Door ? *Door->GetActorLocation().ToString() : TEXT("failed"));
}

// ---------------------------------------------------------------- 사망 / 부활 / 게임 오버

bool ALocalCoopGameMode::IsControllerAlive(const AController* Controller) const
{
	if (!Controller || DeadWithoutPawn.Contains(TWeakObjectPtr<AController>(const_cast<AController*>(Controller))))
	{
		return false;
	}
	return UCXPlayerLifeComponent::IsPawnAlive(Controller->GetPawn());
}

void ALocalCoopGameMode::NotifyPlayerDied(AController* Controller, ECXDeathCause Cause)
{
	// 클리어 후 / 게임 오버 후에는 사망 무시
	if (!Controller || bGameOver || bGameCleared)
	{
		return;
	}
	// 일시정지 메뉴 중에는 월드가 멈춰 사망이 일어나지 않는다(디버그 명령만 가능) — 메뉴가 겹치지 않게 무시
	if (const UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this))
	{
		if (Menus->IsPauseMenuOpen())
		{
			UE_LOG(LogTemp, Warning, TEXT("NotifyPlayerDied: ignored while pause menu is open"));
			return;
		}
	}

	const TWeakObjectPtr<AController> Weak(Controller);
	APawn* Pawn = Controller->GetPawn();
	UCXPlayerLifeComponent* Life = Pawn ? Pawn->FindComponentByClass<UCXPlayerLifeComponent>() : nullptr;
	if (Pawn && Life && !Life->IsAlive())
	{
		return;
	}
	if (!Pawn)
	{
		// 안전망 경로: 폰이 이미 파괴됨(엔진 KillZ 등). 같은 컨트롤러 중복 보고 무시
		if (DeadWithoutPawn.Contains(Weak) || RespawnTimers.Contains(Weak))
		{
			return;
		}
		DeadWithoutPawn.Add(Weak);
	}

	const double Now = GetWorld()->GetTimeSeconds();
	AController* PrevDead = LastDeathController.Get();
	const bool bSimultaneous = PrevDead && PrevDead != Controller && (Now - LastDeathTime) <= SimultaneousDeathWindow;
	LastDeathController = Controller;
	LastDeathTime = Now;

	bool bPartnerAlive = false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC != Controller && IsControllerAlive(PC))
		{
			bPartnerAlive = true;
			break;
		}
	}

	const bool bSister = IsSisterController(Controller);
	UE_LOG(LogTemp, Log, TEXT("NotifyPlayerDied: %s cause=%d partnerAlive=%d"), bSister ? TEXT("SISTER") : TEXT("BROTHER"), static_cast<int32>(Cause), bPartnerAlive ? 1 : 0);

	if (!bPartnerAlive)
	{
		// 부활 대기 중 나머지도 사망(또는 혼자) → 게임 오버. 0.1초 안이면 동시 사망(두 이름)
		if (Life)
		{
			Life->EnterDeath(Cause, -1.0f);
		}
		bool bBrotherFell = !bSister;
		bool bSisterFell = bSister;
		if (bSimultaneous)
		{
			if (IsSisterController(PrevDead))
			{
				bSisterFell = true;
			}
			else
			{
				bBrotherFell = true;
			}
		}
		TriggerGameOver(bBrotherFell, bSisterFell);
		return;
	}

	// 부활 대기 10초(월드 타이머 — ESC 메뉴 일시정지 중 멈춤)
	if (Life)
	{
		Life->EnterDeath(Cause, RespawnDelay);
	}
	FTimerHandle& RespawnHandle = RespawnTimers.FindOrAdd(Weak);
	GetWorldTimerManager().SetTimer(RespawnHandle, FTimerDelegate::CreateUObject(this, &ALocalCoopGameMode::RespawnPlayer, Weak), FMath::Max(0.01f, RespawnDelay), false);

	// 사망자 칸 카메라: 1초 그 자리 → 파트너 추적(카메라 컴포넌트는 건드리지 않고 뷰 타깃만 바꾼다)
	if (Cast<APlayerController>(Controller))
	{
		FTimerHandle& ViewHandle = DeadViewTimers.FindOrAdd(Weak);
		GetWorldTimerManager().SetTimer(ViewHandle, FTimerDelegate::CreateUObject(this, &ALocalCoopGameMode::SwitchDeadView, Weak), FMath::Max(0.01f, DeadViewHoldTime), false);
	}
}

void ALocalCoopGameMode::SwitchDeadView(TWeakObjectPtr<AController> WeakController)
{
	DeadViewTimers.Remove(WeakController);
	APlayerController* PC = Cast<APlayerController>(WeakController.Get());
	if (!PC || bGameOver || IsControllerAlive(PC))
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Other = It->Get();
		if (Other && Other != PC && IsControllerAlive(Other))
		{
			PC->SetViewTargetWithBlend(Other->GetPawn(), DeadViewBlendTime);
			return;
		}
	}
}

void ALocalCoopGameMode::RespawnPlayer(TWeakObjectPtr<AController> WeakController)
{
	RespawnTimers.Remove(WeakController);
	if (FTimerHandle* ViewHandle = DeadViewTimers.Find(WeakController))
	{
		GetWorldTimerManager().ClearTimer(*ViewHandle);
		DeadViewTimers.Remove(WeakController);
	}

	AController* Controller = WeakController.Get();
	if (!Controller || bGameOver || bGameCleared)
	{
		return;
	}

	// 부활 위치: 살아 있는 파트너 근처 안전 지면(기획 2.1 기본안) → 실패하면 맵 시작 지점
	APawn* Pawn = Controller->GetPawn();
	FVector Location = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	FRotator Rotation = Pawn ? Pawn->GetActorRotation() : FRotator::ZeroRotator;
	bool bFound = false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It && !bFound; ++It)
	{
		const APlayerController* Other = It->Get();
		if (Other && Other != Controller && IsControllerAlive(Other))
		{
			if (const UCXPlayerLifeComponent* PartnerLife = Other->GetPawn()->FindComponentByClass<UCXPlayerLifeComponent>())
			{
				bFound = PartnerLife->FindRespawnSpotFor(Pawn, Location, Rotation);
			}
		}
	}
	if (!bFound)
	{
		// TODO(세이브포인트): 마지막 세이브포인트가 생기면 여기서 먼저 시도
		if (const AActor* Start = FindPlayerStart(Controller))
		{
			Location = Start->GetActorLocation();
			Rotation = Start->GetActorRotation();
		}
	}
	UE_LOG(LogTemp, Log, TEXT("RespawnPlayer: %s at %s (%s)"), IsSisterController(Controller) ? TEXT("SISTER") : TEXT("BROTHER"),
		*Location.ToCompactString(), bFound ? TEXT("near partner") : TEXT("player start"));

	UCXPlayerLifeComponent* Life = Pawn ? Pawn->FindComponentByClass<UCXPlayerLifeComponent>() : nullptr;
	if (Pawn && Life && !DeadWithoutPawn.Contains(WeakController))
	{
		Life->Revive(Location, Rotation, RespawnInvulnerableTime);
	}
	else
	{
		// 폰이 파괴된 경우: 새로 스폰(SetPlayerDefaults가 컴포넌트를 다시 붙인다)
		DeadWithoutPawn.Remove(WeakController);
		if (Pawn)
		{
			Pawn->Destroy();
		}
		RestartPlayerAtTransform(Controller, FTransform(FRotator(0.0f, Rotation.Yaw, 0.0f), Location));
		if (UCXPlayerLifeComponent* NewLife = UCXPlayerLifeComponent::EnsureOn(Controller->GetPawn()))
		{
			NewLife->StartInvulnerability(RespawnInvulnerableTime);
		}
	}

	// 카메라를 자기 폰으로 복귀
	if (APlayerController* PC = Cast<APlayerController>(Controller))
	{
		if (APawn* NewPawn = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(NewPawn, DeadViewBlendTime * 0.6f);
		}
	}
}

void ALocalCoopGameMode::ClearLifeTimers()
{
	FTimerManager& Timers = GetWorldTimerManager();
	for (TPair<TWeakObjectPtr<AController>, FTimerHandle>& Pair : RespawnTimers)
	{
		Timers.ClearTimer(Pair.Value);
	}
	for (TPair<TWeakObjectPtr<AController>, FTimerHandle>& Pair : DeadViewTimers)
	{
		Timers.ClearTimer(Pair.Value);
	}
	RespawnTimers.Reset();
	DeadViewTimers.Reset();
}

void ALocalCoopGameMode::TriggerGameOver(bool bBrotherFell, bool bSisterFell)
{
	if (bGameOver || bGameCleared)
	{
		return;
	}
	bGameOver = true;
	ClearLifeTimers();

	UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this);
	if (Menus)
	{
		// 게임 오버 중(진입 연출 포함) ESC/Start로 일시정지 메뉴가 열리지 않게. 맵 이동 시 서브시스템이 자동 해제.
		Menus->SetMenuBlocked(true);
	}

	// 부활 대기 중이던 사람도 부활 없음으로 확정 + 게임플레이 입력 차단
	APlayerController* FirstLocal = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		if (APawn* Pawn = PC->GetPawn())
		{
			if (UCXPlayerLifeComponent* Life = Pawn->FindComponentByClass<UCXPlayerLifeComponent>())
			{
				Life->CancelRespawn();
			}
			Pawn->DisableInput(PC);
		}
		if (PC->IsLocalController())
		{
			PC->FlushPressedKeys();
			if (!FirstLocal || (PC->GetLocalPlayer() && PC->GetLocalPlayer()->GetLocalPlayerIndex() == 0))
			{
				FirstLocal = PC;
			}
		}
	}

	// BGM 컷 + 저음 신호 끊김 효과 슬롯
	if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
	{
		Audio->StopMusic(GameOverMusicFadeTime);
	}
	if (GameOverSound)
	{
		UGameplayStatics::PlaySound2D(this, GameOverSound, 1.0f, 1.0f, 0.0f, nullptr, nullptr, true);
	}

	// 마지막 사망 순간 슬로우(0.3) → 실시간 0.5초 뒤 월드 정지 + 전체 화면 전환
	UGameplayStatics::SetGlobalTimeDilation(this, GameOverTimeDilation);

	// 게임 오버화면: 화면 전체 하나(1P 컨트롤러 소유, 누구나 조작). 위젯이 노이즈/딤/패널 연출을 실시간으로 직접 한다.
	if (Menus && FirstLocal)
	{
		if (UCXGameOverWidget* Widget = Cast<UCXGameOverWidget>(Menus->PushMenu(UCXGameOverWidget::StaticClass(), FirstLocal, 200)))
		{
			Widget->SetFallen(bBrotherFell, bSisterFell);
		}
	}
	// 월드 타이머는 딜레이션을 받으므로 게임 시간 = 실시간 × 딜레이션
	const float Delay = FMath::Max(0.01f, GameOverSlowMoTime * FMath::Max(0.01f, GameOverTimeDilation));
	GetWorldTimerManager().SetTimer(GameOverPauseTimer, this, &ALocalCoopGameMode::EnterGameOverPause, Delay, false);
	UE_LOG(LogTemp, Log, TEXT("TriggerGameOver: brother=%d sister=%d"), bBrotherFell ? 1 : 0, bSisterFell ? 1 : 0);
}

void ALocalCoopGameMode::EnterGameOverPause()
{
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	UGameplayStatics::SetGamePaused(this, true);
	// 분할선이 사라지며 전체 화면 하나로(맵 이동 시 BeginPlay/메뉴 서브시스템이 되돌린다)
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->SetForceDisableSplitscreen(true);
	}
}

// ---------------------------------------------------------------- 디버그

bool ALocalCoopGameMode::DebugKillPlayer(int32 LocalPlayerIndex)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->GetLocalPlayer() && PC->GetLocalPlayer()->GetLocalPlayerIndex() == LocalPlayerIndex)
		{
			const bool bWasAlive = IsControllerAlive(PC);
			NotifyPlayerDied(PC, ECXDeathCause::Debug);
			return bWasAlive && !IsControllerAlive(PC);
		}
	}
	return false;
}

void ALocalCoopGameMode::CXKillPlayer(const FString& Who)
{
	const FString Arg = Who.TrimStartAndEnd().ToLower();
	TArray<APlayerController*> Targets;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		const bool bSister = IsSisterController(PC);
		const int32 Index = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetLocalPlayerIndex() : INDEX_NONE;
		const bool bMatch = Arg == TEXT("all") || Arg == TEXT("both")
			|| ((Arg == TEXT("brother") || Arg == TEXT("b")) && !bSister)
			|| ((Arg == TEXT("sister") || Arg == TEXT("s")) && bSister)
			|| (Arg.IsNumeric() && FCString::Atoi(*Arg) == Index);
		if (bMatch)
		{
			Targets.Add(PC);
		}
	}
	for (APlayerController* PC : Targets)
	{
		NotifyPlayerDied(PC, ECXDeathCause::Debug);
	}
	UE_LOG(LogTemp, Log, TEXT("CXKillPlayer %s: %d target(s) - %s"), *Arg, Targets.Num(), *GetLifeDebugState());
}

FString ALocalCoopGameMode::GetLifeDebugState() const
{
	FString Out = FString::Printf(TEXT("gameOver=%d cleared=%d"), bGameOver ? 1 : 0, bGameCleared ? 1 : 0);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		const int32 Index = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetLocalPlayerIndex() : INDEX_NONE;
		const APawn* Pawn = PC->GetPawn();
		const UCXPlayerLifeComponent* Life = Pawn ? Pawn->FindComponentByClass<UCXPlayerLifeComponent>() : nullptr;
		const AActor* ViewTarget = PC->GetViewTarget();
		Out += FString::Printf(TEXT(" | P%d %s pawn=%s view=%s %s"), Index, IsSisterController(PC) ? TEXT("SISTER") : TEXT("BROTHER"),
			Pawn ? *Pawn->GetName() : TEXT("none"), ViewTarget ? *ViewTarget->GetName() : TEXT("none"),
			Life ? *Life->GetDebugState() : TEXT("(no life)"));
	}
	return Out;
}
