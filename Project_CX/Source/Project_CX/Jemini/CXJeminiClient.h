// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpFwd.h"
#include "CXJeminiRuleTypes.h"

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
	/** 기능 C 힌트용 퍼즐 종류: passcode / memory / freq / circuit / arrow(적). 모르면 빈 값(JSON 생략). 정답은 보내지 않는다. */
	FString PuzzleKind;

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

	/** 지금 보관 중인 규칙(기능 A) — JSON에는 id·label·action·held만(active_rules). */
	TArray<FCXJeminiRule> ActiveRules;

	/** 기능 C: 대답을 기다리는 작전 제안(pending_proposal, label·action만). */
	TArray<FCXJeminiRule> PendingProposal;

	/** 기능 C: 맵에 아직 안 열린 WARDEN이 있으면 true → JSON warden {evidence, last_reason, near}. */
	bool bHasWarden = false;
	/** 팀이 얻은 증거 id("E1"~"E3")만. */
	TArray<FString> WardenEvidence;
	/** 마지막 설득 거절 사유(없으면 빈 값). */
	FString WardenLastReason;
	/** 플레이어가 WARDEN 단말기 근처(대사가 보이는 거리). */
	bool bWardenNear = false;
};

/** 서버 응답. */
struct FCXJeminiResponse
{
	/** ok / rejected / chat / fallback / rule / advice */
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

	/** status "rule"일 때: register / cancel / list. */
	FString RuleOp;
	/** register: 서버가 검증·정규화한 규칙(Id는 UE가 붙인다). */
	bool bHasRule = false;
	FCXJeminiRule Rule;
	/** cancel: 지울 규칙 id(0 = 전부). */
	int32 RuleId = 0;

	/** status "advice"(기능 C)일 때: 서버가 검증한 제안 규칙(0~2개). 등록하지 않고 대답 대기로 보관한다. */
	TArray<FCXJeminiRule> Proposal;

	bool IsOk() const { return Status == TEXT("ok") && !CommandName.IsEmpty(); }
	bool IsRule() const { return Status == TEXT("rule"); }
	bool IsAdvice() const { return Status == TEXT("advice"); }
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
	 *  POST /validate — 규칙 발동 직전 재검증(LLM 없음). 응답은 /command와 같은 모양(status ok/rejected, command, battery_after, reason, dialogue)
	 *  이라 FCXJeminiResponse로 받는다. Who = Shield·QuickReboot 대상(비우면 생략).
	 */
	void SendValidate(const FEndpoint& Endpoint, const FString& Action, const FString& Who, int32 ReserveBattery, const FCXJeminiState& State, FOnCXJeminiResponse OnDone);

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
