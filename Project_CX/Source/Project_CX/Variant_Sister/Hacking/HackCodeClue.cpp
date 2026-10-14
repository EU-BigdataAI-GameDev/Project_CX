// Copyright Epic Games, Inc. All Rights Reserved.

#include "HackCodeClue.h"
#include "HackTerminal.h"
#include "BrotherCombatComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

AHackCodeClue::AHackCodeClue()
{
	PrimaryActorTick.bCanEverTick = true;

	DigitText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DigitText"));
	SetRootComponent(DigitText);
	DigitText->SetHorizontalAlignment(EHTA_Center);
	DigitText->SetVerticalAlignment(EVRTA_TextCenter);
	DigitText->SetWorldSize(120.0f);
	DigitText->SetText(FText::FromString(TEXT("?")));
	// 방향은 액터 회전으로 정한다(TextRender는 +X 쪽에서 읽힘 → 카메라(-X)를 보게 하려면 액터 yaw 180으로 배치)

	SlotText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("SlotText"));
	SlotText->SetupAttachment(DigitText);
	SlotText->SetHorizontalAlignment(EHTA_Center);
	SlotText->SetVerticalAlignment(EVRTA_TextCenter);
	SlotText->SetWorldSize(36.0f);
	SlotText->SetRelativeLocation(FVector(0.0f, 0.0f, 90.0f));

	// 오빠에게 Owner가 지정되기 전에는 아무에게도 보이지 않게(여동생 화면에 잠깐이라도 노출되지 않도록)
	DigitText->SetHiddenInGame(true);
	SlotText->SetHiddenInGame(true);
}

void AHackCodeClue::BeginPlay()
{
	Super::BeginPlay();

	SlotText->SetText(FText::FromString(FString::Printf(TEXT("#%d"), DigitIndex + 1)));
	DigitText->SetTextRenderColor(DigitColor);
	SlotText->SetTextRenderColor(DigitColor);
	bBound = TryBindToBrother();
}

void AHackCodeClue::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bBound)
	{
		bBound = TryBindToBrother();
	}

	// 비밀번호는 단말기 BeginPlay에서 랜덤 생성되므로 매 틱 최신 값을 표시(값 한 자리라 비용 무시)
	if (Terminal)
	{
		DigitText->SetText(FText::AsNumber(Terminal->GetPasscodeDigit(DigitIndex)));
	}
}

bool AHackCodeClue::TryBindToBrother()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (Pawn && Pawn->FindComponentByClass<UBrotherCombatComponent>())
		{
			SetOwner(Pawn);
			DigitText->SetOnlyOwnerSee(true);
			SlotText->SetOnlyOwnerSee(true);
			DigitText->SetHiddenInGame(false);
			SlotText->SetHiddenInGame(false);
			return true;
		}
	}
	return false;
}
