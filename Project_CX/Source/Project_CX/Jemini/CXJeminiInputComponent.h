// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXJeminiVoice.h"
#include "CXJeminiInputComponent.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputComponent;
class APlayerController;
class SCXJeminiChatBox;
class SWidget;

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
	bool IsRecording() const { return Recorder.IsRecording(); }
	float GetRecordSeconds() const { return Recorder.GetRecordedSeconds(); }
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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditDefaultsOnly, Category = "Jemini")
	float MaxVoiceSeconds = 8.0f;

private:
	void BuildInputAssets();
	void TryBindInput();

	void OnChatPressed();
	void OnTalkPressed();
	void OnTalkReleased();

	void OpenChat();
	void CloseChat();
	void HandleChatSubmit(const FString& Text);

	void SendText(const FString& Text);
	void SendVoice(const TArray<uint8>& Wav);
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

	FCXJeminiVoiceRecorder Recorder;

	/** 서버: 원격 클라이언트에서 받는 음성 조각. */
	TArray<uint8> PendingVoice;

	float LastBlockedTime = -100.0f;
	FString Notice;
	float NoticeTime = -100.0f;

	static int32 OpenChatCount;
};
