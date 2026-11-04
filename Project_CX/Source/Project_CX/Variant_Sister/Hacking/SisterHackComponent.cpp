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

	// 적 해킹 쿨타임 / 주파수 동조 진행 — 월드 Tick 기준이라 일시정지(ESC 메뉴) 중에는 함께 멈춘다
	if (EnemyCooldownRemaining > 0.0f && !ActiveEnemy)
	{
		EnemyCooldownRemaining = FMath::Max(0.0f, EnemyCooldownRemaining - DeltaTime);
	}
	if (ActiveEnemy)
	{
		TickEnemyHack(DeltaTime);
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
		// 적 해킹 중 F/X = 주파수 고정(확인)
		ConfirmFreqLock();
		return;
	}
	if (ActiveTerminal)
	{
		// 해킹 중 X: 비밀번호 퍼즐에서는 제출
		if (ActiveTerminal->PuzzleType == EHackPuzzleType::Passcode)
		{
			SubmitPasscode();
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

	if (ActiveTerminal->PuzzleType == EHackPuzzleType::ArrowSequence)
	{
		StartRound(0);
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

// ---------------------------------------------------------------- 적 해킹: 주파수 동조(해킹 시스템 기획 10장)

void USisterHackComponent::StartEnemyHack(AActor* Target)
{
	// 퍼즐은 여동생을 조작하는 머신에서 로컬로(입력 반응성), 결과만 서버가 승인한다
	ActiveEnemy = Target;
	bEnemyResolved = false;
	FreqStep = 0;
	FreqElapsed = 0.0f;
	FreqLockoutUntil = 0.0f;
	FreqCursor = 0.0f;
	FreqDirection = 1.0f;
	PlaceFreqZone();
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

	FreqElapsed += DeltaTime;
	if (FreqElapsed >= FreqTimeLimit)
	{
		ResolveEnemyHack(false);
		return;
	}

	// 커서 왕복(막대 양 끝에서 반사)
	FreqCursor += FreqDirection * GetFreqCursorSpeed() * DeltaTime;
	for (int32 Guard = 0; Guard < 4 && (FreqCursor > 1.0f || FreqCursor < 0.0f); ++Guard)
	{
		if (FreqCursor > 1.0f)
		{
			FreqCursor = 2.0f - FreqCursor;
			FreqDirection = -1.0f;
		}
		else
		{
			FreqCursor = -FreqCursor;
			FreqDirection = 1.0f;
		}
	}
	FreqCursor = FMath::Clamp(FreqCursor, 0.0f, 1.0f);
}

void USisterHackComponent::ConfirmFreqLock()
{
	if (!ActiveEnemy || bEnemyResolved || FreqElapsed < FreqLockoutUntil)
	{
		return;
	}

	const bool bHit = FMath::Abs(FreqCursor - FreqZoneCenter) <= GetFreqZoneWidth() * 0.5f;
	if (!bHit)
	{
		// 빗나감: 1단계부터 다시(새 구간) + 짧은 입력 무시. 시간 제한은 계속 흐른다 → 벌칙은 "시간 손해"뿐.
		FreqStep = 0;
		FreqLockoutUntil = FreqElapsed + FreqMissLockout;
		PlaceFreqZone();
		SetFeedback(EHackFeedback::Denied);
		return;
	}

	++FreqStep;
	if (FreqStep >= GetFreqStepCount())
	{
		ResolveEnemyHack(true);
		return;
	}
	// 다음 단계: 구간이 좁아지고 커서가 빨라진다
	PlaceFreqZone();
	SetFeedback(EHackFeedback::Correct);
}

void USisterHackComponent::PlaceFreqZone()
{
	// 구간이 막대 안에 들어가고, 커서에서 FreqZoneMinCursorGap 이상 떨어진 위치
	const float Half = GetFreqZoneWidth() * 0.5f;
	const float Min = Half + 0.02f;
	const float Max = 1.0f - Half - 0.02f;
	float Center = FMath::FRandRange(Min, Max);
	for (int32 Try = 0; Try < 8 && FMath::Abs(Center - FreqCursor) < FreqZoneMinCursorGap; ++Try)
	{
		Center = FMath::FRandRange(Min, Max);
	}
	if (FMath::Abs(Center - FreqCursor) < FreqZoneMinCursorGap)
	{
		// 운 나쁘면 커서 반대편 끝 쪽으로
		Center = FreqCursor < 0.5f ? Max : Min;
	}
	FreqZoneCenter = Center;
}

float USisterHackComponent::GetFreqZoneWidth() const
{
	if (FreqZoneWidths.Num() == 0)
	{
		return 0.2f;
	}
	return FMath::Clamp(FreqZoneWidths[FMath::Clamp(FreqStep, 0, FreqZoneWidths.Num() - 1)], 0.02f, 1.0f);
}

float USisterHackComponent::GetFreqCursorSpeed() const
{
	if (FreqCursorSpeeds.Num() == 0)
	{
		return 1.5f;
	}
	return FMath::Max(0.1f, FreqCursorSpeeds[FMath::Clamp(FreqStep, 0, FreqCursorSpeeds.Num() - 1)]);
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
	UE_LOG(LogTemp, Log, TEXT("SisterHack: enemy hack %s on %s (t=%.2f, step %d)"), bSuccess ? TEXT("SUCCESS") : TEXT("FAILED"),
		*GetNameSafe(ActiveEnemy), FreqElapsed, FreqStep);

	GetWorld()->GetTimerManager().SetTimer(EnemyResultTimer, this, &USisterHackComponent::EndEnemySession, FMath::Max(0.05f, EnemyResultHoldTime), false);
}

void USisterHackComponent::EndEnemySession()
{
	GetWorld()->GetTimerManager().ClearTimer(EnemyResultTimer);
	ActiveEnemy = nullptr;
	bEnemyResolved = false;
	FreqStep = 0;
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

FString USisterHackComponent::DebugFreqLockPress(bool bInsideZone)
{
	if (ActiveEnemy && !bEnemyResolved)
	{
		// 입력 무시 시간도 테스트에서는 건너뛴다
		FreqLockoutUntil = 0.0f;
		const float Half = GetFreqZoneWidth() * 0.5f;
		if (bInsideZone)
		{
			FreqCursor = FreqZoneCenter;
		}
		else
		{
			// 구간 바깥에서 더 넓은 쪽
			FreqCursor = FreqZoneCenter > 0.5f ? FMath::Max(0.0f, FreqZoneCenter - Half - 0.1f) : FMath::Min(1.0f, FreqZoneCenter + Half + 0.1f);
		}
		ConfirmFreqLock();
	}
	return GetEnemyHackDebugState();
}

FString USisterHackComponent::GetEnemyHackDebugState() const
{
	static const TCHAR* FeedbackNames[] = { TEXT("None"), TEXT("Correct"), TEXT("RoundClear"), TEXT("Denied"), TEXT("Granted"), TEXT("Failed") };
	return FString::Printf(TEXT("enemyHack=%s resolved=%d step=%d/%d cursor=%.2f zone=%.2f+-%.3f elapsed=%.2f/%.1f feedback=%s cooldown=%.2f moveLocked=%d"),
		*GetNameSafe(ActiveEnemy), bEnemyResolved ? 1 : 0, FreqStep, GetFreqStepCount(), FreqCursor,
		FreqZoneCenter, GetFreqZoneWidth() * 0.5f, FreqElapsed, FreqTimeLimit,
		FeedbackNames[FMath::Clamp(static_cast<int32>(Feedback), 0, 5)], EnemyCooldownRemaining, bMovementLocked ? 1 : 0);
}

void USisterHackComponent::InputDirection(EHackArrow Direction)
{
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

	if (!ArrowSequence.IsValidIndex(ArrowIndex))
	{
		return;
	}

	if (ArrowSequence[ArrowIndex] != Direction)
	{
		// 틀리면 이 라운드 처음부터(같은 배열 유지 → 외워서 재도전)
		ArrowIndex = 0;
		SetFeedback(EHackFeedback::Denied);
		return;
	}

	++ArrowIndex;
	if (ArrowIndex < ArrowSequence.Num())
	{
		SetFeedback(EHackFeedback::Correct);
		return;
	}

	if (RoundIndex + 1 < GetRoundCount())
	{
		StartRound(RoundIndex + 1);
		SetFeedback(EHackFeedback::RoundClear);
	}
	else
	{
		FinishSuccess();
	}
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

void USisterHackComponent::StartRound(int32 NewRoundIndex)
{
	RoundIndex = NewRoundIndex;
	ArrowIndex = 0;
	const TArray<int32>& Lengths = ActiveTerminal->ArrowRoundLengths;
	ArrowSequence = ActiveTerminal->MakeArrowRound(Lengths.IsValidIndex(RoundIndex) ? Lengths[RoundIndex] : 4);
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
	ArrowSequence.Reset();
	ArrowIndex = 0;
	RoundIndex = 0;
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
		// ArrowIndex는 이미 다음 칸 — 맞힌 순서(1부터)마다 조금씩 높게. 적 해킹은 단계가 3개뿐이라 두 칸씩 올린다.
		PlayHackSound(CorrectSound, 1.0f + CorrectPitchStep * FMath::Max(0, (ActiveEnemy ? FreqStep * 2 : ArrowIndex) - 1));
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
	return ActiveTerminal ? FMath::Max(1, ActiveTerminal->ArrowRoundLengths.Num()) : 0;
}

float USisterHackComponent::GetFeedbackAge() const
{
	return GetWorld()->GetTimeSeconds() - FeedbackTime;
}

float USisterHackComponent::GetProgress() const
{
	if (ActiveEnemy)
	{
		return static_cast<float>(FreqStep) / GetFreqStepCount();
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

	// 전체 화살표 수 대비 지금까지 맞힌 수
	int32 Total = 0;
	int32 Done = 0;
	const TArray<int32>& Lengths = ActiveTerminal->ArrowRoundLengths;
	for (int32 i = 0; i < Lengths.Num(); ++i)
	{
		Total += Lengths[i];
		if (i < RoundIndex)
		{
			Done += Lengths[i];
		}
	}
	Done += ArrowIndex;
	return Total > 0 ? static_cast<float>(Done) / Total : 0.0f;
}
