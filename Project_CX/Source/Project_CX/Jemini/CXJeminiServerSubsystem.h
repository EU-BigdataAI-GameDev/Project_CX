// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "CXJeminiServerSubsystem.generated.h"

/**
 *  제미니 LLM 서버 자동 실행 — 게임을 켜면 서버도 같이 켜지고, 게임을 끄면 같이 꺼진다.
 *
 *  - 서버 주소(DefaultGame.ini [/Script/Project_CX.CXJeminiDrone] ServerUrl)가 이 PC(127.0.0.1/localhost)일 때만 동작.
 *    원격 주소(허깅페이스 Space)면 아무것도 하지 않는다.
 *  - 시작 시 /health 확인 → 이미 켜져 있으면(직접 띄운 서버 등) 그대로 쓴다. 꺼져 있으면 실행:
 *      1) 패키징한 게임: 게임 폴더의 Jemini\jemini-server.exe (Jemini\tools\build_exe.bat로 만든 것)
 *      2) 개발(에디터): 저장소 Jemini\.venv\Scripts\python.exe run_server.py
 *  - 내가 띄운 서버만 게임 종료(게임 인스턴스 종료) 때 프로세스 트리째 끈다.
 *  - 서버가 켜지는 데 몇 초 걸린다 — 그 사이 요청은 "신호가 끊겼어요" 대사(fallback)로 끝난다.
 *
 *  설정: DefaultGame.ini [/Script/Project_CX.CXJeminiServerSubsystem] bAutoStartServer
 */
UCLASS(Config = Game)
class UCXJeminiServerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 지금 서버 상태(로그·디버그). */
	enum class EState : uint8 { Disabled, Checking, AlreadyRunning, Launched, LaunchFailed, Remote };
	EState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Jemini|Server")
	bool DidLaunchServer() const { return ServerProcess.IsValid(); }

protected:
	/** false면 서버를 띄우지 않는다(직접 띄우거나 원격 서버만 쓸 때). */
	UPROPERTY(Config)
	bool bAutoStartServer = true;

	/** /health 확인 타임아웃(초). */
	UPROPERTY(Config)
	float HealthCheckTimeout = 1.5f;

private:
	void CheckThenLaunch(const FString& BaseUrl, const FString& Host, int32 Port);
	bool LaunchServer(const FString& Host, int32 Port);
	static bool ParseLocalUrl(const FString& Url, FString& OutHost, int32& OutPort);

	FProcHandle ServerProcess;
	EState State = EState::Disabled;
	bool bShuttingDown = false;
};
