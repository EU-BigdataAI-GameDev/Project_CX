// Copyright Epic Games, Inc. All Rights Reserved.

#include "SisterHackComponent.h"
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

USisterHackComponent::USisterHackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void USisterHackComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildInputAssets();
}

void USisterHackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bInputBound)
	{
		TryBindInput();
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
	HackMappingContext->MapKey(InteractAction, EKeys::Gamepad_FaceButton_Left);
	HackMappingContext->MapKey(InteractAction, EKeys::F);
	HackMappingContext->MapKey(CancelAction, EKeys::Gamepad_FaceButton_Right);
	HackMappingContext->MapKey(CancelAction, EKeys::Escape);
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

	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &USisterHackComponent::Interact);
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
	if (ActiveTerminal)
	{
		FocusTerminal = ActiveTerminal;
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
}

// ---------------------------------------------------------------- 세션

void USisterHackComponent::Interact()
{
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
	if (!ActiveTerminal || ActiveTerminal->GetState() == EHackTerminalState::Hacked)
	{
		return;
	}
	ActiveTerminal->SetInProgress(false);
	EndSession();
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
}

void USisterHackComponent::SetMovementLocked(bool bLocked)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}
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
