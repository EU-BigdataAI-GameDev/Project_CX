// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CXJeminiClient.h"

/**
 *  WARDEN 설득 HTTP(POST /persuade, /persuade/voice) — Docs/Jemini/02_요구사항_명세서.md "기능 B".
 *  제미니 서버와 같은 주소·토큰(ACXJeminiDrone::GetEndpoint). 실패는 Status "fallback"으로 콜백한다.
 */
struct FCXWardenResponse
{
	/** solved / rejected / cooldown / fallback */
	FString Status = TEXT("fallback");
	bool bSolved = false;
	/** WARDEN 대사(60자 이내). */
	FString Reply;
	FString Reason;
	/** 음성일 때 들은 문장. */
	FString Heard;
	bool bMock = false;
	/** 체크리스트(로그용) "reg fail cover link | unsup inj demand" 비트 문자열. */
	FString ChecklistDebug;
	/** 서버가 누적 인정한 증거 id(이번 발언까지). bHasCredited=false면 응답에 없던 것(연결 실패 등) — 기존 값 유지. */
	TArray<FString> Credited;
	bool bHasCredited = false;
};

DECLARE_DELEGATE_OneParam(FOnCXWardenResponse, const FCXWardenResponse& /*Response*/);

namespace CXWardenClient
{
	/** POST /persuade {text, evidence, session} */
	void SendText(const CXJemini::FEndpoint& Endpoint, const FString& Text, const TArray<FString>& Evidence, const TArray<FString>& Credited, const FString& Session, FOnCXWardenResponse OnDone);

	/** POST /persuade/voice (multipart: audio WAV + meta JSON {evidence, session}) */
	void SendVoice(const CXJemini::FEndpoint& Endpoint, const TArray<uint8>& Wav, const TArray<FString>& Evidence, const TArray<FString>& Credited, const FString& Session, FOnCXWardenResponse OnDone);

	bool ParseResponse(const FString& Body, FCXWardenResponse& Out);
}
