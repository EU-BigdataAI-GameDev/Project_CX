// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXJeminiServerSubsystem.h"
#include "CXJeminiDrone.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXJeminiServer, Log, All);

void UCXJeminiServerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!bAutoStartServer || IsRunningCommandlet() || IsRunningDedicatedServer())
	{
		State = EState::Disabled;
		return;
	}

	const FString BaseUrl = ACXJeminiDrone::GetConfiguredServerUrl();
	FString Host;
	int32 Port = 0;
	if (!ParseLocalUrl(BaseUrl, Host, Port))
	{
		// 원격 서버(허깅페이스 Space 등)는 띄울 필요가 없다
		State = EState::Remote;
		UE_LOG(LogCXJeminiServer, Log, TEXT("JeminiServer: remote server %s — not launching locally"), *BaseUrl);
		return;
	}
	CheckThenLaunch(BaseUrl, Host, Port);
}

void UCXJeminiServerSubsystem::Deinitialize()
{
	bShuttingDown = true;
	FTSTicker::GetCoreTicker().RemoveTicker(VerifyHandle);
	if (ServerProcess.IsValid())
	{
		// 내가 띄운 서버만 끈다(직접 띄운 서버는 건드리지 않음)
		if (FPlatformProcess::IsProcRunning(ServerProcess))
		{
			FPlatformProcess::TerminateProc(ServerProcess, true);
		}
		FPlatformProcess::CloseProc(ServerProcess);
		UE_LOG(LogCXJeminiServer, Log, TEXT("JeminiServer: stopped the server this game started"));
	}
	Super::Deinitialize();
}

void UCXJeminiServerSubsystem::VerifyLaunched(const FString& Host, int32 Port)
{
	if (bShuttingDown || !ServerProcess.IsValid() || FPlatformProcess::IsProcRunning(ServerProcess))
	{
		return;
	}
	UE_LOG(LogCXJeminiServer, Warning, TEXT("JeminiServer: %s exited shortly after start — trying the next candidate"), *LaunchedCandidate);
	FailedCandidates.AddUnique(LaunchedCandidate);
	FPlatformProcess::CloseProc(ServerProcess);
	ServerProcess.Reset();
	State = LaunchServer(Host, Port) ? EState::Launched : EState::LaunchFailed;
}

bool UCXJeminiServerSubsystem::ParseLocalUrl(const FString& Url, FString& OutHost, int32& OutPort)
{
	// http://127.0.0.1:8000 → 호스트 127.0.0.1, 포트 8000
	FString Rest = Url;
	int32 SchemeEnd = Rest.Find(TEXT("://"));
	if (SchemeEnd == INDEX_NONE)
	{
		return false;
	}
	const bool bHttps = Rest.StartsWith(TEXT("https"), ESearchCase::IgnoreCase);
	Rest = Rest.Mid(SchemeEnd + 3);
	int32 Slash = INDEX_NONE;
	if (Rest.FindChar(TEXT('/'), Slash))
	{
		Rest = Rest.Left(Slash);
	}
	FString HostPart = Rest;
	int32 Port = bHttps ? 443 : 80;
	int32 Colon = INDEX_NONE;
	if (Rest.FindLastChar(TEXT(':'), Colon))
	{
		HostPart = Rest.Left(Colon);
		Port = FCString::Atoi(*Rest.Mid(Colon + 1));
	}
	const bool bLocal = HostPart.Equals(TEXT("127.0.0.1")) || HostPart.Equals(TEXT("localhost"), ESearchCase::IgnoreCase);
	if (!bLocal || Port <= 0 || bHttps)
	{
		return false;
	}
	OutHost = HostPart;
	OutPort = Port;
	return true;
}

void UCXJeminiServerSubsystem::CheckThenLaunch(const FString& BaseUrl, const FString& Host, int32 Port)
{
	State = EState::Checking;
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(BaseUrl + TEXT("/health"));
	Request->SetVerb(TEXT("GET"));
	Request->SetTimeout(FMath::Max(0.3f, HealthCheckTimeout));

	TWeakObjectPtr<UCXJeminiServerSubsystem> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda([WeakThis, Host, Port](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		UCXJeminiServerSubsystem* Self = WeakThis.Get();
		if (!Self || Self->bShuttingDown)
		{
			return;
		}
		if (bConnected && Response.IsValid() && Response->GetResponseCode() == 200)
		{
			Self->State = EState::AlreadyRunning;
			UE_LOG(LogCXJeminiServer, Log, TEXT("JeminiServer: already running on %s:%d — using it"), *Host, Port);
			return;
		}
		Self->State = Self->LaunchServer(Host, Port) ? EState::Launched : EState::LaunchFailed;
	});
	Request->ProcessRequest();
}

bool UCXJeminiServerSubsystem::LaunchServer(const FString& Host, int32 Port)
{
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString RootDir = FPaths::ConvertRelativePathToFull(FPaths::RootDir());
	const FString RepoJemini = FPaths::ConvertRelativePathToFull(FPaths::Combine(ProjectDir, TEXT("../Jemini")));

	struct FCandidate
	{
		FString Exe;
		FString Args;
		FString WorkDir;
	};
	const FString ServerArgs = FString::Printf(TEXT("--host %s --port %d"), *Host, Port);
	const FCandidate PackagedExeA{ FPaths::Combine(RootDir, TEXT("Jemini/jemini-server.exe")), ServerArgs, FPaths::Combine(RootDir, TEXT("Jemini")) };
	const FCandidate PackagedExeB{ FPaths::Combine(ProjectDir, TEXT("Jemini/jemini-server.exe")), ServerArgs, FPaths::Combine(ProjectDir, TEXT("Jemini")) };
	const FCandidate DevPython{ FPaths::Combine(RepoJemini, TEXT(".venv/Scripts/python.exe")), FString::Printf(TEXT("run_server.py %s"), *ServerArgs), RepoJemini };
	const FCandidate DevExe{ FPaths::Combine(RepoJemini, TEXT("dist/jemini-server/jemini-server.exe")), ServerArgs, FPaths::Combine(RepoJemini, TEXT("dist/jemini-server")) };

	// 에디터(개발)는 코드 수정이 바로 반영되는 파이썬을 먼저, 패키징한 게임은 묶은 exe를 먼저
	TArray<FCandidate> Candidates;
	if (GIsEditor)
	{
		Candidates = { DevPython, DevExe, PackagedExeA, PackagedExeB };
	}
	else
	{
		Candidates = { PackagedExeA, PackagedExeB, DevExe, DevPython };
	}

	for (const FCandidate& Candidate : Candidates)
	{
		if (FailedCandidates.Contains(Candidate.Exe) || !IFileManager::Get().FileExists(*Candidate.Exe))
		{
			continue;
		}
		uint32 ProcessId = 0;
		ServerProcess = FPlatformProcess::CreateProc(*Candidate.Exe, *Candidate.Args, false, true, true, &ProcessId, 0, *Candidate.WorkDir, nullptr, nullptr);
		if (ServerProcess.IsValid())
		{
			UE_LOG(LogCXJeminiServer, Log, TEXT("JeminiServer: launched %s %s (pid %u)"), *Candidate.Exe, *Candidate.Args, ProcessId);
			// 프로세스는 떴는데 곧바로 죽는 경우(불완전한 exe 빌드 등)를 위해 몇 초 뒤 살아 있는지 확인한다(이슈 #21)
			LaunchedCandidate = Candidate.Exe;
			if (!bShuttingDown)
			{
				FTSTicker::GetCoreTicker().RemoveTicker(VerifyHandle);
				TWeakObjectPtr<UCXJeminiServerSubsystem> WeakThis(this);
				VerifyHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Host, Port](float)
				{
					if (UCXJeminiServerSubsystem* Self = WeakThis.Get())
					{
						Self->VerifyLaunched(Host, Port);
					}
					return false;
				}), 4.0f);
			}
			return true;
		}
		UE_LOG(LogCXJeminiServer, Warning, TEXT("JeminiServer: failed to start %s"), *Candidate.Exe);
	}
	UE_LOG(LogCXJeminiServer, Warning, TEXT("JeminiServer: no server found to launch (looked for Jemini\\jemini-server.exe next to the game, and Jemini\\.venv in the repo). Jemini will answer with fallback lines."));
	return false;
}
