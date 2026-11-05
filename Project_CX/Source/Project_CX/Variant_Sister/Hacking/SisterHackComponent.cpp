// Copyright Epic Games, Inc. All Rights Reserved.

#include "SisterHackComponent.h"
#include "CXHackTarget.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "CXSfx.h"
#include "CXPlayerLifeComponent.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace HackSfx
{
	USoundBase* Find(const TCHAR* Name)
	{
		return CXSfx::Find<USoundBase>(*FString::Printf(TEXT("/Game/Project_CX/Audio/SFX/Sister/%s.%s"), Name, Name));
	}
}

USisterHackComponent::USisterHackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 적 해킹 결과를 클라이언트 → 서버로 보내는 RPC(Server_ApplyEnemyHack)를 쓰려면 복제 컴포넌트여야 한다
	SetIsReplicatedByDefault(true);

	HackLoopSound = HackSfx::Find(TEXT("SW_Hack_Loop"));
	HackStartSound = HackSfx::Find(TEXT("SW_Hack_Start"));
	HackCancelSound = HackSfx::Find(TEXT("SW_Hack_Cancel"));
	CorrectSound = HackSfx::Find(TEXT("SW_Hack_Input"));
	RoundClearSound = HackSfx::Find(TEXT("SW_Hack_RoundClear"));
	DeniedSound = HackSfx::Find(TEXT("SW_Hack_Denied"));
	GrantedSound = HackSfx::Find(TEXT("SW_Hack_Granted"));
	DigitTickSound = HackSfx::Find(TEXT("SW_Hack_DigitTick"));
	DetectSound = HackSfx::Find(TEXT("SW_Hack_Detect"));
}

void USisterHackComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildInputAssets();
}

void USisterHackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HackLoopAudio)
	{
		HackLoopAudio->Stop();
	}
	Super::EndPlay(EndPlayReason);
}

void USisterHackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bInputBound)
	{
		TryBindInput();
	}

	// 적 해킹 쿨타임 / 제한 시간 / 주파수 동조 커서 — 월드 Tick 기준이라 일시정지(ESC 메뉴) 중에는 함께 멈춘다
	if (EnemyCooldownRemaining > 0.0f && !ActiveEnemy)
	{
		EnemyCooldownRemaining = FMath::Max(0.0f, EnemyCooldownRemaining - DeltaTime);
	}
	if (ActiveEnemy)
	{
		TickEnemyHack(DeltaTime);
	}
	else if (IsFreqLockSession())
	{
		TickFreqLock(DeltaTime);
	}

	FocusTimer -= DeltaTime;
	if (FocusTimer <= 0.0f)
	{
		FocusTimer = 0.2f;
		UpdateFocus();
	}
}

// ---------------------------------------------------------------- 입력

void USisterHackComponent::BuildInputAssets()
{
	// 에셋 없이 런타임에 입력을 만든다 — 2P(게임패드) 기준 D-pad/X/B, 키보드로도 테스트 가능하게 함께 매핑
	auto MakeAction = [this](const TCHAR* Name)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Boolean;
		return Action;
	};
	InteractAction = MakeAction(TEXT("IA_Hack_Interact"));
	CancelAction = MakeAction(TEXT("IA_Hack_Cancel"));
	UpAction = MakeAction(TEXT("IA_Hack_Up"));
	DownAction = MakeAction(TEXT("IA_Hack_Down"));
	LeftAction = MakeAction(TEXT("IA_Hack_Left"));
	RightAction = MakeAction(TEXT("IA_Hack_Right"));

	HackMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Hack"));
	// 해킹 시작/제출(F / X)은 공용 상호작용 키 → UCoopInteractComponent가 받아 Interact()로 넘긴다(같은 키 이중 바인딩 방지).
	// InteractAction은 매핑 없이 남겨 둔다(키 매핑이 없으므로 트리거되지 않음).
	HackMappingContext->MapKey(CancelAction, EKeys::Gamepad_FaceButton_Right);
	// 키보드 해킹 취소는 Backspace — Escape는 ESC(일시정지) 메뉴 전용(설정 메뉴 기획 4.4 키 충돌)
	HackMappingContext->MapKey(CancelAction, EKeys::BackSpace);
	HackMappingContext->MapKey(UpAction, EKeys::Gamepad_DPad_Up);
	HackMappingContext->MapKey(UpAction, EKeys::Up);
	HackMappingContext->MapKey(DownAction, EKeys::Gamepad_DPad_Down);
	HackMappingContext->MapKey(DownAction, EKeys::Down);
	HackMappingContext->MapKey(LeftAction, EKeys::Gamepad_DPad_Left);
	HackMappingContext->MapKey(LeftAction, EKeys::Left);
	HackMappingContext->MapKey(RightAction, EKeys::Gamepad_DPad_Right);
	HackMappingContext->MapKey(RightAction, EKeys::Right);
}

void USisterHackComponent::TryBindInput()
{
	// 폰이 빙의되고 InputComponent가 생긴 뒤에야 바인딩할 수 있으므로 성공할 때까지 매 틱 재시도한다(BrotherCombatComponent와 같은 방식)
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UEnhancedInputComponent* Input = Pawn ? Cast<UEnhancedInputComponent>(Pawn->InputComponent) : nullptr;
	if (!PC || !Input || !PC->GetLocalPlayer())
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(HackMappingContext, 10);
	}

	Input->BindAction(CancelAction, ETriggerEvent::Started, this, &USisterHackComponent::CancelHack);
	Input->BindAction(UpAction, ETriggerEvent::Started, this, &USisterHackComponent::OnInputUp);
	Input->BindAction(DownAction, ETriggerEvent::Started, this, &USisterHackComponent::OnInputDown);
	Input->BindAction(LeftAction, ETriggerEvent::Started, this, &USisterHackComponent::OnInputLeft);
	Input->BindAction(RightAction, ETriggerEvent::Started, this, &USisterHackComponent::OnInputRight);
	bInputBound = true;
}

// ---------------------------------------------------------------- 탐지

void USisterHackComponent::UpdateFocus()
{
	if (ActiveEnemy)
	{
		// 적 해킹 중에는 단말기 포커스 없음(TERMINAL ONLINE 표시·감지음 억제)
		FocusTerminal = nullptr;
		return;
	}
	if (ActiveTerminal)
	{
		FocusTerminal = ActiveTerminal;
		LastDetectedTerminal = ActiveTerminal;
		return;
	}

	const AActor* Owner = GetOwner();
	AHackTerminal* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<AHackTerminal> It(GetWorld()); It; ++It)
	{
		AHackTerminal* Terminal = *It;
		if (!Terminal->CanBeHacked())
		{
			continue;
		}
		const float Dist = FVector::Dist(Owner->GetActorLocation(), Terminal->GetActorLocation());
		if (Dist <= FMath::Min(MaxDetectRange, Terminal->InteractRange) && Dist < BestDist)
		{
			Best = Terminal;
			BestDist = Dist;
		}
	}
	FocusTerminal = Best;

	// 단말기 감지(TERMINAL ONLINE) — 범위에 새로 들어온 순간 1회. 취소 직후 같은 단말기를 다시 잡을 때는 울리지 않는다.
	if (Best && Best != LastDetectedTerminal.Get())
	{
		PlayHackSound(DetectSound);
	}
	LastDetectedTerminal = Best;
}

// ---------------------------------------------------------------- 세션

bool USisterHackComponent::WantsInteract()
{
	if (IsHacking())
	{
		return true;
	}
	UpdateFocus();
	return FocusTerminal != nullptr;
}

void USisterHackComponent::Interact()
{
	if (ActiveEnemy)
	{
		// 적 해킹(화살표)은 D-pad만 쓴다 — F/X는 세션 중이라 다른 상호작용으로 새지 않게 여기서 삼킨다
		return;
	}
	if (ActiveTerminal)
	{
		// 해킹 중 X: 비밀번호 = 제출, 메모리 패턴 = 커서 노드 선택, 주파수 동조 = 고정(확인)
		if (ActiveTerminal->PuzzleType == EHackPuzzleType::Passcode)
		{
			SubmitPasscode();
		}
		else if (ActiveTerminal->PuzzleType == EHackPuzzleType::MemoryPattern)
		{
			PressMemoryNode(MemoryCursor);
		}
		else if (ActiveTerminal->PuzzleType == EHackPuzzleType::FrequencyLock)
		{
			ConfirmFreqLock();
		}
		return;
	}

	UpdateFocus();
	if (!FocusTerminal || !FocusTerminal->CanBeHacked())
	{
		return;
	}

	ActiveTerminal = FocusTerminal;
	ActiveTerminal->SetInProgress(true);
	SetMovementLocked(true);
	SetFeedback(EHackFeedback::None);
	PlayHackSound(HackStartSound);
	StartHackLoop();

	if (ActiveTerminal->PuzzleType == EHackPuzzleType::FrequencyLock)
	{
		// 숫자(구간 폭·속도·제한 시간)는 단말기 설정 — 기본은 원래 적 해킹 값에 제한 시간 없음
		FreqLock.Start(ActiveTerminal->FreqZoneWidths, ActiveTerminal->FreqCursorSpeeds, ActiveTerminal->FreqTimeLimit, ActiveTerminal->FreqMissLockout);
	}
	else if (ActiveTerminal->PuzzleType == EHackPuzzleType::MemoryPattern)
	{
		// 패턴 생성·판정은 서버(단말기)가 한다 — 커서는 가운데(5)에서 시작
		MemoryCursor = 4;
		MemoryLastPressed = INDEX_NONE;
		MemoryLocalCorrect = 0;
		SendMemorySession(true);
	}
	else
	{
		PasscodeInput.Init(0, 4);
		PasscodeCursor = 0;
	}
}

void USisterHackComponent::CancelHack()
{
	if (ActiveEnemy)
	{
		// 결과가 이미 났으면(표시만 남은 상태) 조용히 닫고, 진행 중이면 취소음 — 어느 쪽이든 쿨타임 시작
		if (!bEnemyResolved)
		{
			PlayHackSound(HackCancelSound);
		}
		EndEnemySession();
		return;
	}
	if (!ActiveTerminal || ActiveTerminal->GetState() == EHackTerminalState::Hacked)
	{
		return;
	}
	if (ActiveTerminal->PuzzleType == EHackPuzzleType::MemoryPattern)
	{
		SendMemorySession(false);
	}
	ActiveTerminal->SetInProgress(false);
	PlayHackSound(HackCancelSound);
	EndSession();
}

AActor* USisterHackComponent::FindEnemyHackTarget() const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !GetWorld())
	{
		return nullptr;
	}
	AActor* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const ICXHackTarget* Target = Cast<ICXHackTarget>(*It);
		if (!Target || !Target->IsHackTargetAvailable())
		{
			continue;
		}
		const float Dist = FVector::Dist(Owner->GetActorLocation(), It->GetActorLocation());
		if (Dist <= Target->GetHackInteractRange() && Dist < BestDist)
		{
			Best = *It;
			BestDist = Dist;
		}
	}
	return Best;
}

bool USisterHackComponent::TryHackEnemy()
{
	// 다른 해킹 중이거나 쿨타임이면 없음(HUD 마커가 쿨타임을 보여 준다)
	if (IsHacking() || EnemyCooldownRemaining > 0.0f)
	{
		return false;
	}
	AActor* TargetActor = FindEnemyHackTarget();
	if (!TargetActor)
	{
		return false;
	}
	StartEnemyHack(TargetActor);
	return true;
}

// ---------------------------------------------------------------- 적 해킹: 화살표 빠른 입력(해킹 시스템 기획 10장)

void USisterHackComponent::StartEnemyHack(AActor* Target)
{
	// 퍼즐은 여동생을 조작하는 머신에서 로컬로(입력 반응성), 결과만 서버가 승인한다
	ActiveEnemy = Target;
	bEnemyResolved = false;
	EnemyElapsed = 0.0f;
	EnemyArrows.Start(EnemyArrowCount);
	FocusTerminal = nullptr;

	// 단말기 해킹과 같은 규칙: 제자리 고정(최대 4초) — 그동안 거미는 계속 공격한다(오빠가 엄호하는 협동 구간)
	SetMovementLocked(true);
	SetFeedback(EHackFeedback::None);
	PlayHackSound(HackStartSound);
	StartHackLoop();
	UE_LOG(LogTemp, Log, TEXT("SisterHack: enemy hack start on %s"), *Target->GetName());
}

void USisterHackComponent::TickEnemyHack(float DeltaTime)
{
	if (bEnemyResolved)
	{
		return;
	}

	// 대상이 죽었거나 다른 이유로 그로기/제거되면 세션 실패(그로기 없음)
	const ICXHackTarget* Target = Cast<ICXHackTarget>(ActiveEnemy);
	if (!IsValid(ActiveEnemy) || !Target || !Target->IsHackTargetAvailable())
	{
		ResolveEnemyHack(false);
		return;
	}

	// 틀려도 시간은 계속 흐른다(같은 배열 처음부터) → 벌칙은 "시간 손해"뿐, 4초가 자연스러운 상한
	EnemyElapsed += DeltaTime;
	if (EnemyElapsed >= EnemyArrowTimeLimit)
	{
		ResolveEnemyHack(false);
	}
}

void USisterHackComponent::InputEnemyArrow(EHackArrow Direction)
{
	if (!ActiveEnemy || bEnemyResolved)
	{
		return;
	}
	switch (EnemyArrows.Input(Direction))
	{
	case ECXArrowStep::Wrong:		SetFeedback(EHackFeedback::Denied); break;
	case ECXArrowStep::Correct:		SetFeedback(EHackFeedback::Correct); break;
	case ECXArrowStep::Complete:	ResolveEnemyHack(true); break;
	default: break;
	}
}

void USisterHackComponent::ResolveEnemyHack(bool bSuccess)
{
	if (!ActiveEnemy || bEnemyResolved)
	{
		return;
	}
	bEnemyResolved = true;
	SetFeedback(bSuccess ? EHackFeedback::Granted : EHackFeedback::Failed);
	// 결과가 나면 바로 움직일 수 있게(패널은 잠깐 더 보여 줌)
	SetMovementLocked(false);
	StopHackLoop();

	if (bSuccess)
	{
		AActor* Target = ActiveEnemy;
		if (GetOwner()->HasAuthority())
		{
			ApplyEnemyHackOnServer(Target);
		}
		else
		{
			Server_ApplyEnemyHack(Target);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("SisterHack: enemy hack %s on %s (t=%.2f, arrow %d/%d)"), bSuccess ? TEXT("SUCCESS") : TEXT("FAILED"),
		*GetNameSafe(ActiveEnemy), EnemyElapsed, EnemyArrows.Index, EnemyArrows.Num());

	GetWorld()->GetTimerManager().SetTimer(EnemyResultTimer, this, &USisterHackComponent::EndEnemySession, FMath::Max(0.05f, EnemyResultHoldTime), false);
}

void USisterHackComponent::EndEnemySession()
{
	GetWorld()->GetTimerManager().ClearTimer(EnemyResultTimer);
	ActiveEnemy = nullptr;
	bEnemyResolved = false;
	EnemyArrows.Reset();
	EnemyElapsed = 0.0f;
	EnemyCooldownRemaining = EnemyHackCooldown;
	SetMovementLocked(false);
	StopHackLoop();
}

void USisterHackComponent::Server_ApplyEnemyHack_Implementation(AActor* Target)
{
	ApplyEnemyHackOnServer(Target);
}

bool USisterHackComponent::ApplyEnemyHackOnServer(AActor* Target)
{
	// 서버 재검증: 살아 있는 여동생, 해킹 가능한 대상(생존·그로기 아님), 거리(+여유), 서버 기준 쿨타임
	APawn* Pawn = Cast<APawn>(GetOwner());
	ICXHackTarget* HackTarget = Cast<ICXHackTarget>(Target);
	if (!Pawn || !HackTarget || !IsValid(Target) || !UCXPlayerLifeComponent::IsPawnAlive(Pawn) || !HackTarget->IsHackTargetAvailable())
	{
		return false;
	}
	const float Dist = FVector::Dist(Pawn->GetActorLocation(), Target->GetActorLocation());
	if (Dist > HackTarget->GetHackInteractRange() + EnemyHackServerRangeSlack)
	{
		UE_LOG(LogTemp, Warning, TEXT("SisterHack: server rejected enemy hack on %s (dist %.0f)"), *Target->GetName(), Dist);
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastServerEnemyHackTime < EnemyHackCooldown)
	{
		UE_LOG(LogTemp, Warning, TEXT("SisterHack: server rejected enemy hack on %s (cooldown)"), *Target->GetName());
		return false;
	}
	LastServerEnemyHackTime = Now;
	return HackTarget->ApplyHackSuccess(Pawn);
}

namespace SisterHackDebug
{
	const TCHAR* FeedbackName(EHackFeedback Feedback)
	{
		static const TCHAR* Names[] = { TEXT("None"), TEXT("Correct"), TEXT("RoundClear"), TEXT("Denied"), TEXT("Granted"), TEXT("Failed") };
		return Names[FMath::Clamp(static_cast<int32>(Feedback), 0, 5)];
	}

	FString ArrowString(const TArray<EHackArrow>& Sequence)
	{
		static const TCHAR* Names[] = { TEXT("U"), TEXT("D"), TEXT("L"), TEXT("R") };
		FString Out;
		for (const EHackArrow Arrow : Sequence)
		{
			Out += Names[FMath::Clamp(static_cast<int32>(Arrow), 0, 3)];
		}
		return Out;
	}
}

FString USisterHackComponent::DebugEnemyArrowPress(bool bCorrect)
{
	EHackArrow Expected = EHackArrow::Up;
	if (ActiveEnemy && !bEnemyResolved && EnemyArrows.GetExpected(Expected))
	{
		// 오답 = 정답과 반대 방향(위↔아래, 좌↔우)
		const EHackArrow Opposite = Expected == EHackArrow::Up ? EHackArrow::Down : Expected == EHackArrow::Down ? EHackArrow::Up
			: Expected == EHackArrow::Left ? EHackArrow::Right : EHackArrow::Left;
		InputDirection(bCorrect ? Expected : Opposite);
	}
	return GetEnemyHackDebugState();
}

FString USisterHackComponent::GetEnemyHackDebugState() const
{
	return FString::Printf(TEXT("enemyHack=%s resolved=%d arrows=%s index=%d/%d elapsed=%.2f/%.1f feedback=%s cooldown=%.2f moveLocked=%d"),
		*GetNameSafe(ActiveEnemy), bEnemyResolved ? 1 : 0, *SisterHackDebug::ArrowString(EnemyArrows.Sequence), EnemyArrows.Index, EnemyArrows.Num(),
		EnemyElapsed, EnemyArrowTimeLimit, SisterHackDebug::FeedbackName(Feedback), EnemyCooldownRemaining, bMovementLocked ? 1 : 0);
}

// ---------------------------------------------------------------- 단말기 주파수 동조(해킹 시스템 기획 12장)

bool USisterHackComponent::IsFreqLockSession() const
{
	return ActiveTerminal && ActiveTerminal->PuzzleType == EHackPuzzleType::FrequencyLock;
}

void USisterHackComponent::TickFreqLock(float DeltaTime)
{
	if (ActiveTerminal->GetState() == EHackTerminalState::Hacked || Feedback == EHackFeedback::Granted)
	{
		return;
	}
	if (FreqLock.Tick(DeltaTime))
	{
		// 단말기에서는 시간 초과 = 실패가 아니라 1단계부터 다시(무제한 재시도 원칙). 기본 설정은 제한 시간 없음.
		FreqLock.Restart();
		SetFeedback(EHackFeedback::Denied);
	}
}

void USisterHackComponent::ConfirmFreqLock()
{
	if (!IsFreqLockSession() || ActiveTerminal->GetState() == EHackTerminalState::Hacked || Feedback == EHackFeedback::Granted)
	{
		return;
	}
	switch (FreqLock.Confirm())
	{
	case ECXFreqLockStep::Miss:		SetFeedback(EHackFeedback::Denied); break;
	case ECXFreqLockStep::Hit:		SetFeedback(EHackFeedback::Correct); break;
	case ECXFreqLockStep::Complete:	FinishSuccess(); break;
	default: break;
	}
}

FString USisterHackComponent::DebugFreqLockPress(bool bInsideZone)
{
	if (IsFreqLockSession())
	{
		FreqLock.DebugPlaceCursor(bInsideZone);
		ConfirmFreqLock();
	}
	return GetFreqLockDebugState();
}

FString USisterHackComponent::GetFreqLockDebugState() const
{
	return FString::Printf(TEXT("freqTerminal=%s state=%d step=%d/%d cursor=%.2f zone=%.2f+-%.3f elapsed=%.2f limit=%.1f feedback=%s moveLocked=%d"),
		ActiveTerminal ? *ActiveTerminal->TerminalName : TEXT("None"), ActiveTerminal ? static_cast<int32>(ActiveTerminal->GetState()) : -1,
		FreqLock.Step, FreqLock.GetStepCount(), FreqLock.Cursor, FreqLock.ZoneCenter, FreqLock.GetZoneWidth() * 0.5f, FreqLock.Elapsed,
		FreqLock.HasTimeLimit() ? FreqLock.TimeLimit : 0.0f, SisterHackDebug::FeedbackName(Feedback), bMovementLocked ? 1 : 0);
}

// ---------------------------------------------------------------- 방향 입력

void USisterHackComponent::InputDirection(EHackArrow Direction)
{
	if (ActiveEnemy)
	{
		InputEnemyArrow(Direction);
		return;
	}
	if (!ActiveTerminal || ActiveTerminal->GetState() == EHackTerminalState::Hacked)
	{
		return;
	}

	if (ActiveTerminal->PuzzleType == EHackPuzzleType::Passcode)
	{
		switch (Direction)
		{
		case EHackArrow::Up:    PasscodeInput[PasscodeCursor] = (PasscodeInput[PasscodeCursor] + 1) % 10; break;
		case EHackArrow::Down:  PasscodeInput[PasscodeCursor] = (PasscodeInput[PasscodeCursor] + 9) % 10; break;
		case EHackArrow::Left:  PasscodeCursor = (PasscodeCursor + 3) % 4; break;
		case EHackArrow::Right: PasscodeCursor = (PasscodeCursor + 1) % 4; break;
		}
		PlayHackSound(DigitTickSound);
		return;
	}

	if (ActiveTerminal->PuzzleType == EHackPuzzleType::MemoryPattern)
	{
		// 3x3 커서(전화 키패드 배치). 가장자리에서 멈춤(감아 돌리면 여동생이 위치를 잃기 쉽다)
		int32 Col = MemoryCursor % 3;
		int32 Row = MemoryCursor / 3;
		switch (Direction)
		{
		case EHackArrow::Up:    Row = FMath::Max(0, Row - 1); break;
		case EHackArrow::Down:  Row = FMath::Min(2, Row + 1); break;
		case EHackArrow::Left:  Col = FMath::Max(0, Col - 1); break;
		case EHackArrow::Right: Col = FMath::Min(2, Col + 1); break;
		}
		const int32 NewCursor = Row * 3 + Col;
		if (NewCursor != MemoryCursor)
		{
			MemoryCursor = NewCursor;
			PlayHackSound(DigitTickSound);
		}
		return;
	}

	// 주파수 동조는 F/X만 쓴다(D-pad 무시). 화살표 시퀀스는 단말기에서 폐기(적 해킹 전용).
}

void USisterHackComponent::SubmitPasscode()
{
	if (!ActiveTerminal || ActiveTerminal->PuzzleType != EHackPuzzleType::Passcode || ActiveTerminal->GetState() == EHackTerminalState::Hacked)
	{
		return;
	}

	bool bMatch = ActiveTerminal->Passcode.Len() == 4;
	for (int32 i = 0; bMatch && i < 4; ++i)
	{
		bMatch = PasscodeInput[i] == ActiveTerminal->GetPasscodeDigit(i);
	}

	if (bMatch)
	{
		FinishSuccess();
	}
	else
	{
		// 입력값은 유지 — 틀린 자리만 고치면 되도록
		SetFeedback(EHackFeedback::Denied);
	}
}

void USisterHackComponent::FinishSuccess()
{
	SetFeedback(EHackFeedback::Granted);
	ActiveTerminal->MarkHacked();
	// 성공 표시를 잠깐 보여준 뒤 세션 종료
	GetWorld()->GetTimerManager().SetTimer(GrantedTimer, this, &USisterHackComponent::EndSession, GrantedHoldTime, false);
}

void USisterHackComponent::EndSession()
{
	GetWorld()->GetTimerManager().ClearTimer(GrantedTimer);
	ActiveTerminal = nullptr;
	FocusTerminal = nullptr;
	FreqLock.Reset();
	MemoryLastPressed = INDEX_NONE;
	MemoryLocalCorrect = 0;
	SetMovementLocked(false);
	StopHackLoop();
}

void USisterHackComponent::SetMovementLocked(bool bLocked)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || bLocked == bMovementLocked)
	{
		return;
	}
	bMovementLocked = bLocked;
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (bLocked)
	{
		// 해킹 중에는 제자리 고정(점프 버튼(A)이 눌려도 무시되도록 이동 모드 자체를 끈다)
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	else
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
}

void USisterHackComponent::SetFeedback(EHackFeedback NewFeedback)
{
	Feedback = NewFeedback;
	FeedbackTime = GetWorld()->GetTimeSeconds();

	// HUD 판정 연출과 같은 프레임에 소리
	switch (NewFeedback)
	{
	case EHackFeedback::Correct:
		// 맞힌 순서(1부터)마다 조금씩 높게. 주파수 동조는 단계가 3개뿐이라 두 칸씩 올린다.
		PlayHackSound(CorrectSound, 1.0f + CorrectPitchStep * FMath::Max(0, (ActiveEnemy ? EnemyArrows.Index : (IsFreqLockSession() ? FreqLock.Step * 2 : MemoryLocalCorrect)) - 1));
		break;
	case EHackFeedback::Failed:		PlayHackSound(DeniedSound, 0.8f); break;
	case EHackFeedback::RoundClear:	PlayHackSound(RoundClearSound); break;
	case EHackFeedback::Denied:		PlayHackSound(DeniedSound); break;
	case EHackFeedback::Granted:	PlayHackSound(GrantedSound); break;
	default: break;
	}
}

// ---------------------------------------------------------------- 사운드

bool USisterHackComponent::IsLocallyControlledOwner() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn && Pawn->IsLocallyControlled();
}

void USisterHackComponent::PlayHackSound(USoundBase* Sound, float Pitch)
{
	if (!Sound || !IsLocallyControlledOwner())
	{
		return;
	}
	// 해킹 UI 원샷은 동시에 1개 — 이전 소리를 끊고 새 소리
	if (UAudioComponent* Previous = LastHackOneShot.Get())
	{
		Previous->Stop();
	}
	LastHackOneShot = UGameplayStatics::SpawnSound2D(this, Sound, 1.0f, Pitch);
}

void USisterHackComponent::StartHackLoop()
{
	if (!HackLoopSound || !IsLocallyControlledOwner())
	{
		return;
	}
	if (!HackLoopAudio)
	{
		HackLoopAudio = UGameplayStatics::SpawnSound2D(this, HackLoopSound, 1.0f, 1.0f, 0.0f, nullptr, false, false);
	}
	else
	{
		HackLoopAudio->Play();
	}
}

void USisterHackComponent::StopHackLoop()
{
	if (HackLoopAudio && HackLoopAudio->IsPlaying())
	{
		HackLoopAudio->FadeOut(HackLoopFadeOut, 0.0f);
	}
}

// ---------------------------------------------------------------- 조회

int32 USisterHackComponent::GetRoundCount() const
{
	if (!ActiveTerminal)
	{
		return 0;
	}
	return ActiveTerminal->PuzzleType == EHackPuzzleType::MemoryPattern ? ActiveTerminal->GetMemoryRoundCount() : 1;
}

float USisterHackComponent::GetFeedbackAge() const
{
	return GetWorld()->GetTimeSeconds() - FeedbackTime;
}

float USisterHackComponent::GetProgress() const
{
	if (ActiveEnemy)
	{
		return EnemyArrows.Num() > 0 ? static_cast<float>(EnemyArrows.Index) / EnemyArrows.Num() : 0.0f;
	}
	if (!ActiveTerminal)
	{
		return 0.0f;
	}
	if (ActiveTerminal->GetState() == EHackTerminalState::Hacked)
	{
		return 1.0f;
	}
	if (ActiveTerminal->PuzzleType == EHackPuzzleType::Passcode)
	{
		return 0.0f;
	}
	if (ActiveTerminal->PuzzleType == EHackPuzzleType::MemoryPattern)
	{
		// 전체 노드 수(라운드 길이 합) 대비 지금까지 맞힌 수 — 단말기 복제 값 기준
		int32 MemTotal = 0;
		int32 MemDone = 0;
		const TArray<int32>& MemLengths = ActiveTerminal->MemoryRoundLengths;
		for (int32 i = 0; i < MemLengths.Num(); ++i)
		{
			MemTotal += MemLengths[i];
			if (i < ActiveTerminal->GetMemoryRound())
			{
				MemDone += MemLengths[i];
			}
		}
		MemDone += ActiveTerminal->GetMemoryInputIndex();
		return MemTotal > 0 ? FMath::Clamp(static_cast<float>(MemDone) / MemTotal, 0.0f, 1.0f) : 0.0f;
	}
	if (ActiveTerminal->PuzzleType == EHackPuzzleType::FrequencyLock)
	{
		return static_cast<float>(FreqLock.Step) / FreqLock.GetStepCount();
	}
	return 0.0f;
}

// ---------------------------------------------------------------- 메모리 패턴(해킹 시스템 기획 11장) — 서버 권위

bool USisterHackComponent::IsMemorySession() const
{
	return ActiveTerminal && ActiveTerminal->PuzzleType == EHackPuzzleType::MemoryPattern;
}

void USisterHackComponent::SendMemorySession(bool bBegin)
{
	if (!ActiveTerminal)
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		Server_MemorySession_Implementation(ActiveTerminal, bBegin);
	}
	else
	{
		Server_MemorySession(ActiveTerminal, bBegin);
	}
}

void USisterHackComponent::Server_MemorySession_Implementation(AHackTerminal* Terminal, bool bBegin)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Terminal) || !Pawn || !Terminal->IsMemoryPattern())
	{
		return;
	}
	if (bBegin)
	{
		// 서버 재검증: 살아 있음 + 단말기 가까이 + 아직 해킹 가능
		if (!UCXPlayerLifeComponent::IsPawnAlive(Pawn) || Terminal->GetState() == EHackTerminalState::Hacked
			|| FVector::Dist(Pawn->GetActorLocation(), Terminal->GetActorLocation()) > Terminal->InteractRange + Terminal->MemoryServerRangeSlack)
		{
			UE_LOG(LogTemp, Warning, TEXT("SisterHack: server rejected memory session on %s"), *Terminal->TerminalName);
			// 여동생 쪽 세션을 닫게 한다(로컬 폰이면 즉시 실행되는 Client RPC)
			Client_MemoryResult(EHackFeedback::Failed);
			return;
		}
		Terminal->ServerMemoryBegin(Pawn);
	}
	else
	{
		Terminal->ServerMemoryEnd(Pawn);
	}
}

void USisterHackComponent::PressMemoryNode(int32 Node)
{
	if (!IsMemorySession() || ActiveTerminal->GetState() == EHackTerminalState::Hacked || Feedback == EHackFeedback::Granted)
	{
		return;
	}
	Node = FMath::Clamp(Node, 0, 8);
	if (!ActiveTerminal->IsMemoryRelayActive())
	{
		// 오빠 중계 전에는 입력 무시(서버도 거부) — 낮은 거부음만. HUD는 PARTNER RELAY REQUIRED를 깜빡인다.
		PlayHackSound(DeniedSound, 0.7f);
		return;
	}
	MemoryLastPressed = Node;
	if (GetOwner()->HasAuthority())
	{
		// 리슨 서버 호스트(로컬 2인 포함): 바로 판정
		HandleMemoryResult(ActiveTerminal->ServerMemoryPress(Cast<APawn>(GetOwner()), Node));
	}
	else
	{
		Server_MemoryPress(ActiveTerminal, Node);
	}
}

void USisterHackComponent::Server_MemoryPress_Implementation(AHackTerminal* Terminal, int32 Node)
{
	if (!IsValid(Terminal))
	{
		return;
	}
	const EHackFeedback Result = Terminal->ServerMemoryPress(Cast<APawn>(GetOwner()), Node);
	if (Result != EHackFeedback::None)
	{
		Client_MemoryResult(Result);
	}
}

void USisterHackComponent::Client_MemoryResult_Implementation(EHackFeedback Result)
{
	HandleMemoryResult(Result);
}

void USisterHackComponent::HandleMemoryResult(EHackFeedback Result)
{
	if (!IsMemorySession())
	{
		return;
	}
	switch (Result)
	{
	case EHackFeedback::Correct:
		++MemoryLocalCorrect;
		SetFeedback(EHackFeedback::Correct);
		break;
	case EHackFeedback::RoundClear:
		MemoryLocalCorrect = 0;
		SetFeedback(EHackFeedback::RoundClear);
		break;
	case EHackFeedback::Denied:
		MemoryLocalCorrect = 0;
		SetFeedback(EHackFeedback::Denied);
		break;
	case EHackFeedback::Failed:
		// 서버가 세션 시작을 거부(사망·거리·이미 해킹됨) — 조용히 닫는다
		if (ActiveTerminal->GetState() != EHackTerminalState::Hacked)
		{
			ActiveTerminal->SetInProgress(false);
		}
		PlayHackSound(HackCancelSound);
		EndSession();
		break;
	case EHackFeedback::Granted:
		// 단말기 Hacked는 서버가 이미 처리(복제) — 여기서는 성공 표시 후 세션 종료만
		SetFeedback(EHackFeedback::Granted);
		GetWorld()->GetTimerManager().SetTimer(GrantedTimer, this, &USisterHackComponent::EndSession, GrantedHoldTime, false);
		break;
	default:
		break;
	}
}

FString USisterHackComponent::DebugMemoryPress(int32 Node)
{
	if (IsMemorySession())
	{
		MemoryCursor = FMath::Clamp(Node, 0, 8);
		Interact();
	}
	return ActiveTerminal ? ActiveTerminal->GetMemoryDebugState() : FString(TEXT("no session"));
}

FString USisterHackComponent::DebugMemoryPressAnswer(bool bCorrect)
{
	if (!IsMemorySession())
	{
		return TEXT("no memory session");
	}
	const int32 Expected = ActiveTerminal->GetMemoryExpectedNode();
	if (Expected == INDEX_NONE)
	{
		return ActiveTerminal->GetMemoryDebugState();
	}
	return DebugMemoryPress(bCorrect ? Expected : (Expected + 4) % 9);
}
