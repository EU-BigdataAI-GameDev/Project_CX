// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HackCodeClue.generated.h"

class AHackTerminal;
class UTextRenderComponent;

/**
 *  비밀번호 단말기의 숫자 한 자리를 벽에 표시하는 단서(.claude/Plans/해킹 시스템 기획.md 4장).
 *
 *  오빠 폰을 Owner로 잡고 OnlyOwnerSee를 켜서 **오빠 화면에만** 보인다 — 오빠가 맵을 돌며 읽어주고
 *  여동생이 단말기에 입력하는 정보 비대칭 협동 기믹. 숫자는 단말기 비밀번호(런타임 랜덤)에서 읽는다.
 */
UCLASS()
class AHackCodeClue : public AActor
{
	GENERATED_BODY()

public:
	AHackCodeClue();

	/** 숫자를 가져올 비밀번호 단말기. */
	UPROPERTY(EditAnywhere, Category = "Hack")
	TObjectPtr<AHackTerminal> Terminal;

	/** 비밀번호에서 몇 번째 자리(0~3)인지. 화면에는 #1~#4로 표시된다. */
	UPROPERTY(EditAnywhere, Category = "Hack", meta = (ClampMin = "0", ClampMax = "3"))
	int32 DigitIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Hack")
	FColor DigitColor = FColor(255, 214, 40);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Hack")
	TObjectPtr<UTextRenderComponent> DigitText;

	UPROPERTY(VisibleAnywhere, Category = "Hack")
	TObjectPtr<UTextRenderComponent> SlotText;

private:
	/** 오빠 폰을 찾아 Owner로 지정하고 오빠 화면 전용으로 만든다. 폰은 BeginPlay 뒤에 생길 수 있어 Tick에서 재시도. */
	bool TryBindToBrother();

	bool bBound = false;
};
