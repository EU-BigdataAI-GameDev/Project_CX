// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXJeminiInputComponent.h"
#include "CXJeminiDrone.h"
#include "CXJeminiClient.h"
#include "Interfaces/IHttpRequest.h"
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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

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
	// 마이크 스트림은 GC 소멸자가 아니라 여기서 닫는다(Docs/Debug_log.md 2026-11-07 PIE 종료 크래시)
	ShutdownVoice();
	if (APlayerController* PC = BoundController.Get(); PC && JeminiInput)
	{
		PC->PopInputComponent(JeminiInput);
	}
	BoundController.Reset();
	Super::EndPlay(EndPlayReason);
}

void UCXJeminiInputComponent::OnUnregister()
{
	ShutdownVoice();
	Super::OnUnregister();
}

void UCXJeminiInputComponent::ShutdownVoice()
{
	Recorder.Shutdown();
	bDebugFeeding = false;
	DebugSamples.Reset();
	ClearLiveCaption();
	FinalPartialWav.Reset();
	if (PartialRequest.IsValid())
	{
		// 취소하면 완료 콜백이 bOk=false로 오지만 세션 번호가 바뀌어 무시된다(약한 포인터라 컴포넌트가 사라져도 안전)
		const FHttpRequestPtr Request = MoveTemp(PartialRequest);
		PartialRequest.Reset();
		Request->OnProcessRequestComplete().Unbind();
		Request->CancelRequest();
	}
	bPartialInFlight = false;
}

bool UCXJeminiInputComponent::DebugStartMicRecording()
{
	OnTalkPressed();
	return Recorder.IsRecording();
}

void UCXJeminiInputComponent::DebugStopMicRecording()
{
	OnTalkReleased();
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
	TickDebugFeed(DeltaTime);
	TickLiveCaption();
}

float UCXJeminiInputComponent::GetRecordSeconds() const
{
	if (bDebugFeeding)
	{
		return DebugSampleRate > 0 ? static_cast<float>(DebugPlayhead / DebugSampleRate) : 0.0f;
	}
	return Recorder.GetRecordedSeconds();
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
	if (ChatWidget.IsValid() || IsRecording())
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
	if (ChatWidget.IsValid() || IsRecording())
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
		return;
	}
	BeginLiveCaption();
}

void UCXJeminiInputComponent::OnTalkReleased()
{
	if (!IsRecording())
	{
		return;
	}
	const TArray<uint8> Wav = StopRecording();
	if (Wav.Num() == 0)
	{
		EndLiveCaptionRecording(false, Wav);
		SetNotice(TEXT("HOLD [Y] AND SPEAK"));
		return;
	}
	const bool bSubmitted = SendVoice(Wav);
	EndLiveCaptionRecording(bSubmitted, Wav);
}

TArray<uint8> UCXJeminiInputComponent::StopRecording()
{
	if (bDebugFeeding)
	{
		bDebugFeeding = false;
		const int32 Frames = FMath::Min(DebugSamples.Num() / DebugChannels, static_cast<int32>(DebugPlayhead));
		if (Frames < DebugSampleRate * 0.3f)
		{
			DebugSamples.Reset();
			return {};
		}
		const TArray<float> Fed(DebugSamples.GetData(), Frames * DebugChannels);
		DebugSamples.Reset();
		return FCXJeminiVoiceRecorder::EncodeWav16kMono(Fed, DebugChannels, DebugSampleRate);
	}
	return Recorder.Stop();
}

bool UCXJeminiInputComponent::SendVoice(const TArray<uint8>& Wav)
{
	if (!CanTalkNow())
	{
		MarkBlocked();
		return false;
	}
	if (GetOwner()->HasAuthority())
	{
		ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
		return Drone && Drone->SubmitVoice(Cast<APawn>(GetOwner()), Wav);
	}
	// 원격 클라이언트: RPC 크기 제한 때문에 조각으로 보낸다
	for (int32 Offset = 0; Offset < Wav.Num(); Offset += CXJeminiInput::VoiceChunkBytes)
	{
		const int32 Size = FMath::Min(CXJeminiInput::VoiceChunkBytes, Wav.Num() - Offset);
		TArray<uint8> Chunk(Wav.GetData() + Offset, Size);
		Server_VoiceChunk(Chunk, Offset + Size >= Wav.Num());
	}
	return true;
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

// ---------------------------------------------------------------- 실시간 자막(말하는 도중 STT, 로컬 전용)

bool UCXJeminiInputComponent::CanUseLiveCaptions() const
{
	// 호스트(로컬 협동 두 폰 모두 여기)에서만: 자막 요청은 이 PC의 Jemini 서버로 간다.
	// 원격 클라이언트는 HUD도 서버 주소도 따로라 최종 Heard 말풍선만 쓴다.
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return bLiveCaptions && Pawn && Pawn->HasAuthority() && Pawn->IsLocallyControlled() && ACXJeminiDrone::Get(this) != nullptr;
}

void UCXJeminiInputComponent::BeginLiveCaption()
{
	++CaptionSession;
	LiveCaption.Reset();
	FinalPartialWav.Reset();
	// 첫 요청은 LiveCaptionMinSeconds가 쌓이자마자(간격 대기 없이)
	LastPartialTime = -100.0;
	CaptionState = CanUseLiveCaptions() ? ECXLiveCaptionState::Listening : ECXLiveCaptionState::None;
	if (CaptionState == ECXLiveCaptionState::None)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		UE_LOG(LogCXJeminiInput, Log, TEXT("Live caption: off (enabled=%d authority=%d local=%d drone=%d)"), bLiveCaptions ? 1 : 0,
			Pawn && Pawn->HasAuthority() ? 1 : 0, Pawn && Pawn->IsLocallyControlled() ? 1 : 0, ACXJeminiDrone::Get(this) ? 1 : 0);
	}
}

void UCXJeminiInputComponent::EndLiveCaptionRecording(bool bSubmitted, const TArray<uint8>& FinalWav)
{
	if (CaptionState != ECXLiveCaptionState::Listening)
	{
		return;
	}
	const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	if (!bSubmitted || !Drone)
	{
		ClearLiveCaption();
		return;
	}
	// 최종 응답(드론 대사)이 올 때까지 마지막 자막 유지 + 전체 WAV로 한 번 더 받아 적기
	CaptionState = ECXLiveCaptionState::Pending;
	PendingStartTime = GetWorld()->GetTimeSeconds();
	PendingDialogueSeq = Drone->GetDialogueSeq();
	if (bPartialInFlight)
	{
		FinalPartialWav = FinalWav;
	}
	else
	{
		SendPartial(FinalWav);
	}
}

void UCXJeminiInputComponent::ClearLiveCaption()
{
	CaptionState = ECXLiveCaptionState::None;
	LiveCaption.Reset();
	FinalPartialWav.Reset();
	// 늦게 오는 응답은 세션 번호로 버린다
	++CaptionSession;
}

TArray<uint8> UCXJeminiInputComponent::SnapshotWav() const
{
	if (bDebugFeeding)
	{
		const int32 MaxFrames = FMath::FloorToInt(MaxVoiceSeconds * DebugSampleRate);
		const int32 Frames = FMath::Min3(DebugSamples.Num() / DebugChannels, static_cast<int32>(DebugPlayhead), MaxFrames);
		if (Frames <= 0)
		{
			return {};
		}
		const TArray<float> Fed(DebugSamples.GetData(), Frames * DebugChannels);
		return FCXJeminiVoiceRecorder::EncodeWav16kMono(Fed, DebugChannels, DebugSampleRate);
	}
	return Recorder.EncodeSnapshot(MaxVoiceSeconds);
}

void UCXJeminiInputComponent::TickLiveCaption()
{
	if (CaptionState == ECXLiveCaptionState::Listening)
	{
		if (!IsRecording())
		{
			// 녹음이 다른 경로로 끝남(EndPlay 등)
			ClearLiveCaption();
			return;
		}
		const double Now = FPlatformTime::Seconds();
		if (!bPartialInFlight && Now - LastPartialTime >= LiveCaptionInterval && GetRecordSeconds() >= LiveCaptionMinSeconds)
		{
			SendPartial(SnapshotWav());
		}
	}
	else if (CaptionState == ECXLiveCaptionState::Pending)
	{
		const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
		const float Waited = GetWorld()->GetTimeSeconds() - PendingStartTime;
		// 드론 대사가 바뀜 = 최종 응답 도착(이제 HUD가 Heard 말풍선을 그린다). 요청이 끝났는데 대사가 없거나 너무 오래면 지운다.
		if (!Drone || Drone->GetDialogueSeq() != PendingDialogueSeq || (!Drone->IsRequestPending() && Waited > 1.0f) || Waited > 15.0f)
		{
			ClearLiveCaption();
		}
	}
}

void UCXJeminiInputComponent::SendPartial(const TArray<uint8>& Wav)
{
	const ACXJeminiDrone* Drone = ACXJeminiDrone::Get(this);
	if (!Drone || Wav.Num() < 64)
	{
		return;
	}
	CXJemini::FEndpoint Endpoint = Drone->GetEndpoint();
	Endpoint.TimeoutSec = LiveCaptionTimeout;
	UE_LOG(LogCXJeminiInput, Log, TEXT("Live caption: send %d bytes (rec %.2fs, state %d)"), Wav.Num(), GetRecordSeconds(), static_cast<int32>(CaptionState));
	bPartialInFlight = true;
	LastPartialTime = FPlatformTime::Seconds();
	++PartialSent;
	const int32 Session = CaptionSession;
	TWeakObjectPtr<UCXJeminiInputComponent> WeakThis(this);
	PartialRequest = CXJemini::SendSttPartial(Endpoint, Wav, FOnCXJeminiPartial::CreateLambda([WeakThis, Session](bool bOk, const FString& Text)
	{
		UCXJeminiInputComponent* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bPartialInFlight = false;
		Self->PartialRequest.Reset();
		if (Session != Self->CaptionSession || Self->CaptionState == ECXLiveCaptionState::None)
		{
			return;
		}
		// null·실패면 이전 글자 유지(처음부터 없으면 아무것도 안 보임 = 기존 동작)
		if (bOk)
		{
			++Self->PartialReceived;
			Self->LiveCaption = Text.Left(60);
		}
		// 뗀 뒤 보낼 전체 WAV가 기다리고 있으면 이어서
		if (Self->CaptionState == ECXLiveCaptionState::Pending && Self->FinalPartialWav.Num() > 0)
		{
			const TArray<uint8> Final = MoveTemp(Self->FinalPartialWav);
			Self->FinalPartialWav.Reset();
			Self->SendPartial(Final);
		}
	}));
}

FString UCXJeminiInputComponent::GetLiveCaptionDebug() const
{
	static const TCHAR* Names[] = { TEXT("None"), TEXT("Listening"), TEXT("Pending") };
	return FString::Printf(TEXT("caption=%s text=\"%s\" sent=%d received=%d inflight=%d rec=%.2fs"), Names[static_cast<int32>(CaptionState)], *LiveCaption,
		PartialSent, PartialReceived, bPartialInFlight ? 1 : 0, GetRecordSeconds());
}

// ---------------------------------------------------------------- 테스트: WAV 파일을 마이크처럼 흘려 넣기

namespace CXJeminiInput
{
	/** RIFF WAV(16bit PCM) → float 인터리브 샘플. */
	static bool DecodeWav16(const TArray<uint8>& Bytes, TArray<float>& OutSamples, int32& OutChannels, int32& OutRate)
	{
		auto Read32 = [&Bytes](int32 At)
		{
			return static_cast<uint32>(Bytes[At]) | (static_cast<uint32>(Bytes[At + 1]) << 8) | (static_cast<uint32>(Bytes[At + 2]) << 16) | (static_cast<uint32>(Bytes[At + 3]) << 24);
		};
		auto Read16 = [&Bytes](int32 At) { return static_cast<uint16>(Bytes[At] | (Bytes[At + 1] << 8)); };
		if (Bytes.Num() < 44 || FMemory::Memcmp(Bytes.GetData(), "RIFF", 4) != 0 || FMemory::Memcmp(Bytes.GetData() + 8, "WAVE", 4) != 0)
		{
			return false;
		}
		int32 Bits = 0;
		OutChannels = 0;
		OutRate = 0;
		int32 At = 12;
		while (At + 8 <= Bytes.Num())
		{
			const uint32 Size = Read32(At + 4);
			const int32 Body = At + 8;
			if (FMemory::Memcmp(Bytes.GetData() + At, "fmt ", 4) == 0 && Body + 16 <= Bytes.Num())
			{
				OutChannels = Read16(Body + 2);
				OutRate = static_cast<int32>(Read32(Body + 4));
				Bits = Read16(Body + 14);
			}
			else if (FMemory::Memcmp(Bytes.GetData() + At, "data", 4) == 0)
			{
				if (Bits != 16 || OutChannels <= 0 || OutRate <= 0)
				{
					return false;
				}
				const int32 Count = FMath::Min(static_cast<int32>(Size), Bytes.Num() - Body) / 2;
				OutSamples.SetNumUninitialized(Count);
				for (int32 i = 0; i < Count; ++i)
				{
					OutSamples[i] = static_cast<int16>(Read16(Body + i * 2)) / 32768.0f;
				}
				return Count > 0;
			}
			At = Body + static_cast<int32>(Size) + static_cast<int32>(Size & 1);
		}
		return false;
	}
}

bool UCXJeminiInputComponent::DebugFeedVoiceFile(const FString& WavPath)
{
	if (ChatWidget.IsValid() || IsRecording())
	{
		return false;
	}
	if (!CanTalkNow())
	{
		MarkBlocked();
		return false;
	}
	TArray<uint8> Bytes;
	const FString Path = FPaths::ConvertRelativePathToFull(WavPath.TrimQuotes());
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || !CXJeminiInput::DecodeWav16(Bytes, DebugSamples, DebugChannels, DebugSampleRate))
	{
		UE_LOG(LogCXJeminiInput, Warning, TEXT("DebugFeedVoiceFile: cannot read 16-bit PCM WAV '%s'"), *Path);
		DebugSamples.Reset();
		return false;
	}
	DebugPlayhead = 0.0;
	bDebugFeeding = true;
	BeginLiveCaption();
	UE_LOG(LogCXJeminiInput, Log, TEXT("DebugFeedVoiceFile: %s (%.2fs, %d ch, %d Hz) live captions %s"), *Path,
		static_cast<float>(DebugSamples.Num()) / (DebugChannels * DebugSampleRate), DebugChannels, DebugSampleRate,
		CaptionState == ECXLiveCaptionState::Listening ? TEXT("on") : TEXT("off"));
	return true;
}

void UCXJeminiInputComponent::TickDebugFeed(float DeltaTime)
{
	if (!bDebugFeeding)
	{
		return;
	}
	// 실시간과 같은 속도로 흘려 넣고, 파일 끝(또는 최대 길이)이면 버튼을 뗀 것처럼 전송
	DebugPlayhead += DeltaTime * DebugSampleRate;
	const double TotalFrames = static_cast<double>(DebugSamples.Num() / DebugChannels);
	if (DebugPlayhead >= TotalFrames || DebugPlayhead / DebugSampleRate >= MaxVoiceSeconds - 0.1f)
	{
		DebugPlayhead = FMath::Min(DebugPlayhead, TotalFrames);
		OnTalkReleased();
	}
}
