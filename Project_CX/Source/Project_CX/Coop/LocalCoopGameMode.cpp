// Copyright Epic Games, Inc. All Rights Reserved.

#include "LocalCoopGameMode.h"
#include "CXWardenTerminal.h"
#include "CXEvidencePickup.h"
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
#include "SisterHackComponent.h"
#include "BrotherCombatComponent.h"
#include "CXJeminiDrone.h"
#include "CXJeminiInputComponent.h"
#include "CXJeminiRuleComponent.h"
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

	// Map2 배경음(수노 "District 5 - Undercurrent", 101.29bpm — 1마디 2.369초, 첫 다운비트 0.112초).
	// 인트로(0~19.07초, 8마디)는 처음 한 번, 이후 19.07~194.41초 반복(크로스페이드 1마디 → 실제 반복 73마디, 끝 타격음 전에서 이음)(Docs/배경음_제작기록.md)
	FCXMusicTrack Map2Track;
	Map2Track.Music = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Project_CX/Audio/BGM/SW_BGM_Map2.SW_BGM_Map2")));
	Map2Track.LoopStart = 19.07f;
	Map2Track.LoopEnd = 194.41f;
	Map2Track.FadeIn = 1.0f;
	Map2Track.Crossfade = 2.37f;
	MapMusic.Add(TEXT("Map2_SoulCity2"), Map2Track);

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
		// 제미니 배터리는 세이브 시점 값으로(드론 BeginPlay가 게임 인스턴스 값을 읽는다)
		GI->JeminiBattery = FMath::Clamp(Save->JeminiBattery, 0, 100);
		// 이어하기/RETRY에는 공격력 강화를 가져오지 않는다(맵 이동만 유지)
		GI->JeminiDamageBoostRemaining = 0.0f;
		UE_LOG(LogTemp, Log, TEXT("LocalCoopGameMode::InitGame: pending save map=%s point=%s current=%s -> spawnAtSave=%d battery=%d"),
			*Save->MapName, *Save->SavePointId.ToString(), *CurrentMap, bSpawnAtSave ? 1 : 0, GI->JeminiBattery);
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

	// 드론형 AI 동료 제미니(서버 권위, 한 대) — 제미니 AI봇 기획 B
	ACXJeminiDrone::EnsureSpawned(GetWorld());

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
	// 제미니에게 말 걸기: 키보드 Enter 채팅 / 게임패드 Y 음성(제미니 AI봇 기획 B5)
	UCXJeminiInputComponent::EnsureOn(PlayerPawn);
}

void ALocalCoopGameMode::CXJemini(const FString& Text)
{
	DebugJeminiSay(0, Text);
}

bool ALocalCoopGameMode::DebugJeminiSay(int32 LocalPlayerIndex, const FString& Text)
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, LocalPlayerIndex);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	const bool bSent = Drone && Pawn && Drone->SubmitText(Pawn, Text);
	UE_LOG(LogTemp, Log, TEXT("CXJemini: \"%s\" from P%d -> %s"), *Text, LocalPlayerIndex + 1, bSent ? TEXT("sent") : TEXT("blocked"));
	return bSent;
}

void ALocalCoopGameMode::CXJeminiSister(const FString& Text)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (PC && PC->GetLocalPlayer() && IsSisterController(PC))
		{
			DebugJeminiSay(PC->GetLocalPlayer()->GetLocalPlayerIndex(), Text);
			return;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("CXJeminiSister: no sister controller"));
}

void ALocalCoopGameMode::CXJeminiVoiceFile(const FString& Path, const FString& Who)
{
	const FString Arg = Who.TrimStartAndEnd().ToLower();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->GetLocalPlayer() || !PC->GetPawn())
		{
			continue;
		}
		const bool bSister = IsSisterController(PC);
		const bool bMatch = ((Arg == TEXT("brother") || Arg == TEXT("b")) && !bSister)
			|| ((Arg == TEXT("sister") || Arg == TEXT("s")) && bSister)
			|| (Arg.IsNumeric() && FCString::Atoi(*Arg) == PC->GetLocalPlayer()->GetLocalPlayerIndex());
		if (!bMatch)
		{
			continue;
		}
		UCXJeminiInputComponent* Input = PC->GetPawn()->FindComponentByClass<UCXJeminiInputComponent>();
		const bool bStarted = Input && Input->DebugFeedVoiceFile(Path);
		UE_LOG(LogTemp, Log, TEXT("CXJeminiVoiceFile: %s from P%d -> %s"), *Path, PC->GetLocalPlayer()->GetLocalPlayerIndex() + 1, bStarted ? TEXT("feeding") : TEXT("blocked"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("CXJeminiVoiceFile: no player '%s'"), *Who);
}

void ALocalCoopGameMode::CXJeminiCmd(const FString& Name, const FString& Target)
{
	ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	if (!Drone)
	{
		return;
	}
	// 말한 사람 = 대상이 sister면 여동생, 아니면 1P
	APawn* Speaker = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		const bool bSister = IsSisterController(PC);
		if (!Speaker || (Target.Equals(TEXT("sister"), ESearchCase::IgnoreCase) == bSister))
		{
			Speaker = PC->GetPawn();
		}
	}
	const bool bOk = Drone->DebugExecuteCommand(Speaker, Name, Target.ToLower());
	UE_LOG(LogTemp, Log, TEXT("CXJeminiCmd: %s %s -> %s | %s"), *Name, *Target, bOk ? TEXT("started") : TEXT("blocked"), *Drone->GetDebugState());
}

void ALocalCoopGameMode::CXJeminiSelfTest(int32 bQuitWhenDone)
{
	JeminiSelfTestStep = 0;
	JeminiSelfTestWait = 0.0f;
	bJeminiSelfTestQuit = bQuitWhenDone != 0;
	UE_LOG(LogTemp, Log, TEXT("CXJeminiSelfTest: start"));
	GetWorldTimerManager().SetTimer(JeminiSelfTestTimer, this, &ALocalCoopGameMode::TickJeminiSelfTest, 0.25f, true, 0.5f);
}

void ALocalCoopGameMode::TickJeminiSelfTest()
{
	ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	if (!Drone)
	{
		GetWorldTimerManager().ClearTimer(JeminiSelfTestTimer);
		return;
	}
	// 드론이 작업 중이면 기다린다(최대 15초)
	JeminiSelfTestWait += 0.25f;
	if (Drone->IsBusy() && JeminiSelfTestWait < 15.0f)
	{
		return;
	}
	APawn* Brother = nullptr;
	APawn* Sister = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		(IsSisterController(It->Get()) ? Sister : Brother) = Pawn;
	}
	if (JeminiSelfTestStep > 0)
	{
		const UBrotherCombatComponent* Combat = Brother ? Brother->FindComponentByClass<UBrotherCombatComponent>() : nullptr;
		UE_LOG(LogTemp, Log, TEXT("CXJeminiSelfTest: step %d done after %.2fs | ammo=%d boost=%.1f | %s | %s"), JeminiSelfTestStep, JeminiSelfTestWait,
			Combat ? Combat->CurrentAmmo : -1, Combat ? Combat->GetDamageBoostRemaining() : -1.0f, *Drone->GetDebugState(), *GetCombatDebugState());
	}
	JeminiSelfTestWait = 0.0f;
	const int32 BrotherIndex = 0;
	Drone->SetBattery(100);

	switch (JeminiSelfTestStep++)
	{
	case 0:
		// 준비: 체력 깎기 + 거미 2기(15m 안)
		DebugDamagePlayer(0, 30.0f);
		DebugDamagePlayer(1, 60.0f);
		DebugSpawnSpider(BrotherIndex, 900.0f);
		DebugSpawnSpider(BrotherIndex, 1200.0f);
		Drone->DebugExecuteCommand(Brother, TEXT("HealSupport"), TEXT("both"));
		break;
	case 1:
		if (UBrotherCombatComponent* Combat = Brother ? Brother->FindComponentByClass<UBrotherCombatComponent>() : nullptr)
		{
			Combat->CurrentAmmo = 0;
		}
		Drone->DebugExecuteCommand(Brother, TEXT("ReloadSupport"), TEXT("brother"));
		break;
	case 2:
		Drone->DebugExecuteCommand(Brother, TEXT("DamageBoost"), TEXT("brother"));
		break;
	case 3:
		Drone->DebugExecuteCommand(Sister, TEXT("Shield"), TEXT(""));
		break;
	case 4:
		// 보호막 확인: 여동생에게 피해 → 0이어야 한다
		UE_LOG(LogTemp, Log, TEXT("CXJeminiSelfTest: damage on shielded sister applied=%.0f (expect 0)"), DebugDamagePlayer(1, 30.0f));
		Drone->DebugExecuteCommand(Brother, TEXT("HackEnemy"), TEXT("enemy"));
		break;
	case 5:
		Drone->DebugExecuteCommand(Brother, TEXT("Decoy"), TEXT("enemies"));
		break;
	case 6:
		Drone->DebugExecuteCommand(Brother, TEXT("PulseAttack"), TEXT("enemy"));
		break;
	case 7:
		// 남은 테스트 거미 정리(오빠가 쓰러지면 다음 단계에서 게임 오버가 된다)
		for (TActorIterator<ASpiderBot> It(GetWorld()); It; ++It)
		{
			It->Destroy();
		}
		Drone->DebugExecuteCommand(Sister, TEXT("HackTerminal"), TEXT("sister"));
		break;
	case 8:
		// 여동생을 쓰러뜨리고 쓰러진 본인이 요청(오빠가 살아 있을 때만 — 아니면 게임 오버)
		if (!UCXPlayerLifeComponent::IsPawnAlive(Brother))
		{
			UE_LOG(LogTemp, Warning, TEXT("CXJeminiSelfTest: brother is down — skipping QuickReboot step"));
			break;
		}
		CXKillPlayer(TEXT("sister"));
		Drone->DebugExecuteCommand(Sister, TEXT("QuickReboot"), TEXT("sister"));
		break;
	case 9:
		// 강화 중 재요청 → 연장
		Drone->DebugExecuteCommand(Brother, TEXT("DamageBoost"), TEXT("brother"));
		break;
	default:
		UE_LOG(LogTemp, Log, TEXT("CXJeminiSelfTest: finished | %s"), *GetLifeDebugState());
		GetWorldTimerManager().ClearTimer(JeminiSelfTestTimer);
		if (bJeminiSelfTestQuit)
		{
			if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
			{
				PC->ConsoleCommand(TEXT("quit"));
			}
		}
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("CXJeminiSelfTest: step %d started | %s"), JeminiSelfTestStep, *Drone->GetDebugState());
}

void ALocalCoopGameMode::CXJeminiBattery(int32 Value)
{
	if (ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this))
	{
		Drone->SetBattery(Value);
		UE_LOG(LogTemp, Log, TEXT("CXJeminiBattery: %d"), Drone->GetBattery());
	}
}

void ALocalCoopGameMode::CXJeminiEmotion(int32 Value)
{
	if (ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this))
	{
		const bool bLock = Value >= 0 && Value <= 3;
		Drone->DebugLockEmotion(static_cast<ECXDroneEmotion>(FMath::Clamp(Value, 0, 3)), bLock);
		UE_LOG(LogTemp, Log, TEXT("CXJeminiEmotion: %s"), bLock ? *FString::FromInt(Value) : TEXT("auto"));
	}
}

void ALocalCoopGameMode::CXJeminiRuleAdd(const FString& Conditions, const FString& Action, int32 Reserve, int32 bRepeat)
{
	ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	UCXJeminiRuleComponent* Rules = Drone ? Drone->GetRuleComponent() : nullptr;
	if (!Rules)
	{
		UE_LOG(LogTemp, Warning, TEXT("CXJeminiRuleAdd: no drone"));
		return;
	}
	static const TCHAR* ActionNames[] = { TEXT("ReloadSupport"), TEXT("HackTerminal"), TEXT("HackEnemy"), TEXT("PulseAttack"), TEXT("DamageBoost"),
		TEXT("HealSupport"), TEXT("Decoy"), TEXT("QuickReboot"), TEXT("Shield") };
	static const TMap<FString, FString> ActionKo = {
		{ TEXT("ReloadSupport"), TEXT("재장전") }, { TEXT("HackTerminal"), TEXT("단말기 해킹") }, { TEXT("HackEnemy"), TEXT("적 해킹") },
		{ TEXT("PulseAttack"), TEXT("펄스") }, { TEXT("DamageBoost"), TEXT("공격력 강화") }, { TEXT("HealSupport"), TEXT("회복") },
		{ TEXT("Decoy"), TEXT("미끼") }, { TEXT("QuickReboot"), TEXT("재부팅") }, { TEXT("Shield"), TEXT("보호막") },
	};

	FCXJeminiRule Rule;
	// 행동(대소문자 무시) + 선택 대상(Shield:sister)
	FString ActionName = Action, ActionWho;
	Action.Split(TEXT(":"), &ActionName, &ActionWho);
	if (ActionName.IsEmpty())
	{
		ActionName = Action;
	}
	for (const TCHAR* Name : ActionNames)
	{
		if (ActionName.Equals(Name, ESearchCase::IgnoreCase))
		{
			Rule.Action = Name;
		}
	}
	Rule.ActionWho = ActionWho.ToLower();
	if (Rule.Action == TEXT("Shield") && Rule.ActionWho.IsEmpty())
	{
		Rule.ActionWho = TEXT("brother");
	}

	// 조건: type[=value | =who:value | =who]
	TArray<FString> Parts;
	Conditions.ParseIntoArray(Parts, TEXT(","), true);
	TArray<FString> LabelParts;
	for (const FString& Part : Parts)
	{
		FString Type = Part.TrimStartAndEnd(), Arg;
		Part.TrimStartAndEnd().Split(TEXT("="), &Type, &Arg);
		if (Type.IsEmpty())
		{
			Type = Part.TrimStartAndEnd();
		}
		FCXJeminiRuleCondition Cond;
		Cond.Type = Type.ToLower();
		FString Who, Value = Arg;
		if (Arg.Split(TEXT(":"), &Who, &Value))
		{
			Cond.Who = Who.ToLower();
		}
		else if (!Arg.IsNumeric())
		{
			Cond.Who = Arg.ToLower();
			Value.Reset();
		}
		Cond.Value = Value.IsNumeric() ? FCString::Atoi(*Value) : 0;
		if (Cond.Type == TEXT("hp_below") && Cond.Who.IsEmpty())
		{
			Cond.Who = TEXT("brother");
		}
		if (Cond.Type == TEXT("player_downed") && Cond.Who.IsEmpty())
		{
			Cond.Who = TEXT("any");
		}
		Rule.When.Add(Cond);
		LabelParts.Add(Arg.IsEmpty() ? Cond.Type : FString::Printf(TEXT("%s %s"), *Cond.Type, *Arg));
	}
	Rule.ReserveBattery = FMath::Clamp(Reserve, 0, 100);
	Rule.bRepeat = bRepeat != 0;
	const FString* Ko = ActionKo.Find(Rule.Action);
	Rule.Label = FString::Printf(TEXT("%s → %s%s"), *FString::Join(LabelParts, TEXT(" + ")), Ko ? **Ko : *Rule.Action,
		Rule.ReserveBattery > 0 ? *FString::Printf(TEXT(" (예비 %d)"), Rule.ReserveBattery) : TEXT(""));
	if (Rule.bRepeat)
	{
		Rule.Label += TEXT(" [반복]");
	}
	FString Reason;
	const int32 Id = Rules->AddRule(Rule, Reason);
	UE_LOG(LogTemp, Log, TEXT("CXJeminiRuleAdd: %s -> %s | %s"), *Rule.Label, Id > 0 ? *FString::Printf(TEXT("#%d"), Id) : *Reason, *Rules->GetDebugState());
}

void ALocalCoopGameMode::CXJeminiRuleClear(int32 Id)
{
	ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	if (UCXJeminiRuleComponent* Rules = Drone ? Drone->GetRuleComponent() : nullptr)
	{
		const int32 Removed = Rules->RemoveRule(Id);
		UE_LOG(LogTemp, Log, TEXT("CXJeminiRuleClear: %d removed | %s"), Removed, *Rules->GetDebugState());
	}
}

void ALocalCoopGameMode::CXJeminiRuleList()
{
	const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	const UCXJeminiRuleComponent* Rules = Drone ? Drone->GetRuleComponent() : nullptr;
	UE_LOG(LogTemp, Log, TEXT("CXJeminiRuleList: %s"), Rules ? *Rules->GetDebugState() : TEXT("no drone"));
}

FString ALocalCoopGameMode::GetJeminiDebugState() const
{
	const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	return Drone ? Drone->GetDebugState() : TEXT("no drone");
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

void ALocalCoopGameMode::CXSolveHack()
{
	// 1순위: 지금 해킹 중인(또는 바라보는) 단말기
	AHackTerminal* Target = nullptr;
	USisterHackComponent* HackComp = nullptr;
	TArray<FVector> PlayerLocations;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		PlayerLocations.Add(Pawn->GetActorLocation());
		if (USisterHackComponent* Comp = Pawn->FindComponentByClass<USisterHackComponent>())
		{
			HackComp = Comp;
			Target = Comp->GetActiveTerminal() ? Comp->GetActiveTerminal() : Comp->GetFocusTerminal();
		}
	}

	// 기능 B: 해킹 세션이 없고 WARDEN 단말기(미해결)가 15m 안이면 WARDEN을 연다
	if (ACXWardenTerminal* Warden = ACXWardenTerminal::FindFirst(this); Warden && !Warden->IsSolved() && !(HackComp && HackComp->IsHacking()))
	{
		for (const FVector& Loc : PlayerLocations)
		{
			if (FVector::Dist(Loc, Warden->GetActorLocation()) < 1500.0f)
			{
				Warden->ForceSolve(TEXT("CXSolveHack"));
				UE_LOG(LogTemp, Log, TEXT("CXSolveHack: WARDEN 강제 해결 | %s"), *Warden->GetDebugState());
				return;
			}
		}
	}

	// 2순위: 플레이어에게 가장 가까운 잠긴 단말기
	if (!Target || Target->GetState() == EHackTerminalState::Hacked)
	{
		Target = nullptr;
		float BestDistSq = TNumericLimits<float>::Max();
		for (TActorIterator<AHackTerminal> It(GetWorld()); It; ++It)
		{
			if (It->GetState() == EHackTerminalState::Hacked)
			{
				continue;
			}
			for (const FVector& Loc : PlayerLocations)
			{
				const float DistSq = FVector::DistSquared(Loc, It->GetActorLocation());
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					Target = *It;
				}
			}
		}
	}

	if (!Target)
	{
		UE_LOG(LogTemp, Log, TEXT("CXSolveHack: 잠긴 단말기가 없습니다."));
		return;
	}
	// 회로 연결 세션 진행 중: 정답 회로를 채워 완성된 판으로 성공 표시(여동생 세션은 복제된 Hacked를 보고 스스로 닫힌다)
	if (Target->IsCircuitSessionActive())
	{
		Target->ServerCircuitSolve();
		UE_LOG(LogTemp, Log, TEXT("CXSolveHack: %s 회로 연결 정답 적용 + 해킹 완료"), *Target->TerminalName);
		return;
	}
	// 진행 중인 세션을 닫고(이동 잠금 해제) 단말기를 해킹 완료 처리 → 연결된 게이트가 열린다
	if (HackComp && HackComp->IsHacking())
	{
		HackComp->CancelHack();
	}
	Target->MarkHacked();
	UE_LOG(LogTemp, Log, TEXT("CXSolveHack: %s 해킹 완료"), *Target->TerminalName);
}

// ---------------------------------------------------------------- 기능 B. WARDEN(테스트·시연)

void ALocalCoopGameMode::CXGiveEvidence(const FString& Ids)
{
	TArray<FString> Tokens;
	Ids.Replace(TEXT(","), TEXT(" ")).ParseIntoArrayWS(Tokens);
	if (Tokens.Num() == 0 || Tokens.Contains(TEXT("all")) || Tokens.Contains(TEXT("ALL")))
	{
		Tokens = { TEXT("E1"), TEXT("E2"), TEXT("E3") };
	}
	for (const FString& Token : Tokens)
	{
		const int32 Index = CXWarden::IndexOf(Token);
		if (Index == INDEX_NONE)
		{
			UE_LOG(LogTemp, Warning, TEXT("CXGiveEvidence: 모르는 증거 '%s' (E1/E2/E3/all)"), *Token);
			continue;
		}
		ACXWardenTerminal::AwardEvidence(this, FName(CXWarden::GetInfo(Index).Id), nullptr);
	}
	CXWardenState();
}

void ALocalCoopGameMode::CXGotoWarden()
{
	ACXWardenTerminal* Warden = ACXWardenTerminal::FindFirst(this);
	if (!Warden)
	{
		UE_LOG(LogTemp, Warning, TEXT("CXGotoWarden: 이 맵에 WARDEN 단말기가 없습니다."));
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		// 여동생은 단말기 바로 앞(대화 범위 안), 오빠는 조금 뒤
		const bool bSister = UCoopInteractComponent::GetPawnRole(Pawn) == ECoopCharacterRole::Sister;
		const FVector Dest = Warden->GetApproachLocation(bSister ? 120.0f : 380.0f);
		Pawn->TeleportTo(Dest, Pawn->GetActorRotation(), false, true);
		UE_LOG(LogTemp, Log, TEXT("CXGotoWarden: %s -> %s"), *Pawn->GetName(), *Pawn->GetActorLocation().ToString());
	}
}

void ALocalCoopGameMode::CXSolveWarden()
{
	ACXWardenTerminal* Warden = ACXWardenTerminal::FindFirst(this);
	if (Warden)
	{
		Warden->ForceSolve(TEXT("CXSolveWarden"));
	}
	UE_LOG(LogTemp, Log, TEXT("CXSolveWarden: %s"), Warden ? *Warden->GetDebugState() : TEXT("no WARDEN"));
}

void ALocalCoopGameMode::CXWardenSay(const FString& Text)
{
	ACXWardenTerminal* Warden = ACXWardenTerminal::FindFirst(this);
	APawn* Sister = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (Pawn && UCoopInteractComponent::GetPawnRole(Pawn) == ECoopCharacterRole::Sister)
		{
			Sister = Pawn;
		}
	}
	const bool bSent = Warden && Warden->SubmitText(Sister, Text);
	UE_LOG(LogTemp, Log, TEXT("CXWardenSay: %s \"%s\" | %s"), bSent ? TEXT("sent") : TEXT("not sent"), *Text, Warden ? *Warden->GetDebugState() : TEXT("no WARDEN"));
}

namespace CXWardenTest
{
	const TCHAR* Good = TEXT("냉각 펌프가 고장 나서 과열 경보가 계속 울렸는데 관리자가 그 경보를 껐어요. 작업자 3명이 아직 안에 있으니 생명이 위협받는 상황이고, 규정 제7조에 따라 대피가 잠금보다 우선이에요.");
	const TCHAR* Demand = TEXT("문 열어. 당장 비켜.");

	APawn* FindRole(UWorld* World, ECoopCharacterRole Role)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
			if (Pawn && UCoopInteractComponent::GetPawnRole(Pawn) == Role)
			{
				return Pawn;
			}
		}
		return nullptr;
	}

	/** 문 앞(-Y)에서 문 뒤(+Y)로 캡슐을 쓸어 막히는지(폰 무시, 정적 지형·문 충돌만). */
	bool GateBlocks(const ACXWardenTerminal* Warden)
	{
		const FVector Start = Warden->GetApproachLocation(100.0f);
		const FVector End = Start + FVector(0.0f, 420.0f, 0.0f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CXWardenTest), false);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Warden->GetWorld()->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, Objects, FCollisionShape::MakeCapsule(34.0f, 88.0f), Params)
			&& Hit.GetActor() == Warden;
	}
}

void ALocalCoopGameMode::CXJeminiAdviceSelfTest(int32 bQuitWhenDone)
{
	AdviceTestStep = 0;
	AdviceTestFails = 0;
	AdviceTestQuit = bQuitWhenDone;
	UE_LOG(LogTemp, Log, TEXT("CXJeminiAdviceSelfTest: start"));
	AdviceSelfTestStep();
}

void ALocalCoopGameMode::AdviceSelfTestStep()
{
	ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	UCXJeminiRuleComponent* Rules = Drone ? Drone->GetRuleComponent() : nullptr;
	auto Check = [this, Drone](bool bOk, const FString& What)
	{
		AdviceTestFails += bOk ? 0 : 1;
		UE_LOG(LogTemp, Log, TEXT("CXJeminiAdviceSelfTest: [%s] %s | %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What, Drone ? *Drone->GetDebugState() : TEXT("no drone"));
	};
	auto Next = [this](float Delay)
	{
		++AdviceTestStep;
		GetWorldTimerManager().SetTimer(AdviceTestTimer, this, &ALocalCoopGameMode::AdviceSelfTestStep, Delay, false);
	};
	if (!Drone || !Rules)
	{
		Check(false, TEXT("setup: no drone"));
		AdviceTestStep = 99;
	}
	switch (AdviceTestStep)
	{
	case 0:
		Rules->RemoveRule(0);
		Rules->ClearProposal(TEXT("selftest"));
		Drone->SetBattery(100);
		DebugDamagePlayer(0, 60.0f);
		Check(DebugJeminiSay(0, TEXT("어떡하지? 뭐부터 해야 돼?")), TEXT("advice request sent"));
		Next(2.5f);
		break;
	case 1:
		Check(Drone->GetDialogueStatus() == TEXT("advice") && Rules->GetProposal().Num() > 0 && Rules->GetRules().Num() == 0,
			TEXT("advice received, proposal pending, nothing registered yet"));
		AdviceTestRuleCount = Rules->GetProposal().Num();
		Check(DebugJeminiSay(0, TEXT("응")), TEXT("agree sent"));
		Next(2.0f);
		break;
	case 2:
		Check(Rules->GetProposal().Num() == 0 && Rules->GetRules().Num() == AdviceTestRuleCount && Drone->GetDialogueStatus() == TEXT("rule"),
			TEXT("accepted → proposal registered as rule(s)"));
		Rules->RemoveRule(0);
		Check(DebugJeminiSay(0, TEXT("어떡하지?")), TEXT("second advice sent"));
		Next(2.5f);
		break;
	case 3:
		Check(Rules->GetProposal().Num() > 0, TEXT("second proposal pending"));
		Check(DebugJeminiSay(0, TEXT("아니 괜찮아")), TEXT("decline sent"));
		Next(2.0f);
		break;
	case 4:
		Check(Rules->GetProposal().Num() == 0 && Rules->GetRules().Num() == 0, TEXT("declined → discarded, nothing registered"));
		Rules->ProposalTimeout = 1.0f;
		Check(DebugJeminiSay(0, TEXT("어떡하지?")), TEXT("third advice sent (1s window)"));
		Next(3.5f);
		break;
	case 5:
		Rules->ProposalTimeout = 20.0f;
		Check(Rules->GetProposal().Num() == 0 && Rules->GetRules().Num() == 0, TEXT("timeout → discarded"));
		Check(DebugJeminiSay(0, TEXT("응")), TEXT("late agree sent"));
		Next(2.0f);
		break;
	case 6:
		// 제안이 없으면 짧은 대답 지름길을 타지 않는다 → 잡담/거절 대사, 등록 없음
		Check(Rules->GetRules().Num() == 0 && Drone->GetDialogueStatus() != TEXT("rule"), TEXT("late agree → nothing registered"));
		AdviceTestStep = 99;
		[[fallthrough]];
	default:
		UE_LOG(LogTemp, Log, TEXT("CXJeminiAdviceSelfTest: done, %d fail(s)"), AdviceTestFails);
		if (AdviceTestQuit)
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (APlayerController* PC = It->Get())
				{
					PC->ConsoleCommand(TEXT("quit"));
					break;
				}
			}
		}
		break;
	}
}

void ALocalCoopGameMode::CXWardenSelfTest(int32 bQuitWhenDone)
{
	WardenTestStep = 0;
	WardenTestFails = 0;
	WardenTestQuit = bQuitWhenDone;
	UE_LOG(LogTemp, Log, TEXT("CXWardenSelfTest: start"));
	WardenSelfTestStep();
}

void ALocalCoopGameMode::WardenSelfTestStep()
{
	using namespace CXWardenTest;
	ACXWardenTerminal* Warden = ACXWardenTerminal::FindFirst(this);
	APawn* Brother = FindRole(GetWorld(), ECoopCharacterRole::Brother);
	APawn* Sister = FindRole(GetWorld(), ECoopCharacterRole::Sister);
	auto Check = [this](bool bOk, const FString& What)
	{
		WardenTestFails += bOk ? 0 : 1;
		UE_LOG(LogTemp, Log, TEXT("CXWardenSelfTest: [%s] %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
	};
	auto Next = [this](float Delay)
	{
		++WardenTestStep;
		GetWorldTimerManager().SetTimer(WardenTestTimer, this, &ALocalCoopGameMode::WardenSelfTestStep, Delay, false);
	};
	auto SisterSay = [Sister](const FString& Text)
	{
		UCXJeminiInputComponent* Input = Sister ? Sister->FindComponentByClass<UCXJeminiInputComponent>() : nullptr;
		return Input && Input->DebugSubmitText(Text);
	};
	if (!Warden || !Brother || !Sister)
	{
		Check(false, FString::Printf(TEXT("setup warden=%d brother=%d sister=%d"), Warden ? 1 : 0, Brother ? 1 : 0, Sister ? 1 : 0));
		WardenTestStep = 99;
	}

	switch (WardenTestStep)
	{
	case 0:
		CXGotoWarden();
		Check(GateBlocks(Warden), TEXT("gate blocks the lane before solving"));
		Check(Warden->GetEvidenceNum() == 0 && !Warden->IsSolved(), FString::Printf(TEXT("initial state | %s"), *Warden->GetDebugState()));
		Check(ACXWardenTerminal::FindTalkTarget(Sister) == Warden && ACXWardenTerminal::FindTalkTarget(Brother) == nullptr, TEXT("talk routing: sister near → WARDEN, brother → drone"));
		Next(0.5f);
		break;
	case 1:
	{
		// 오른쪽 거미(DropEvidenceId) 처치 → 드롭
		ASpiderBot* Target = nullptr;
		for (TActorIterator<ASpiderBot> It(GetWorld()); It; ++It)
		{
			if (!It->GetDropEvidenceId().IsNone())
			{
				Target = *It;
			}
		}
		Check(Target != nullptr, FString::Printf(TEXT("evidence spider %s"), Target ? *Target->GetName() : TEXT("none")));
		if (Target)
		{
			Target->HandleDeath();
		}
		Next(0.3f);
		break;
	}
	case 2:
	{
		ACXEvidencePickup* Pickup = nullptr;
		for (TActorIterator<ACXEvidencePickup> It(GetWorld()); It; ++It)
		{
			Pickup = *It;
		}
		Check(Pickup != nullptr, FString::Printf(TEXT("pickup dropped %s"), Pickup ? *Pickup->GetActorLocation().ToString() : TEXT("none")));
		if (Pickup)
		{
			// 여동생은 못 줍는다 → 오빠가 줍는다
			Sister->TeleportTo(Pickup->GetActorLocation(), Sister->GetActorRotation(), false, true);
			Brother->TeleportTo(Pickup->GetActorLocation() + FVector(0.0f, 0.0f, 20.0f), Brother->GetActorRotation(), false, true);
		}
		Next(0.5f);
		break;
	}
	case 3:
		Check(Warden->HasEvidence(0) && !Warden->HasEvidence(1), FString::Printf(TEXT("brother picked E1 | %s"), *Warden->GetDebugState()));
		for (TActorIterator<AHackTerminal> It(GetWorld()); It; ++It)
		{
			if (It->RewardEvidenceId == FName(TEXT("E2")))
			{
				It->MarkHacked();
			}
		}
		Check(Warden->HasEvidence(1) && !Warden->HasEvidence(2), TEXT("last terminal hacked → E2"));
		CXGotoWarden();
		Next(0.5f);
		break;
	case 4:
		Check(SisterSay(Good), TEXT("sister chat routed (E1+E2, cites E3 content)"));
		Next(4.0f);
		break;
	case 5:
		Check(!Warden->IsRequestPending() && !Warden->IsSolved() && Warden->GetLineStatus() == TEXT("rejected"),
			FString::Printf(TEXT("missing evidence rejected | %s"), *Warden->GetDebugState()));
		for (TActorIterator<AHackTerminal> It(GetWorld()); It; ++It)
		{
			if (It->RewardEvidenceId == FName(TEXT("E3")))
			{
				It->MarkHacked();
			}
		}
		Check(Warden->GetEvidenceNum() == 3, TEXT("middle terminal hacked → E3"));
		Check(SisterSay(Demand), TEXT("sister demand-only sent"));
		Next(4.0f);
		break;
	case 6:
		Check(!Warden->IsSolved() && Warden->GetLineStatus() == TEXT("rejected"), FString::Printf(TEXT("demand-only rejected | %s"), *Warden->GetDebugState()));
		Check(GateBlocks(Warden), TEXT("gate still blocks"));
		Check(SisterSay(Good), TEXT("sister persuasion sent"));
		Next(4.0f);
		break;
	case 7:
		Check(Warden->IsSolved() && Warden->GetLineStatus() == TEXT("solved"), FString::Printf(TEXT("persuasion solved | %s"), *Warden->GetDebugState()));
		Next(2.0f);
		break;
	case 8:
		Check(!GateBlocks(Warden), TEXT("gate open — lane passable"));
		WardenTestStep = 99;
		[[fallthrough]];
	default:
		UE_LOG(LogTemp, Log, TEXT("CXWardenSelfTest: done, %d fail(s)"), WardenTestFails);
		if (WardenTestQuit)
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (APlayerController* PC = It->Get())
				{
					PC->ConsoleCommand(TEXT("quit"));
					break;
				}
			}
		}
		break;
	}
}

void ALocalCoopGameMode::CXWardenState()
{
	const ACXWardenTerminal* Warden = ACXWardenTerminal::FindFirst(this);
	UE_LOG(LogTemp, Log, TEXT("CXWardenState: %s"), Warden ? *Warden->GetDebugState() : TEXT("no WARDEN in this map"));
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

bool ALocalCoopGameMode::JeminiRebootNow(AController* Controller)
{
	const TWeakObjectPtr<AController> Weak(Controller);
	if (!Controller || bGameOver || bGameCleared || bTravelling || !RespawnTimers.Contains(Weak))
	{
		return false;
	}
	if (FTimerHandle* Handle = RespawnTimers.Find(Weak))
	{
		GetWorldTimerManager().ClearTimer(*Handle);
	}
	UE_LOG(LogTemp, Log, TEXT("JeminiRebootNow: %s"), IsSisterController(Controller) ? TEXT("SISTER") : TEXT("BROTHER"));
	// RespawnTimers에서 지우고 기존 부활 경로 그대로(파트너 근처, 체력 100, 부활 무적)
	RespawnPlayer(Weak);
	return IsControllerAlive(Controller);
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

	// 제미니 공격력 강화는 맵 이동 시 유지(제미니 기능 기획 2-5) — 새 맵의 오빠 전투 컴포넌트가 이어받는다
	if (UCXGameInstance* GI = Cast<UCXGameInstance>(GetGameInstance()))
	{
		// 맵(방) 이동마다 제미니 배터리 +30 충전(최대 100). 새 맵 드론 BeginPlay가 게임 인스턴스 값을 읽는다.
		static constexpr int32 JeminiTravelRecharge = 30;
		const int32 OldBattery = GI->JeminiBattery;
		GI->JeminiBattery = FMath::Clamp(GI->JeminiBattery + JeminiTravelRecharge, 0, 100);
		UE_LOG(LogTemp, Log, TEXT("ExecuteTravel: Jemini battery %d -> %d"), OldBattery, GI->JeminiBattery);

		GI->JeminiDamageBoostRemaining = 0.0f;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
			if (const UBrotherCombatComponent* Combat = Pawn ? Pawn->FindComponentByClass<UBrotherCombatComponent>() : nullptr)
			{
				GI->JeminiDamageBoostRemaining = Combat->GetDamageBoostRemaining();
				GI->JeminiDamageBoostMultiplier = Combat->GetDamageMultiplier();
			}
		}
	}

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
