// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXJeminiVoice.h"
#include "AudioCaptureCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXJeminiVoice, Log, All);

int32 FCXJeminiVoiceRecorder::ActiveRecorders = 0;

FCXJeminiVoiceRecorder::FCXJeminiVoiceRecorder() = default;

FCXJeminiVoiceRecorder::~FCXJeminiVoiceRecorder()
{
	Cancel();
}

bool FCXJeminiVoiceRecorder::Start()
{
	if (bRecording || IsAnyRecording())
	{
		return false;
	}
	if (!Capture)
	{
		Capture = MakeUnique<Audio::FAudioCaptureSynth>();
	}

	Audio::FCaptureDeviceInfo Info;
	if (!Capture->GetDefaultCaptureDeviceInfo(Info) || Info.PreferredSampleRate <= 0)
	{
		UE_LOG(LogCXJeminiVoice, Warning, TEXT("JeminiVoice: no default capture device (microphone)"));
		return false;
	}
	NumChannels = FMath::Max(1, Info.InputChannels);
	SampleRate = Info.PreferredSampleRate;

	if (!Capture->IsStreamOpen() && !Capture->OpenDefaultStream())
	{
		UE_LOG(LogCXJeminiVoice, Warning, TEXT("JeminiVoice: failed to open capture stream (%s)"), *Info.DeviceName);
		return false;
	}
	Samples.Reset();
	// 열려 있던 스트림에 남은 이전 데이터는 버린다
	TArray<float> Discard;
	Capture->GetAudioData(Discard);
	if (!Capture->StartCapturing())
	{
		return false;
	}
	bRecording = true;
	++ActiveRecorders;
	UE_LOG(LogCXJeminiVoice, Log, TEXT("JeminiVoice: recording from %s (%d ch, %d Hz)"), *Info.DeviceName, NumChannels, SampleRate);
	return true;
}

void FCXJeminiVoiceRecorder::Poll()
{
	if (bRecording && Capture)
	{
		Capture->GetAudioData(Samples);
	}
}

float FCXJeminiVoiceRecorder::GetRecordedSeconds() const
{
	return SampleRate > 0 ? static_cast<float>(Samples.Num()) / (NumChannels * SampleRate) : 0.0f;
}

TArray<uint8> FCXJeminiVoiceRecorder::Stop(float MinSeconds)
{
	if (!bRecording)
	{
		return {};
	}
	Poll();
	Capture->StopCapturing();
	bRecording = false;
	--ActiveRecorders;

	const float Seconds = GetRecordedSeconds();
	if (Seconds < MinSeconds)
	{
		UE_LOG(LogCXJeminiVoice, Log, TEXT("JeminiVoice: too short (%.2fs), discarded"), Seconds);
		Samples.Reset();
		return {};
	}
	TArray<uint8> Wav = EncodeWav16kMono(Samples, NumChannels, SampleRate);
	UE_LOG(LogCXJeminiVoice, Log, TEXT("JeminiVoice: %.2fs -> %d bytes WAV"), Seconds, Wav.Num());
	Samples.Reset();
	return Wav;
}

void FCXJeminiVoiceRecorder::Cancel()
{
	if (bRecording)
	{
		if (Capture)
		{
			Capture->AbortCapturing();
		}
		bRecording = false;
		--ActiveRecorders;
	}
	Samples.Reset();
}

TArray<uint8> FCXJeminiVoiceRecorder::EncodeWav16kMono(const TArray<float>& Interleaved, int32 InNumChannels, int32 InSampleRate)
{
	const int32 Channels = FMath::Max(1, InNumChannels);
	const int32 Frames = Interleaved.Num() / Channels;
	if (Frames <= 0 || InSampleRate <= 0)
	{
		return {};
	}

	// 1) 채널 평균 → 모노
	TArray<float> Mono;
	Mono.SetNumUninitialized(Frames);
	for (int32 f = 0; f < Frames; ++f)
	{
		float Sum = 0.0f;
		for (int32 c = 0; c < Channels; ++c)
		{
			Sum += Interleaved[f * Channels + c];
		}
		Mono[f] = Sum / Channels;
	}

	// 2) 선형 보간 리샘플(음성 인식용이라 충분). 다운샘플 시 간단한 구간 평균으로 앨리어싱 완화.
	const double Ratio = static_cast<double>(InSampleRate) / TargetSampleRate;
	const int32 OutFrames = FMath::Max(1, static_cast<int32>(Frames / Ratio));
	TArray<int16> Pcm;
	Pcm.SetNumUninitialized(OutFrames);
	for (int32 i = 0; i < OutFrames; ++i)
	{
		const double Src = i * Ratio;
		float Value;
		if (Ratio > 1.0)
		{
			const int32 Begin = static_cast<int32>(Src);
			const int32 End = FMath::Min(Frames, FMath::Max(Begin + 1, static_cast<int32>(Src + Ratio)));
			float Sum = 0.0f;
			for (int32 k = Begin; k < End; ++k)
			{
				Sum += Mono[k];
			}
			Value = Sum / (End - Begin);
		}
		else
		{
			const int32 I0 = FMath::Min(static_cast<int32>(Src), Frames - 1);
			const int32 I1 = FMath::Min(I0 + 1, Frames - 1);
			const float T = static_cast<float>(Src - I0);
			Value = FMath::Lerp(Mono[I0], Mono[I1], T);
		}
		Pcm[i] = static_cast<int16>(FMath::Clamp(Value, -1.0f, 1.0f) * 32767.0f);
	}

	// 3) RIFF/WAVE 헤더(44바이트) + PCM
	const uint32 DataBytes = Pcm.Num() * sizeof(int16);
	TArray<uint8> Wav;
	Wav.Reserve(44 + DataBytes);
	auto Put32 = [&Wav](uint32 V) { for (int32 b = 0; b < 4; ++b) { Wav.Add(static_cast<uint8>((V >> (8 * b)) & 0xFF)); } };
	auto Put16 = [&Wav](uint16 V) { Wav.Add(static_cast<uint8>(V & 0xFF)); Wav.Add(static_cast<uint8>(V >> 8)); };
	auto PutTag = [&Wav](const char* Tag) { for (int32 b = 0; b < 4; ++b) { Wav.Add(static_cast<uint8>(Tag[b])); } };

	PutTag("RIFF");
	Put32(36 + DataBytes);
	PutTag("WAVE");
	PutTag("fmt ");
	Put32(16);					// PCM fmt 청크 크기
	Put16(1);					// PCM
	Put16(1);					// 모노
	Put32(TargetSampleRate);
	Put32(TargetSampleRate * sizeof(int16));
	Put16(sizeof(int16));		// block align
	Put16(16);					// bits per sample
	PutTag("data");
	Put32(DataBytes);
	Wav.Append(reinterpret_cast<const uint8*>(Pcm.GetData()), DataBytes);
	return Wav;
}
