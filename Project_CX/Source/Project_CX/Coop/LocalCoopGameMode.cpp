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
#include "CXHealthComponent.h"
#include "SpiderBot.h"
#include "GameFramework/DamageType.h"
#include "Engine/GameViewportClient.h"
#include "CoopInteractComponent.h"
#include "CXPlayerAudioComponent.h"
#include "CXCoopExitDoor.h"
#include "CXSavePoint.h"
#include "CXLevelExit.h"
#include "HackTerminal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "CXGameInstance.h"
#include "Components/CapsuleComponent.h"
#include "Misc/PackageName.h"
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

	// 세이브 복도(Map2와 보스맵 사이 쉼터): 시작화면 곡(District 5 - Signal, 70bpm, 따뜻한 패드)을 "안전 지대 테마"로 재사용.
	// Map1 곡(압박감 있는 언더스코어)보다 숨 고르기에 맞다. 인트로(0~21초)는 처음 한 번, 이후 21~224.5초 반복(시작화면과 같은 값).
	FCXMusicTrack CorridorTrack;
	CorridorTrack.Music = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Project_CX/Audio/BGM/SW_BGM_Title.SW_BGM_Title")));
	CorridorTrack.LoopStart = 21.0f;
	CorridorTrack.LoopEnd = 224.5f;
	CorridorTrack.FadeIn = 2.0f;
	CorridorTrack.Crossfade = 2.0f;
	MapMusic.Add(TEXT("Map_SaveCorridor"), CorridorTrack);
}

void ALocalCoopGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// 이어하기/RETRY: 시작화면·게임 오버 위젯이 넘긴 세이브를 소비(한 번만). 이 맵 것일 때만 세이브포인트 스폰.
	UCXGameInstance* GI = Cast<UCXGameInstance>(GetGameInstance());
	if (GI && GI->PendingContinueSave)
	{
		const UCXSaveGame* Save = GI->PendingContinueSave;
		const FString CurrentMap = UCXSaveGame::GetWorldMapPackageName(this);
		if (Save->MapName.Equals(CurrentMap, ESearchCase::IgnoreCase))
		{
			bSpawnAtSave = true;
			SpawnSavePointId = Save->SavePointId;
			SpawnSaveLocation = Save->SpawnLocation;
			SpawnSaveYaw = Save->SpawnYaw;
		}
		UE_LOG(LogTemp, Log, TEXT("LocalCoopGameMode::InitGame: pending save map=%s point=%s current=%s -> spawnAtSave=%d"),
			*Save->MapName, *Save->SavePointId.ToString(), *CurrentMap, bSpawnAtSave ? 1 : 0);
		GI->PendingContinueSave = nullptr;
	}
}

void ALocalCoopGameMode::RestartPlayer(AController* NewPlayer)
{
	// 첫 스폰(폰 없음)만 세이브포인트로. 부활은 RespawnPlayer 경로(Revive/RestartPlayerAtTransform)라 여기 오지 않는다.
	FTransform SpawnTransform;
	if (bSpawnAtSave && NewPlayer && !NewPlayer->GetPawn() && Cast<APlayerController>(NewPlayer)
		&& GetSaveSpawnTransform(NewPlayer, false, SpawnTransform))
	{
		RestartPlayerAtTransform(NewPlayer, SpawnTransform);
		if (ACXSavePoint* SavePoint = ACXSavePoint::FindById(GetWorld(), SpawnSavePointId))
		{
			// 스폰 직후 같은 세이브포인트에서 다시 저장하지 않게(나갔다 들어오면 저장)
			SavePoint->MarkSpawnedHere();
			ActiveSavePoint = SavePoint;
		}
		UE_LOG(LogTemp, Log, TEXT("RestartPlayer: %s at save point %s %s (pawn=%s)"), IsSisterController(NewPlayer) ? TEXT("SISTER") : TEXT("BROTHER"),
			*SpawnSavePointId.ToString(), *SpawnTransform.GetLocation().ToCompactString(), NewPlayer->GetPawn() ? *NewPlayer->GetPawn()->GetName() : TEXT("none"));
		if (NewPlayer->GetPawn())
		{
			return;
		}
		// 스폰 실패(충돌 등) → 기본 시작 지점
		UE_LOG(LogTemp, Warning, TEXT("RestartPlayer: save point spawn failed, falling back to player start"));
	}
	Super::RestartPlayer(NewPlayer);
}

bool ALocalCoopGameMode::GetSaveSpawnTransform(AController* Controller, bool bForRespawn, FTransform& OutTransform) const
{
	const bool bSister = IsSisterController(Controller);
	FVector Floor;
	float Yaw = 0.0f;
	const ACXSavePoint* SavePoint = bForRespawn ? ActiveSavePoint.Get() : ACXSavePoint::FindById(GetWorld(), SpawnSavePointId);
	if (SavePoint)
	{
		Floor = SavePoint->GetSpawnFloorFor(bSister);
		Yaw = SavePoint->GetActorRotation().Yaw;
	}
	else if (!bForRespawn && bSpawnAtSave && !SpawnSaveLocation.IsNearlyZero())
	{
		// 세이브포인트 액터가 없음(런타임 디버그 세이브포인트 등) → 저장된 위치 기준 같은 배치
		Floor = SpawnSaveLocation + FRotator(0.0f, SpawnSaveYaw, 0.0f).RotateVector(FVector(-40.0f, bSister ? 90.0f : -90.0f, 0.0f));
		Yaw = SpawnSaveYaw;
	}
	else
	{
		return false;
	}

	float HalfHeight = 96.0f;
	const APawn* ExistingPawn = Controller ? Controller->GetPawn() : nullptr;
	UClass* PawnClass = ExistingPawn ? ExistingPawn->GetClass() : const_cast<ALocalCoopGameMode*>(this)->GetDefaultPawnClassForController(Controller);
	if (const ACharacter* CharacterCDO = PawnClass ? Cast<ACharacter>(PawnClass->GetDefaultObject()) : nullptr)
	{
		if (const UCapsuleComponent* Capsule = CharacterCDO->GetCapsuleComponent())
		{
			HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	OutTransform = FTransform(FRotator(0.0f, Yaw, 0.0f), Floor + FVector(0.0f, 0.0f, HalfHeight + 5.0f));
	return true;
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
	// 체력 100 — 피해는 엔진 ApplyDamage → OnTakeAnyDamage, 0이면 Kill(Health)(전투 시스템 기획 6.2)
	UCXHealthComponent::EnsureOn(PlayerPawn);
}

// ---------------------------------------------------------------- 게임 클리어

void ALocalCoopGameMode::NotifyGameCleared()
{
	// 게임 오버가 먼저 떴으면 클리어 무시(판정은 서버 한 곳에서 순서대로 — 클리어 화면 기획 6장)
	if (bGameCleared || bGameOver || bTravelling)
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

AActor* ALocalCoopGameMode::DebugSpawnMemoryTerminal(int32 LocalPlayerIndex, float OffsetY, float DepthX)
{
	const APawn* Pawn = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->GetLocalPlayer() && PC->GetLocalPlayer()->GetLocalPlayerIndex() == LocalPlayerIndex)
		{
			Pawn = PC->GetPawn();
		}
	}
	if (!Pawn)
	{
		return nullptr;
	}

	// 바닥 찾기(폰 기준 앞 OffsetY, 뒤쪽 DepthX)
	const FVector Base = Pawn->GetActorLocation() + FVector(DepthX, OffsetY, 0.0f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXSpawnMemoryTerminal), false, Pawn);
	FHitResult Hit;
	FVector SpawnLocation = Base - FVector(0.0f, 0.0f, 90.0f);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Base + FVector(0.0f, 0.0f, 200.0f), Base - FVector(0.0f, 0.0f, 800.0f), ECC_Pawn, Params))
	{
		SpawnLocation = Hit.ImpactPoint;
	}

	// 배치 스크립트(add_map1_hacking.py / build_save_corridor.py)와 같은 단말기 모양: 환풍구 메시 세워서, 화면 쪽을 향하게
	AHackTerminal* Terminal = GetWorld()->SpawnActorDeferred<AHackTerminal>(AHackTerminal::StaticClass(), FTransform(SpawnLocation), nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Terminal)
	{
		return nullptr;
	}
	Terminal->TerminalName = TEXT("DEBUG_ECHO");
	Terminal->PuzzleType = EHackPuzzleType::MemoryPattern;
	if (UStaticMeshComponent* Mesh = Terminal->FindComponentByClass<UStaticMeshComponent>())
	{
		if (UStaticMesh* VentMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/SoulCity/Environment/Meshes/Building_Slum/SM_Slums_Vent_02b.SM_Slums_Vent_02b")))
		{
			Mesh->SetStaticMesh(VentMesh);
			Mesh->SetRelativeScale3D(FVector(0.9f, 1.4f, 1.5f));
			Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, 101.0f * 1.5f * 0.5f));
			Mesh->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
		}
	}
	Terminal->FinishSpawning(FTransform(SpawnLocation));
	UE_LOG(LogTemp, Log, TEXT("DebugSpawnMemoryTerminal: %s at %s"), *Terminal->GetName(), *SpawnLocation.ToString());
	return Terminal;
}

void ALocalCoopGameMode::CXSpawnMemoryTerminal(float OffsetY, float DepthX)
{
	// 여동생을 조작하는 로컬 플레이어 기준
	int32 Index = 1;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->GetLocalPlayer() && IsSisterController(PC))
		{
			Index = PC->GetLocalPlayer()->GetLocalPlayerIndex();
		}
	}
	DebugSpawnMemoryTerminal(Index, OffsetY, DepthX);
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
	// 클리어 후 / 게임 오버 후 / 맵 이동 페이드 중에는 사망 무시
	if (!Controller || bGameOver || bGameCleared || bTravelling)
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
	FTransform SaveTransform;
	const bool bAtSave = !bFound && GetSaveSpawnTransform(Controller, true, SaveTransform);
	if (bAtSave)
	{
		// 파트너 근처 실패 → 이 맵의 마지막 세이브포인트
		Location = SaveTransform.GetLocation();
		Rotation = SaveTransform.Rotator();
	}
	else if (!bFound)
	{
		if (const AActor* Start = FindPlayerStart(Controller))
		{
			Location = Start->GetActorLocation();
			Rotation = Start->GetActorRotation();
		}
	}
	UE_LOG(LogTemp, Log, TEXT("RespawnPlayer: %s at %s (%s)"), IsSisterController(Controller) ? TEXT("SISTER") : TEXT("BROTHER"),
		*Location.ToCompactString(), bFound ? TEXT("near partner") : (bAtSave ? TEXT("save point") : TEXT("player start")));

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

float ALocalCoopGameMode::DebugDamagePlayer(int32 LocalPlayerIndex, float Amount)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->GetLocalPlayer() && PC->GetLocalPlayer()->GetLocalPlayerIndex() == LocalPlayerIndex && PC->GetPawn())
		{
			// 실제 공격과 같은 엔진 경로(서버 권위 + CanBeDamaged — 무적·사망 중이면 0)
			return UGameplayStatics::ApplyDamage(PC->GetPawn(), Amount, nullptr, nullptr, UDamageType::StaticClass());
		}
	}
	return 0.0f;
}

void ALocalCoopGameMode::CXDamagePlayer(const FString& Who, float Amount)
{
	const FString Arg = Who.TrimStartAndEnd().ToLower();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->GetPawn())
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
			const float Applied = UGameplayStatics::ApplyDamage(PC->GetPawn(), Amount, nullptr, nullptr, UDamageType::StaticClass());
			UE_LOG(LogTemp, Log, TEXT("CXDamagePlayer %s: %s applied=%.0f"), *Arg, bSister ? TEXT("SISTER") : TEXT("BROTHER"), Applied);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("CXDamagePlayer: %s"), *GetCombatDebugState());
}

AActor* ALocalCoopGameMode::DebugSpawnSpider(int32 LocalPlayerIndex, float Offset)
{
	const APawn* Pawn = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->GetLocalPlayer() && PC->GetLocalPlayer()->GetLocalPlayerIndex() == LocalPlayerIndex)
		{
			Pawn = PC->GetPawn();
		}
	}
	if (!Pawn)
	{
		return nullptr;
	}

	// 맵 진행 방향 = +Y(사이드뷰 스크롤 축). 같은 깊이(X)에서 Offset만큼 떨어진 바닥을 찾는다.
	const FVector Base = Pawn->GetActorLocation() + FVector(0.0f, Offset, 0.0f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXSpawnSpider), false, Pawn);
	FHitResult Hit;
	FVector SpawnLocation = Base;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Base + FVector(0.0f, 0.0f, 200.0f), Base - FVector(0.0f, 0.0f, 800.0f), ECC_Pawn, Params))
	{
		const ASpiderBot* Defaults = GetDefault<ASpiderBot>();
		SpawnLocation = Hit.ImpactPoint + FVector(0.0f, 0.0f, Defaults->GetSimpleCollisionHalfHeight() + 2.0f);
	}
	const FRotator Facing(0.0f, Offset >= 0.0f ? -90.0f : 90.0f, 0.0f);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ASpiderBot* Spider = GetWorld()->SpawnActor<ASpiderBot>(ASpiderBot::StaticClass(), SpawnLocation, Facing, SpawnParams);
	UE_LOG(LogTemp, Log, TEXT("DebugSpawnSpider: %s"), Spider ? *Spider->GetDebugState() : TEXT("failed"));
	return Spider;
}

void ALocalCoopGameMode::CXSpawnSpider(const FString& Who, float Offset)
{
	const FString Arg = Who.TrimStartAndEnd().ToLower();
	int32 Index = 0;
	if (Arg.IsNumeric())
	{
		Index = FCString::Atoi(*Arg);
	}
	else
	{
		// brother/sister → 그 캐릭터를 조작하는 로컬 플레이어 인덱스
		const bool bWantSister = Arg == TEXT("sister") || Arg == TEXT("s");
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->GetLocalPlayer() && IsSisterController(PC) == bWantSister)
			{
				Index = PC->GetLocalPlayer()->GetLocalPlayerIndex();
			}
		}
	}
	DebugSpawnSpider(Index, Offset);
}

AActor* ALocalCoopGameMode::DebugStunNearestSpider(int32 LocalPlayerIndex)
{
	const APawn* Pawn = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->GetLocalPlayer() && PC->GetLocalPlayer()->GetLocalPlayerIndex() == LocalPlayerIndex)
		{
			Pawn = PC->GetPawn();
		}
	}
	if (!Pawn)
	{
		return nullptr;
	}
	ASpiderBot* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ASpiderBot> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), Pawn->GetActorLocation());
		if (It->IsHackTargetAvailable() && DistSq < BestDistSq)
		{
			Best = *It;
			BestDistSq = DistSq;
		}
	}
	const bool bStunned = Best && Best->ApplyHackStun(nullptr);
	UE_LOG(LogTemp, Log, TEXT("CXStunSpider: %s"), bStunned ? *Best->GetDebugState() : TEXT("no stunnable spider"));
	return bStunned ? Best : nullptr;
}

void ALocalCoopGameMode::CXStunSpider(const FString& Who)
{
	const FString Arg = Who.TrimStartAndEnd().ToLower();
	int32 Index = 0;
	if (Arg.IsNumeric())
	{
		Index = FCString::Atoi(*Arg);
	}
	else
	{
		const bool bWantSister = Arg != TEXT("brother") && Arg != TEXT("b");
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->GetLocalPlayer() && IsSisterController(PC) == bWantSister)
			{
				Index = PC->GetLocalPlayer()->GetLocalPlayerIndex();
			}
		}
	}
	DebugStunNearestSpider(Index);
}

FString ALocalCoopGameMode::GetCombatDebugState() const
{
	FString Out;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const UCXHealthComponent* Health = UCXHealthComponent::FindOn(Pawn);
		const UCXPlayerLifeComponent* Life = Pawn->FindComponentByClass<UCXPlayerLifeComponent>();
		Out += FString::Printf(TEXT("%s %s canDmg=%d life=%s | "), IsSisterController(PC) ? TEXT("SISTER") : TEXT("BROTHER"),
			Health ? *Health->GetDebugState() : TEXT("(no health)"), Pawn->CanBeDamaged() ? 1 : 0,
			Life ? *UEnum::GetValueAsString(Life->GetLifeState()) : TEXT("-"));
	}
	for (TActorIterator<ASpiderBot> It(GetWorld()); It; ++It)
	{
		Out += It->GetDebugState() + TEXT(" | ");
	}
	return Out;
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

// ---------------------------------------------------------------- 세이브포인트 / 맵 이동

void ALocalCoopGameMode::NotifyProgressSaved(ACXSavePoint* SavePoint)
{
	ActiveSavePoint = SavePoint;
}

bool ALocalCoopGameMode::TravelToMap(const FString& MapPackageName)
{
	if (bTravelling || bGameOver || bGameCleared || MapPackageName.IsEmpty())
	{
		return false;
	}
	if (!FPackageName::DoesPackageExist(MapPackageName))
	{
		UE_LOG(LogTemp, Warning, TEXT("TravelToMap: %s does not exist"), *MapPackageName);
		return false;
	}
	bTravelling = true;
	PendingTravelMap = MapPackageName;
	ClearLifeTimers();

	// 맵 이동은 새 맵의 시작 지점에서(이어하기 세이브가 남아 있으면 안 됨)
	if (UCXGameInstance* GI = Cast<UCXGameInstance>(GetGameInstance()))
	{
		GI->PendingContinueSave = nullptr;
	}
	if (UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this))
	{
		// 페이드 중 ESC 메뉴 금지. 둘 다 맵 이동(PreLoadMap) 때 서브시스템이 푼다.
		Menus->SetMenuBlocked(true);
		Menus->NotifyLeavingMap();
	}

	// 두 폰 입력 차단 + 제자리 정지 + 칸마다 검정 페이드(카메라 페이드 — 원격 클라이언트도 RPC로 같이)
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->DisableInput(PC);
			if (ACharacter* Character = Cast<ACharacter>(Pawn))
			{
				Character->GetCharacterMovement()->StopMovementImmediately();
			}
		}
		if (PC->IsLocalController())
		{
			PC->FlushPressedKeys();
		}
		PC->ClientSetCameraFade(true, FColor::Black, FVector2D(0.0f, 1.0f), FMath::Max(0.01f, TravelFadeTime), false, true);
	}
	if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
	{
		Audio->StopMusic(TravelFadeTime);
	}
	GetWorldTimerManager().SetTimer(TravelTimer, this, &ALocalCoopGameMode::ExecuteTravel, FMath::Max(0.01f, TravelFadeTime + 0.05f), false);
	UE_LOG(LogTemp, Log, TEXT("TravelToMap: %s (fade %.2fs)"), *MapPackageName, TravelFadeTime);
	return true;
}

void ALocalCoopGameMode::ExecuteTravel()
{
	if (PendingTravelMap.IsEmpty())
	{
		return;
	}
	const FString Map = PendingTravelMap;
	if (GetNetMode() == NM_Standalone)
	{
		// 로컬 협동: 2P 로컬 플레이어는 게임 인스턴스에 남아 새 맵에서 컨트롤러가 다시 생긴다(BeginPlay는 없을 때만 생성)
		UGameplayStatics::OpenLevel(this, FName(*Map));
	}
	else
	{
		// 리슨 서버: 접속한 클라이언트를 데리고 이동
		GetWorld()->ServerTravel(Map + TEXT("?listen"));
	}
}

void ALocalCoopGameMode::CXSaveHere()
{
	const AActor* SavePoint = DebugSaveHere();
	UE_LOG(LogTemp, Log, TEXT("CXSaveHere: %s | %s"), SavePoint ? *SavePoint->GetName() : TEXT("failed"), *UCXSaveGame::GetSlotDebugState());
}

AActor* ALocalCoopGameMode::DebugSaveHere()
{
	ACXSavePoint* SavePoint = ACXSavePoint::DebugSpawnSavePoint(this, 0.0f, 0.0f);
	return SavePoint && SavePoint->SaveNow() ? SavePoint : nullptr;
}

void ALocalCoopGameMode::CXSpawnSavePoint(float OffsetY)
{
	const ACXSavePoint* SavePoint = ACXSavePoint::DebugSpawnSavePoint(this, OffsetY, 0.0f);
	UE_LOG(LogTemp, Log, TEXT("CXSpawnSavePoint: %s"), SavePoint ? *SavePoint->GetDebugState() : TEXT("failed"));
}

bool ALocalCoopGameMode::DebugGotoNextMap()
{
	for (TActorIterator<ACXLevelExit> It(GetWorld()); It; ++It)
	{
		return It->ForceTravel();
	}
	UE_LOG(LogTemp, Warning, TEXT("DebugGotoNextMap: no ACXLevelExit in this level"));
	return false;
}

void ALocalCoopGameMode::CXGotoNextMap()
{
	const bool bStarted = DebugGotoNextMap();
	UE_LOG(LogTemp, Log, TEXT("CXGotoNextMap: %s"), bStarted ? TEXT("travelling") : TEXT("not started"));
}

void ALocalCoopGameMode::CXDeleteSave()
{
	UE_LOG(LogTemp, Log, TEXT("CXDeleteSave: %s"), UCXSaveGame::DeleteSlot() ? TEXT("deleted") : TEXT("failed"));
}

FString ALocalCoopGameMode::GetFlowDebugState() const
{
	const ACXSavePoint* Active = ActiveSavePoint.Get();
	FString Out = FString::Printf(TEXT("map=%s travelling=%d target=%s spawnAtSave=%d spawnId=%s active=%s | slot: %s"),
		*UCXSaveGame::GetWorldMapPackageName(this), bTravelling ? 1 : 0, *PendingTravelMap, bSpawnAtSave ? 1 : 0, *SpawnSavePointId.ToString(),
		Active ? *Active->GetSavePointId().ToString() : TEXT("none"), *UCXSaveGame::GetSlotDebugState());
	for (TActorIterator<ACXSavePoint> It(GetWorld()); It; ++It)
	{
		Out += TEXT(" | SP ") + It->GetDebugState();
	}
	for (TActorIterator<ACXLevelExit> It(GetWorld()); It; ++It)
	{
		Out += TEXT(" | EXIT ") + It->GetDebugState();
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		Out += FString::Printf(TEXT(" | %s %s"), IsSisterController(PC) ? TEXT("SISTER") : TEXT("BROTHER"), Pawn ? *Pawn->GetActorLocation().ToCompactString() : TEXT("no pawn"));
	}
	return Out;
}
