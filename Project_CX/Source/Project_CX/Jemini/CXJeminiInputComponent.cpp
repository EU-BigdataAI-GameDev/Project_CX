// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXJeminiInputComponent.h"
#include "CXJeminiDrone.h"
#include "CXPlayerLifeComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXJeminiInput, Log, All);

int32 UCXJeminiInputComponent::OpenChatCount = 0;

namespace CXJeminiInput
{
	constexpr int32 MappingPriority = 5;
	constexpr int32 VoiceChunkBytes = 32 * 1024;
	constexpr int32 MaxVoiceBytes = 2 * 1024 * 1024;
	const FLinearColor Accent(0.1f, 0.95f, 1.0f, 1.0f);
}

// ---------------------------------------------------------------- 채팅 입력창(Slate, WBP 없음)

/**
 *  분할 칸 아래쪽의 한 줄 입력창. Enter = 전송, Esc = 취소, 포커스를 잃으면 취소.
 *  입력창이 처리하지 않은 키(W/A/S/D 등)는 여기서 삼켜 게임 뷰포트(이동·사격)로 새지 않게 한다.
 */
class SCXJeminiChatBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCXJeminiChatBox) {}
	SLATE_END_ARGS()

	TFunction<void(const FString&)> OnSubmit;
	TFunction<void()> OnCancel;

	void Construct(const FArguments& InArgs)
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 18);
		FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);

		ChildSlot
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 150.0f))
		[
			SNew(SBox)
			.WidthOverride(760.0f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.01f, 0.02f, 0.04f, 0.88f))
				.Padding(FMargin(14.0f, 10.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.0f, 0.0f, 12.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("JEMINI >")))
						.Font(LabelFont)
						.ColorAndOpacity(CXJeminiInput::Accent)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew(TextBox, SEditableTextBox)
						.Font(Font)
						.HintText(FText::FromString(TEXT("제미니에게 부탁하기 (Enter 전송 / Esc 취소)")))
						.ClearKeyboardFocusOnCommit(false)
						.SelectAllTextWhenFocused(true)
						.OnTextCommitted(this, &SCXJeminiChatBox::HandleCommitted)
					]
				]
			]
		];
	}

	TSharedPtr<SWidget> GetFocusTarget() const { return TextBox; }

	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			Cancel();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		// 입력창이 처리하지 않고 올라온 키는 게임으로 보내지 않는다
		return FReply::Handled();
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }

	void Cancel()
	{
		if (bClosed)
		{
			return;
		}
		bClosed = true;
		if (OnCancel)
		{
			OnCancel();
		}
	}

private:
	void HandleCommitted(const FText& Text, ETextCommit::Type CommitType)
	{
		if (bClosed)
		{
			return;
		}
		if (CommitType == ETextCommit::OnEnter)
		{
			bClosed = true;
			if (OnSubmit)
			{
				OnSubmit(Text.ToString());
			}
		}
		else if (CommitType == ETextCommit::OnUserMovedFocus || CommitType == ETextCommit::OnCleared)
		{
			Cancel();
		}
	}

	TSharedPtr<SEditableTextBox> TextBox;
	bool bClosed = false;
};

// ---------------------------------------------------------------- 컴포넌트

UCXJeminiInputComponent::UCXJeminiInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Server RPC(Server_SubmitText / Server_VoiceChunk)를 쓰려면 복제 컴포넌트여야 한다
	SetIsReplicatedByDefault(true);
}

UCXJeminiInputComponent* UCXJeminiInputComponent::EnsureOn(APawn* Pawn)
{
	if (!Pawn)
	{
		return nullptr;
	}
	if (UCXJeminiInputComponent* Existing = Pawn->FindComponentByClass<UCXJeminiInputComponent>())
	{
		return Existing;
	}
	UCXJeminiInputComponent* Component = NewObject<UCXJeminiInputComponent>(Pawn, TEXT("CXJeminiInput"));
	Pawn->AddInstanceComponent(Component);
	Component->RegisterComponent();
	return Component;
}

void UCXJeminiInputComponent::BeginPlay()
{
	Super::BeginPlay();
	BuildInputAssets();
}

void UCXJeminiInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseChat();
	Recorder.Cancel();
	if (APlayerController* PC = BoundController.Get(); PC && JeminiInput)
	{
		PC->PopInputComponent(JeminiInput);
	}
	BoundController.Reset();
	Super::EndPlay(EndPlayReason);
}

void UCXJeminiInputComponent::BuildInputAssets()
{
	ChatAction = NewObject<UInputAction>(this, TEXT("IA_Jemini_Chat"));
	ChatAction->ValueType = EInputActionValueType::Boolean;
	TalkAction = NewObject<UInputAction>(this, TEXT("IA_Jemini_Talk"));
	TalkAction->ValueType = EInputActionValueType::Boolean;

	// 키보드 Enter = 채팅, 게임패드 Y(FaceButton_Top) = 누르는 동안 녹음 — 기존 바인딩 어디에도 없는 키
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Jemini"));
	MappingContext->MapKey(ChatAction, EKeys::Enter);
	MappingContext->MapKey(TalkAction, EKeys::Gamepad_FaceButton_Top);
}

void UCXJeminiInputComponent::TryBindInput()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC || !PC->GetLocalPlayer() || !ChatAction)
	{
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MappingContext, CXJeminiInput::MappingPriority);
	}
	// 폰 InputComponent 대신 전용 입력 컴포넌트를 컨트롤러 스택에 올린다 — 사망 중 Pawn->DisableInput, 채팅 중 폰 입력 차단과 무관하게 동작
	JeminiInput = NewObject<UEnhancedInputComponent>(Pawn, TEXT("CXJeminiInputBindings"));
	JeminiInput->RegisterComponent();
	JeminiInput->BindAction(ChatAction, ETriggerEvent::Started, this, &UCXJeminiInputComponent::OnChatPressed);
	JeminiInput->BindAction(TalkAction, ETriggerEvent::Started, this, &UCXJeminiInputComponent::OnTalkPressed);
	JeminiInput->BindAction(TalkAction, ETriggerEvent::Completed, this, &UCXJeminiInputComponent::OnTalkReleased);
	PC->PushInputComponent(JeminiInput);
	BoundController = PC;
	bInputBound = true;
}

void UCXJeminiInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bInputBound)
	{
		TryBindInput();
	}

	if (Recorder.IsRecording())
	{
		// 쓰러져 있어도 녹음은 계속(제미니 기능 기획 2-8 — 쓰러진 사람도 "살려줘"라고 말할 수 있다)
		Recorder.Poll();
		if (Recorder.GetRecordedSeconds() >= MaxVoiceSeconds - 0.1f)
		{
			// 최대 길이 → 자동 전송
			OnTalkReleased();
		}
	}
}

bool UCXJeminiInputComponent::CanTalkNow() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return false;
	}
	const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	if (!Drone)
	{
		return false;
	}
	// 클라이언트는 게임모드가 없어 복제값(요청 중 / 드론 작업 중)만 본다
	return GetOwner()->HasAuthority() ? Drone->CanAcceptRequest() : !Drone->IsBusy();
}

void UCXJeminiInputComponent::MarkBlocked()
{
	LastBlockedTime = GetWorld()->GetTimeSeconds();
}

void UCXJeminiInputComponent::SetNotice(const FString& Text)
{
	Notice = Text;
	NoticeTime = GetWorld()->GetTimeSeconds();
}

// ---------------------------------------------------------------- 키보드 채팅

void UCXJeminiInputComponent::OnChatPressed()
{
	if (ChatWidget.IsValid() || Recorder.IsRecording())
	{
		return;
	}
	if (!CanTalkNow())
	{
		MarkBlocked();
		return;
	}
	OpenChat();
}

void UCXJeminiInputComponent::OpenChat()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!PC || !LocalPlayer || !Viewport || !FSlateApplication::IsInitialized())
	{
		return;
	}

	TSharedRef<SCXJeminiChatBox> Box = SNew(SCXJeminiChatBox);
	TWeakObjectPtr<UCXJeminiInputComponent> WeakThis(this);
	Box->OnSubmit = [WeakThis](const FString& Text)
	{
		if (UCXJeminiInputComponent* Self = WeakThis.Get())
		{
			Self->HandleChatSubmit(Text);
		}
	};
	Box->OnCancel = [WeakThis]()
	{
		if (UCXJeminiInputComponent* Self = WeakThis.Get())
		{
			Self->CloseChat();
		}
	};
	ChatWidget = Box;
	ChatContainer = Box;
	++OpenChatCount;

	// 자기 분할 칸에만 표시 + 입력 중 이 폰의 게임 입력 끄기(게임 자체는 계속 진행)
	Viewport->AddViewportWidgetForPlayer(LocalPlayer, Box, 50);
	PC->FlushPressedKeys();
	Pawn->DisableInput(PC);
	FSlateApplication::Get().SetKeyboardFocus(Box->GetFocusTarget(), EFocusCause::SetDirectly);
}

void UCXJeminiInputComponent::CloseChat()
{
	if (!ChatWidget.IsValid())
	{
		return;
	}
	TSharedPtr<SWidget> Widget = ChatContainer;
	ChatWidget.Reset();
	ChatContainer.Reset();
	OpenChatCount = FMath::Max(0, OpenChatCount - 1);

	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (Viewport && PC && PC->GetLocalPlayer() && Widget.IsValid())
	{
		Viewport->RemoveViewportWidgetForPlayer(PC->GetLocalPlayer(), Widget.ToSharedRef());
	}
	if (Pawn && PC)
	{
		// 쓰러진 채 채팅했으면 폰 입력은 계속 꺼 둔다(부활할 때 UCXPlayerLifeComponent가 켠다)
		if (UCXPlayerLifeComponent::IsPawnAlive(Pawn))
		{
			Pawn->EnableInput(PC);
		}
		PC->FlushPressedKeys();
	}
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UCXJeminiInputComponent::HandleChatSubmit(const FString& Text)
{
	// Slate 콜백 안에서 위젯을 지우지 않도록 다음 틱에 닫는다
	const FString Trimmed = Text.TrimStartAndEnd();
	TWeakObjectPtr<UCXJeminiInputComponent> WeakThis(this);
	GetWorld()->GetTimerManager().SetTimerForNextTick([WeakThis, Trimmed]()
	{
		if (UCXJeminiInputComponent* Self = WeakThis.Get())
		{
			Self->CloseChat();
			if (!Trimmed.IsEmpty())
			{
				Self->SendText(Trimmed);
			}
		}
	});
}

void UCXJeminiInputComponent::SendText(const FString& Text)
{
	if (!CanTalkNow())
	{
		MarkBlocked();
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		Server_SubmitText_Implementation(Text);
	}
	else
	{
		Server_SubmitText(Text);
	}
}

bool UCXJeminiInputComponent::DebugSubmitText(const FString& Text)
{
	if (!CanTalkNow())
	{
		return false;
	}
	SendText(Text);
	return true;
}

void UCXJeminiInputComponent::Server_SubmitText_Implementation(const FString& Text)
{
	if (ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this))
	{
		Drone->SubmitText(Cast<APawn>(GetOwner()), Text.Left(300));
	}
}

// ---------------------------------------------------------------- 게임패드 음성

void UCXJeminiInputComponent::OnTalkPressed()
{
	if (ChatWidget.IsValid() || Recorder.IsRecording())
	{
		return;
	}
	if (!CanTalkNow())
	{
		MarkBlocked();
		return;
	}
	if (!Recorder.Start())
	{
		SetNotice(FCXJeminiVoiceRecorder::IsAnyRecording() ? TEXT("MIC BUSY") : TEXT("NO MICROPHONE"));
	}
}

void UCXJeminiInputComponent::OnTalkReleased()
{
	if (!Recorder.IsRecording())
	{
		return;
	}
	const TArray<uint8> Wav = Recorder.Stop();
	if (Wav.Num() == 0)
	{
		SetNotice(TEXT("HOLD [Y] AND SPEAK"));
		return;
	}
	SendVoice(Wav);
}

void UCXJeminiInputComponent::SendVoice(const TArray<uint8>& Wav)
{
	if (!CanTalkNow())
	{
		MarkBlocked();
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		if (ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this))
		{
			Drone->SubmitVoice(Cast<APawn>(GetOwner()), Wav);
		}
		return;
	}
	// 원격 클라이언트: RPC 크기 제한 때문에 조각으로 보낸다
	for (int32 Offset = 0; Offset < Wav.Num(); Offset += CXJeminiInput::VoiceChunkBytes)
	{
		const int32 Size = FMath::Min(CXJeminiInput::VoiceChunkBytes, Wav.Num() - Offset);
		TArray<uint8> Chunk(Wav.GetData() + Offset, Size);
		Server_VoiceChunk(Chunk, Offset + Size >= Wav.Num());
	}
}

void UCXJeminiInputComponent::Server_VoiceChunk_Implementation(const TArray<uint8>& Chunk, bool bFinal)
{
	if (PendingVoice.Num() + Chunk.Num() > CXJeminiInput::MaxVoiceBytes)
	{
		PendingVoice.Reset();
		return;
	}
	PendingVoice.Append(Chunk);
	if (bFinal)
	{
		if (ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this))
		{
			Drone->SubmitVoice(Cast<APawn>(GetOwner()), PendingVoice);
		}
		PendingVoice.Reset();
	}
}
