// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpFwd.h"

/**
 *  제미니 LLM 서버(Jemini/ FastAPI) HTTP 클라이언트 — .claude/Plans/제미니 AI봇 기획.md 1단계 B1.
 *  요청/응답 JSON은 Docs/Jemini/02_요구사항_명세서.md 4절과 1:1.
 *
 *  - 비동기(FHttpModule). 완료 콜백은 게임 스레드에서 온다.
 *  - 네트워크 실패·타임아웃·파싱 실패는 Status "fallback" 응답으로 바꿔 콜백한다(게임은 항상 대사를 받는다).
 */

/** 서버로 보내는 게임 상태(Docs/Jemini/02_요구사항_명세서.md 4절 v2). */
struct FCXJeminiState
{
	int32 BrotherAmmo = 0;
	int32 BrotherAmmoMax = 30;
	bool bBrotherReloading = false;
	int32 BrotherHp = 100;
	int32 BrotherHpMax = 100;
	bool bBrotherAlive = true;
	float BrotherRespawnRemaining = 0.0f;
	float BrotherShieldRemaining = 0.0f;
	float DamageBoostRemaining = 0.0f;

	int32 SisterHp = 100;
	int32 SisterHpMax = 100;
	bool bSisterAlive = true;
	float SisterRespawnRemaining = 0.0f;
	float SisterShieldRemaining = 0.0f;
	/** 퍼즐 중이 아니면 false → JSON에 null. */
	bool bHasPuzzle = false;
	/** "terminal" 또는 "enemy". */
	FString PuzzleType;
	float PuzzleProgress = 0.0f;

	/** 살아 있는 플레이어에게서 15m 안: 살아 있는 적(그로기 포함) / 해킹 가능 / 움직이는(그로기 아님). */
	int32 EnemyAliveCount = 0;
	int32 EnemyHackableCount = 0;
	int32 EnemyActiveCount = 0;
	/** 가장 가까운 살아 있는 적까지 m. 음수면 null. */
	float EnemyNearestDist = -1.0f;
	int32 TerminalLockedCount = 0;

	/** 말한 사람 "brother" / "sister". */
	FString Speaker = TEXT("brother");
	bool bDroneBusy = false;
	int32 Battery = 100;
};

/** 서버 응답. */
struct FCXJeminiResponse
{
	/** ok / rejected / chat / fallback */
	FString Status = TEXT("fallback");
	/** 줄바꿈(
)이 있으면 줄마다 다음 말풍선. */
	FString Dialogue;
	/** ok일 때만: ReloadSupport / HackTerminal / HackEnemy / PulseAttack / DamageBoost / HealSupport / Decoy / QuickReboot / Shield */
	FString CommandName;
	/** brother / sister / enemy / enemies / both */
	FString CommandTarget;
	/** ok면 실행되면 남을 값(효과가 실제로 실행될 때만 반영). */
	int32 BatteryAfter = 0;
	FString Reason;
	FString Heard;
	/** LLM 대신 서버 mock 키워드 판별로 처리됨(mock 모드 또는 모든 모델 한도 초과). */
	bool bMock = false;

	bool IsOk() const { return Status == TEXT("ok") && !CommandName.IsEmpty(); }
};

DECLARE_DELEGATE_OneParam(FOnCXJeminiResponse, const FCXJeminiResponse& /*Response*/);

/** 실시간 자막 응답. bOk = 받아 적은 글자가 있음(서버 text가 null·실패·연결 끊김이면 false). */
DECLARE_DELEGATE_TwoParams(FOnCXJeminiPartial, bool /*bOk*/, const FString& /*Text*/);

namespace CXJemini
{
	struct FEndpoint
	{
		FString BaseUrl;
		FString ApiToken;
		float TimeoutSec = 12.0f;
	};

	/** POST /command */
	void SendText(const FEndpoint& Endpoint, const FString& Text, const FCXJeminiState& State, FOnCXJeminiResponse OnDone);

	/** POST /command/voice (multipart: audio WAV + state JSON) */
	void SendVoice(const FEndpoint& Endpoint, const TArray<uint8>& Wav, const FCXJeminiState& State, FOnCXJeminiResponse OnDone);

	/**
	 *  POST /stt/partial (multipart: audio WAV) — 말하는 도중 실시간 자막(서버 로컬 faster-whisper). 명령은 실행하지 않는다.
	 *  응답 {"text": str|null, "ms": int}. 실패는 조용히 bOk=false(로그는 Verbose).
	 *  반환값으로 요청을 취소할 수 있다(소유자 EndPlay — 취소해도 콜백은 bOk=false로 한 번 온다).
	 */
	FHttpRequestPtr SendSttPartial(const FEndpoint& Endpoint, const TArray<uint8>& Wav, FOnCXJeminiPartial OnDone);

	/** state 객체 JSON 문자열. */
	FString StateToJson(const FCXJeminiState& State);

	/** 응답 본문 파싱. 실패하면 false. */
	bool ParseResponse(const FString& Body, FCXJeminiResponse& Out);

	/** 연결 실패 등 클라이언트 쪽 대체 응답(배터리 그대로). */
	FCXJeminiResponse MakeFallback(int32 Battery, const FString& Reason);
}
