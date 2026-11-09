// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace Audio { class FAudioCaptureSynth; }

/**
 *  게임패드 푸시투토크 녹음(.claude/Plans/제미니 AI봇 기획.md 1단계 B5).
 *
 *  기본 마이크(AudioCapture 플러그인 백엔드)에서 float 샘플을 받아 모은 뒤, Stop()에서 16kHz 모노 16bit WAV로 바꾼다.
 *  한 PC에 마이크가 하나라 녹음기는 동시에 하나만 쓴다(IsAnyRecording).
 *  게임 스레드에서 Start → 매 틱 Poll → Stop 순으로 호출한다.
 */
class FCXJeminiVoiceRecorder
{
public:
	FCXJeminiVoiceRecorder();
	~FCXJeminiVoiceRecorder();

	/** 녹음 시작. 마이크가 없거나 다른 녹음 중이면 false. */
	bool Start();

	/** 캡처 버퍼를 비워 내부에 모은다(매 틱). */
	void Poll();

	/** 녹음 종료 → WAV 바이트. 너무 짧거나(MinSeconds 미만) 실패하면 빈 배열. */
	TArray<uint8> Stop(float MinSeconds = 0.3f);

	/** 취소(데이터 버림, 스트림 닫기). 녹음 중이 아니어도 안전. */
	void Cancel();

	/**
	 *  완전 종료: 취소 + 캡처 스트림 닫기 + 캡처 객체 파괴. 소유자 EndPlay/OnUnregister에서 부른다(여러 번 불러도 안전).
	 *  열린 스트림을 GC 소멸자에서 파괴하면 크래시하므로(Docs/Debug_log.md 2026-11-07) 소멸자에 맡기지 않는다.
	 */
	void Shutdown();

	bool IsRecording() const { return bRecording; }
	float GetRecordedSeconds() const;

	/** 녹음을 멈추지 않고 지금까지(처음부터, 최대 MaxSeconds초)를 WAV로 — 실시간 자막(/stt/partial)용. */
	TArray<uint8> EncodeSnapshot(float MaxSeconds) const;

	/** 지금 어느 녹음기든 녹음 중인지(마이크 하나). */
	static bool IsAnyRecording() { return ActiveRecorders > 0; }

	/** 모노 float → 16kHz 모노 16bit PCM WAV. 테스트용으로 공개. */
	static TArray<uint8> EncodeWav16kMono(const TArray<float>& Interleaved, int32 NumChannels, int32 SampleRate);

	static constexpr int32 TargetSampleRate = 16000;

private:
	/** 열린 캡처 스트림을 중단·닫기(캡처 스레드 종료). 닫혀 있으면 아무것도 안 함. */
	void CloseStream();

	TUniquePtr<Audio::FAudioCaptureSynth> Capture;
	TArray<float> Samples;
	int32 NumChannels = 1;
	int32 SampleRate = 48000;
	bool bRecording = false;

	static int32 ActiveRecorders;
};
