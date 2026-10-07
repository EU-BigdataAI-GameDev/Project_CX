// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CoopHUD.generated.h"

class UBrotherCombatComponent;
class UFont;

/**
 *  로컬 2인 협동용 인게임 HUD(.claude/Plans/HUD 기획.md).
 *
 *  AHUD는 PlayerController마다 하나씩 생기고 Canvas가 그 플레이어의 분할 화면 크기로 잡히므로,
 *  위아래 화면 분할에서 각 플레이어 칸에 자기 정보만 그린다. 레이아웃은 분할 한 칸(1920x540)을 기준으로
 *  설계하고 실제 칸 크기에 맞춰 비율 스케일한다.
 *
 *  - 공통: 플레이어 태그(좌상단), 파트너 위치 표시, 구역 배너, 분할 경계선
 *  - 오빠: 레일건 탄약/장전 패널(좌하단), 조준 타겟 마커
 *  - 여동생: 해킹 링크 패널(좌하단) + 근처 해킹 단말기 활성화 표시(월드 마커/프롬프트)
 */
UCLASS()
class ACoopHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACoopHUD();

	virtual void DrawHUD() override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Style")
	FLinearColor BrotherColor = FLinearColor(0.0f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Style")
	FLinearColor SisterColor = FLinearColor(1.0f, 0.22f, 0.62f, 1.0f);

	/** 탄약이 이 비율 이하로 남으면 경고색으로 바뀐다. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Style")
	float LowAmmoRatio = 0.25f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Style")
	FLinearColor WarningColor = FLinearColor(1.0f, 0.35f, 0.08f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Style")
	FLinearColor PanelColor = FLinearColor(0.01f, 0.015f, 0.03f, 0.62f);

	/** 레벨 시작 시 상단 중앙에 구역명을 띄우는 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Zone")
	float ZoneBannerDuration = 4.0f;

	/** 맵 이름(접두사 없는 패키지 이름) → 구역명. 없는 맵은 DefaultZoneTitle을 쓴다. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Zone")
	TMap<FString, FString> ZoneTitles;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Zone")
	FString DefaultZoneTitle = TEXT("UNDERCITY");

	/** 이 태그가 붙은 액터를 해킹 단말기로 본다. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hack")
	FName HackableTag = TEXT("Hackable");

	/** 클래스 이름이 이 접두사로 시작하는 액터도 해킹 단말기로 본다(레벨 컨벤션 BP_HackTerminal_[번호], 맵2 BP_HackZone). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hack")
	TArray<FString> HackableClassPrefixes = { TEXT("BP_HackTerminal"), TEXT("BP_HackZone") };

	/** 이 거리(cm) 안이면 단말기가 활성화(해킹 가능)된 것으로 표시한다. 여동생 기획서의 상호작용 반경과 맞춘다. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hack")
	float HackActivateRange = 300.0f;

	/** 이 거리(cm) 안의 단말기는 신호 세기와 월드 마커로 위치를 알려준다. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hack")
	float HackSignalRange = 2000.0f;

	/** 화면에 띄우는 해킹 입력 안내. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hack")
	FString HackKeyHint = TEXT("[F]");

private:
	void DrawPlayerTag(const FLinearColor& Color, const FString& SlotText, const FString& DisplayName, const FString& RoleText);
	void DrawBrotherWeapon(const UBrotherCombatComponent& Combat);
	void DrawSisterHackLink();
	void DrawHackTerminalMarker(const AActor& Terminal, bool bActive, float DistM);
	void DrawPistolIcon(float X, float Y, const FLinearColor& Color);

	/** 레벨의 해킹 단말기 목록을 1초마다 갱신한다(매 프레임 전체 액터 순회를 피함). */
	void RefreshHackTerminals();
	void DrawTargetMarker(const UBrotherCombatComponent& Combat);
	void DrawPartnerIndicator(const APawn& Partner, const FLinearColor& Color, const FString& PartnerName);
	void DrawZoneBanner();
	void DrawSplitEdge(const FLinearColor& Color);

	/** 분할 칸 기준 좌표(1920x540)를 실제 픽셀로 변환하는 배율. */
	float S() const { return UIScale; }

	void Panel(float X, float Y, float W, float H, const FLinearColor& Accent);
	void Label(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale, bool bCenterX = false);
	void Bracket(const FVector2D& Center, float HalfSize, float Arm, const FLinearColor& Color, float Thickness);

	/** 다른 로컬 플레이어의 폰(파트너). 없으면 nullptr. */
	APawn* FindPartnerPawn() const;

	/** 시각 이펙트(점멸/펄스)용 0~1 값. */
	float Pulse(float Speed) const;

	TArray<TWeakObjectPtr<AActor>> HackTerminals;
	FTimerHandle HackRefreshTimer;

	float UIScale = 1.0f;
	float StartTime = 0.0f;
	FString ZoneTitle;
};
