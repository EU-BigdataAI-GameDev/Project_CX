// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CXTitlePlayerController.generated.h"

class UCXMenuWidgetBase;
class USoundBase;

/**
 *  시작화면 플레이어 컨트롤러 — 시작화면 메뉴를 띄우고(UCXMenuSubsystem), 메뉴가 화면 전체를 덮는 동안
 *  3D 월드 렌더링을 끈다. 맵을 떠날 때 렌더링 설정을 원래대로 돌린다(뷰포트 클라이언트는 맵 이동 후에도 유지되므로).
 */
UCLASS()
class ACXTitlePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACXTitlePlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Title")
	TSubclassOf<UCXMenuWidgetBase> TitleMenuClass;

	/** 시작화면 배경음. 인트로는 한 번만, 이후 [LoopStart, LoopEnd] 구간을 크로스페이드로 반복(UCXAudioSubsystem). */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Music")
	TSoftObjectPtr<USoundBase> TitleMusic;

	/** 반복 시작(초) — 인트로가 끝나고 본 테마가 들어오는 지점. */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Music")
	float TitleMusicLoopStart = 21.0f;

	/** 반복 끝(초) — 곡 끝 페이드아웃이 시작되기 전. */
	UPROPERTY(EditDefaultsOnly, Category = "Title|Music")
	float TitleMusicLoopEnd = 224.5f;

private:
	bool bDisabledWorldRendering = false;
};
