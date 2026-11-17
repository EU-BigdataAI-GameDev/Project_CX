// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXTitlePlayerController.h"
#include "CXTitleMenuWidget.h"
#include "CXMenuSubsystem.h"
#include "CXAudioSubsystem.h"
#include "Sound/SoundBase.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"

ACXTitlePlayerController::ACXTitlePlayerController()
{
	TitleMenuClass = UCXTitleMenuWidget::StaticClass();
	TitleMusic = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Project_CX/Audio/BGM/SW_BGM_Title.SW_BGM_Title")));
	// 마우스 커서는 메뉴 서브시스템이 관리(마우스를 움직이면 표시, 키/패드 입력 시 숨김)
	bShowMouseCursor = false;
}

void ACXTitlePlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}
	// 1P 컨트롤러만 메뉴를 띄운다(메뉴는 화면 전체에 하나).
	const ULocalPlayer* LP = GetLocalPlayer();
	if (LP && LP->GetLocalPlayerIndex() != 0)
	{
		return;
	}

	if (UCXMenuSubsystem* Menus = UCXMenuSubsystem::Get(this))
	{
		Menus->PushMenu(TitleMenuClass, this);
	}

	if (UCXAudioSubsystem* Audio = UCXAudioSubsystem::Get(this))
	{
		Audio->PlayMusic(this, TitleMusic.LoadSynchronous(), TitleMusicLoopStart, TitleMusicLoopEnd);
	}

	// 배경은 위젯이 전부 그리므로 빈 월드 렌더링 생략
	if (LP && LP->ViewportClient && !LP->ViewportClient->bDisableWorldRendering)
	{
		LP->ViewportClient->bDisableWorldRendering = true;
		bDisabledWorldRendering = true;
	}
}

void ACXTitlePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bDisabledWorldRendering)
	{
		const ULocalPlayer* LP = GetLocalPlayer();
		if (LP && LP->ViewportClient)
		{
			LP->ViewportClient->bDisableWorldRendering = false;
		}
		bDisabledWorldRendering = false;
	}
	Super::EndPlay(EndPlayReason);
}
