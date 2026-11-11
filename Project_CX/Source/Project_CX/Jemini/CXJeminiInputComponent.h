// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXJeminiVoice.h"
#include "HttpFwd.h"
#include "CXJeminiInputComponent.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class APlayerController;
class SCXJeminiChatBox;
class SWidget;

/** 실시간 자막(말하는 도중 STT) 상태 — 로컬 전용(복제 안 함). */
enum class ECXLiveCaptionState : uint8
{
	/** 표시 안 함. */
	None,
	/** 녹음 중: 받아 적은 글자 + 듣는 중 점 애니메이션. */
	Listening,
	/** 버튼을 뗐고 최종 응답(Gemini) 대기 중: 마지막 자막을 흐리게 유지. */
	Pending,
};

/**
 *  제미니에게 말 걸기(.claude/Plans/제미니 AI봇 기획.md 1단계 B5) — 두 플레이어 폰에 런타임으로 붙는다(게임모드 SetPlayerDefaults).
 *
 *  - 키보드: Enter → 채팅 입력창(Slate, 자기 분할 칸 아래) → Enter 전송 / Esc 취소. 입력창이 열린 동안 그 폰의 게임 입력은 꺼진다(게임은 계속).
 *  - 게임패드: Y를 누르는 동안 녹음 → 떼면 전송(최대 MaxVoiceSeconds초에서 자동 전송).
 *    Enhanced Input이 장치별로 로컬 플레이어에 입력을 나누므로 두 키를 모두 바인딩해도 각자 자기 장치로만 동작한다.
 *  - 제미니가 요청 처리 중이거나 드론 작업 중이면(드론 복제값) 새 입력을 막는다(HUD JEMINI BUSY).
 *  - 쓰러진(부활 대기) 플레이어도 말할 수 있다: 바인딩은 폰 InputComponent가 아니라 이 컴포넌트가 만든 입력 컴포넌트를
 *    컨트롤러 입력 스택에 올려 둔다(사망 시 폰 입력이 꺼져도 Enter/Y는 동작). 게임 오버·클리어·이동 중에는 드론이 거절.
 *  - 서버 권위: 텍스트는 Server RPC, 음성은 32KB 조각 RPC로 서버에 보내고 서버의 ACXJeminiDrone이 HTTP를 보낸다.
 *  - 실시간 자막: 녹음하는 동안 LiveCaptionInterval초마다(이전 요청이 끝났을 때만) 지금까지의 WAV를 /stt/partial로 보내
 *    받아 적은 글자를 이 컴포넌트에 둔다 → CoopHUD가 말한 사람 머리 위에 그린다(두 분할 칸 모두 같은 PC라 복제 불필요).
 *    버튼을 떼면 마지막 자막을 Pending으로 두고, 드론의 최종 응답(Heard)이 오면 지운다. 자막 서버가 없거나 null이면 아무것도 안 보인다(기존 동작).
 *    원격 네트워크 클라이언트(호스트가 아닌 PC)는 자막을 쓰지 않는다(최종 Heard 말풍선만).
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UCXJeminiInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXJeminiInputComponent();

	static UCXJeminiInputComponent* EnsureOn(APawn* Pawn);

	/** 어느 플레이어든 채팅 입력창이 열려 있는지(ESC 메뉴 입력 처리기가 Esc를 가로채지 않게). */
	static bool IsAnyChatOpen() { return OpenChatCount > 0; }

	bool IsChatOpen() const { return ChatWidget.IsValid(); }
	bool IsRecording() const { return Recorder.IsRecording() || bDebugFeeding; }
	float GetRecordSeconds() const;

	/** 실시간 자막(로컬 전용). 글자가 있을 때만 HUD가 그린다. */
	ECXLiveCaptionState GetLiveCaptionState() const { return CaptionState; }
	const FString& GetLiveCaption() const { return LiveCaption; }
	bool HasLiveCaption() const { return CaptionState != ECXLiveCaptionState::None && !LiveCaption.IsEmpty(); }

	/**
	 *  테스트: 마이크 대신 WAV 파일(16bit PCM)을 실시간으로 흘려 넣는다 — 녹음·실시간 자막·최종 음성 요청 경로가 실제와 같다.
	 *  콘솔 CXJeminiVoiceFile <경로> [0|1|sister]. 시작했으면 true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Jemini|Debug")
	bool DebugFeedVoiceFile(const FString& WavPath);

	/** 테스트: 자막 상태 한 줄(state / 글자 / 보낸·받은 수). */
	UFUNCTION(BlueprintPure, Category = "Jemini|Debug")
	FString GetLiveCaptionDebug() const;

	/** 테스트: Y를 누른 것처럼 실제 마이크 녹음 시작(PIE 종료 크래시 회귀 확인용). 녹음을 시작했으면 true. */
	UFUNCTION(BlueprintCallable, Category = "Jemini|Debug")
	bool DebugStartMicRecording();

	/** 테스트: Y를 뗀 것처럼 녹음 종료 → 전송(짧으면 버림). */
	UFUNCTION(BlueprintCallable, Category = "Jemini|Debug")
	void DebugStopMicRecording();
	float GetMaxVoiceSeconds() const { return MaxVoiceSeconds; }

	/** 마지막으로 입력이 막힌 시각(HUD "JEMINI BUSY" 표시). */
	float GetLastBlockedTime() const { return LastBlockedTime; }
	/** 마지막 실패 문구(마이크 없음 등)와 시각. */
	const FString& GetNotice() const { return Notice; }
	float GetNoticeTime() const { return NoticeTime; }

	/** 지금 이 플레이어가 제미니에게 말할 수 있는지(드론이 요청을 받을 수 있음 — 쓰러져 있어도 된다). */
	bool CanTalkNow() const;

	/** 테스트: 채팅을 입력한 것처럼 전송(입력창 생략). */
	UFUNCTION(BlueprintCallable, Category = "Jemini|Debug")
	bool DebugSubmitText(const FString& Text);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** EndPlay 없이 등록 해제되는 경우(PIE 종료 순서 등)에도 마이크 스트림·HTTP를 정리한다. */
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditDefaultsOnly, Category = "Jemini")
	float MaxVoiceSeconds = 8.0f;

	/** 실시간 자막 켜기(서버 /stt/partial). */
	UPROPERTY(EditDefaultsOnly, Category = "Jemini|LiveCaption")
	bool bLiveCaptions = true;

	/** 자막 요청 간격(초). 이전 요청이 아직 안 끝났으면 기다린다(동시에 1개). */
	UPROPERTY(EditDefaultsOnly, Category = "Jemini|LiveCaption")
	float LiveCaptionInterval = 0.6f;

	/** 이 길이(초)부터 첫 자막 요청. */
	UPROPERTY(EditDefaultsOnly, Category = "Jemini|LiveCaption")
	float LiveCaptionMinSeconds = 0.4f;

	/** 자막 요청 HTTP 타임아웃(초). */
	UPROPERTY(EditDefaultsOnly, Category = "Jemini|LiveCaption")
	float LiveCaptionTimeout = 3.0f;

private:
	// ---- 실시간 자막
	bool CanUseLiveCaptions() const;
	void BeginLiveCaption();
	/** 녹음 종료. bSubmitted = 최종 음성 요청을 드론이 받음 → Pending(FinalWav로 마지막 자막 1회), 아니면 지운다. */
	void EndLiveCaptionRecording(bool bSubmitted, const TArray<uint8>& FinalWav);
	void ClearLiveCaption();
	void TickLiveCaption();
	void SendPartial(const TArray<uint8>& Wav);
	/** 지금까지 녹음된 WAV(마이크 또는 테스트 파일). */
	TArray<uint8> SnapshotWav() const;
	void TickDebugFeed(float DeltaTime);
	/** 녹음·자막·진행 중 자막 HTTP를 모두 정리(EndPlay/OnUnregister, 여러 번 불러도 안전). */
	void ShutdownVoice();
	/** 녹음 종료 → WAV(테스트 파일이면 흘려 넣은 만큼). */
	TArray<uint8> StopRecording();

	void BuildInputAssets();
	void TryBindInput();

	void OnChatPressed();
	void OnTalkPressed();
	void OnTalkReleased();

	void OpenChat();
	void CloseChat();
	void HandleChatSubmit(const FString& Text);

	void SendText(const FString& Text);
	/** 드론에 보냈으면 true(원격 클라이언트는 RPC를 보냈으면 true). */
	bool SendVoice(const TArray<uint8>& Wav);
	void SetNotice(const FString& Text);
	void MarkBlocked();

	UFUNCTION(Server, Reliable)
	void Server_SubmitText(const FString& Text);

	UFUNCTION(Server, Reliable)
	void Server_VoiceChunk(const TArray<uint8>& Chunk, bool bFinal);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ChatAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TalkAction;

	/** 컨트롤러 입력 스택에 올린 제미니 전용 입력 컴포넌트(폰 입력이 꺼져도 유지). */
	UPROPERTY(Transient)
	TObjectPtr<UEnhancedInputComponent> JeminiInput;

	TWeakObjectPtr<APlayerController> BoundController;

	bool bInputBound = false;

	TSharedPtr<SCXJeminiChatBox> ChatWidget;
	TSharedPtr<SWidget> ChatContainer;
	/** 채팅창을 전체 화면 위(분할 경계)에 올렸는지 — 닫을 때 같은 방식으로 뺀다. */
	bool bChatGlobal = false;

	FCXJeminiVoiceRecorder Recorder;

	/** 서버: 원격 클라이언트에서 받는 음성 조각. */
	TArray<uint8> PendingVoice;

	float LastBlockedTime = -100.0f;
	FString Notice;
	float NoticeTime = -100.0f;

	// ---- 실시간 자막(로컬)
	ECXLiveCaptionState CaptionState = ECXLiveCaptionState::None;
	FString LiveCaption;
	/** 녹음마다 증가 — 이전 녹음의 늦은 응답은 버린다. */
	int32 CaptionSession = 0;
	bool bPartialInFlight = false;
	/** 진행 중인 자막 요청(EndPlay에서 취소). */
	FHttpRequestPtr PartialRequest;
	double LastPartialTime = -100.0;
	/** 뗀 뒤 보낼 마지막 자막 WAV(요청 중이면 끝난 뒤 보낸다). */
	TArray<uint8> FinalPartialWav;
	float PendingStartTime = 0.0f;
	/** 뗀 순간 드론 DialogueSeq — 바뀌면 최종 응답 도착. */
	int32 PendingDialogueSeq = 0;
	int32 PartialSent = 0;
	int32 PartialReceived = 0;

	// ---- 테스트 파일 흘려 넣기(DebugFeedVoiceFile)
	bool bDebugFeeding = false;
	TArray<float> DebugSamples;
	int32 DebugChannels = 1;
	int32 DebugSampleRate = 16000;
	/** 지금까지 흘려 넣은 프레임 수. */
	double DebugPlayhead = 0.0;

	static int32 OpenChatCount;
};
