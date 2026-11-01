// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CXTitlePlayerController.generated.h"

class UCXMenuWidgetBase;

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

private:
	bool bDisabledWorldRendering = false;
};
