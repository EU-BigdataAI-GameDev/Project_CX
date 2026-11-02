// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "CoopInteractable.generated.h"

class APawn;

/** 협동 캐릭터 역할. 문 버튼처럼 "담당 캐릭터만" 쓸 수 있는 상호작용 판정에 쓴다. */
UENUM(BlueprintType)
enum class ECoopCharacterRole : uint8
{
	None,
	Brother,
	Sister
};

UINTERFACE(MinimalAPI, NotBlueprintable)
class UCoopInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 *  두 캐릭터 공용 상호작용(키보드 F / 게임패드 X) 대상(.claude/Plans/게임 클리어 화면 기획.md 2.1).
 *
 *  UCoopInteractComponent가 0.2초마다 범위 안에서 CanInteract가 참인 가장 가까운 대상을 포커스로 잡고,
 *  입력이 들어오면 서버에서 다시 CanInteract로 검증한 뒤 Interact를 호출한다(서버 권위 — listen server 전제).
 */
class ICoopInteractable
{
	GENERATED_BODY()

public:
	/** 이 폰이 지금 상호작용할 수 있는지(거리·담당 캐릭터·대상 상태). 로컬 포커스/HUD 안내와 서버 재검증에 같이 쓴다. */
	virtual bool CanInteract(const APawn* Pawn) const = 0;

	/** 서버에서만 호출된다. 호출 전에 CanInteract로 검증되어 있다. */
	virtual void Interact(APawn* Pawn) = 0;

	/** 이 폰 기준 상호작용 지점(HUD 안내 위치, 포커스 거리 비교). */
	virtual FVector GetInteractLocation(const APawn* Pawn) const = 0;

	/** HUD 안내 문구(키 표기 뒤에 붙는다, 예: "[F] PRESS"). */
	virtual FString GetInteractPrompt(const APawn* Pawn) const { return TEXT("INTERACT"); }
};
