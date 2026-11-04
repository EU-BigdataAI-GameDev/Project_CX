// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "HackTerminal.h"
#include "CoopHUD.generated.h"

class UBrotherCombatComponent;
class USisterHackComponent;
class UFont;
class USoundBase;
class UCXPlayerLifeComponent;
class UCXHealthComponent;

/**
 *  로컬 2인 협동용 인게임 HUD(.claude/Plans/HUD 기획.md).
 *
 *  AHUD는 PlayerController마다 하나씩 생기고 Canvas가 그 플레이어의 분할 화면 크기로 잡히므로,
 *  위아래 화면 분할에서 각 플레이어 칸에 자기 정보만 그린다. 레이아웃은 분할 한 칸(1920x540)을 기준으로
 *  설계하고 실제 칸 크기에 맞춰 비율 스케일한다.
 *
 *  - 공통: 플레이어 태그(좌상단) + 체력 바(태그 아래), 파트너 위치 표시(+ 작은 체력 바), 구역 배너, 분할 경계선, 피격 플래시
 *  - 적: 거미로봇 머리 위 체력 바(피격 후 3초 또는 오빠의 CurrentTarget일 때 — 전투 시스템 기획 9.2)
 *  - 오빠: 레일건 탄약/장전 패널(좌하단), 조준 타겟 마커
 *  - 여동생: 해킹 링크 패널(좌하단) + 근처 해킹 단말기 활성화 표시(월드 마커/프롬프트) + 해킹 중 퍼즐 오버레이
 *    + 적 해킹 대상(ICXHackTarget — 거미로봇)에도 같은 마름모 마커/`[X] HACK`(쿨타임 중엔 RELINK n초), 적 해킹 중 주파수 동조 패널
 *  - 공통: 그로기 중인 적 머리 위 SYSTEM DOWN + 남은 시간 바(두 플레이어 칸 모두 — 해킹 시스템 기획 10장)
 *  - 공통: 세이브포인트 저장 배너 PROGRESS SAVED, 출구 NEXT AREA OFFLINE(두 칸), 출구 안 대기 WAITING FOR PARTNER(자기 칸),
 *    맵 이동 카메라 페이드를 HUD 위에도 덮음(세이브포인트·맵 이동 기획 5장)
 *    (해킹 UI는 여동생 폰을 가진 HUD에서만 그린다 → 오빠 화면에는 절대 나타나지 않는다)
 */
UCLASS()
class ACoopHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACoopHUD();

	virtual void DrawHUD() override;

	/** 현재 맵의 구역명(ZoneTitles 기준). ESC 메뉴 정보줄이 재사용한다. BeginPlay 전엔 빈 문자열. */
	const FString& GetZoneTitle() const { return ZoneTitle; }

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
	FString HackKeyHint = TEXT("[X]");

	/** 부활 대기 마지막 3초 틱(에셋 없음 → 무음). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Respawn")
	TObjectPtr<USoundBase> RespawnTickSound;

	/** 부활 직후 REBOOTED 표시 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Respawn")
	float RebootedShowTime = 1.5f;

	/** 플레이어 체력 바 칸 수(100 → 1칸 = 10). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Health")
	int32 PlayerHealthSegments = 10;

	/** 이 비율 이하면 체력 바가 붉게 펄스(30/100). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Health")
	float LowHealthRatio = 0.3f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Health")
	FLinearColor DangerColor = FLinearColor(1.0f, 0.12f, 0.08f, 1.0f);

	/** 피격 시 자기 칸 가장자리 붉은 플래시 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Health")
	float HitFlashTime = 0.25f;

	/** 적 체력 바 1칸 값(총알 1발 = 10 → 거미로봇 50 = 5칸). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Health")
	float EnemyHealthSegmentValue = 10.0f;

	/** 적 체력 바를 피격 후 보여 주는 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Health")
	float EnemyHealthShowTime = 3.0f;

	/** 세이브포인트 저장 배너 시간(초). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Flow")
	float SaveBannerDuration = 2.5f;

	/** 저장 배너 색(세이브포인트 "저장됨" 초록과 같은 색). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Flow")
	FLinearColor SaveColor = FLinearColor(0.15f, 1.0f, 0.42f, 1.0f);

private:
	void DrawPlayerTag(const FLinearColor& Color, const FString& SlotText, const FString& DisplayName, const FString& RoleText);
	void DrawBrotherWeapon(const UBrotherCombatComponent& Combat);
	void DrawSisterHackLink();
	/** InactiveText가 비어 있지 않으면 비활성 마커의 거리 대신 그 문구를 쓴다(적 해킹 쿨타임 RELINK 등). */
	void DrawHackTerminalMarker(const AActor& Terminal, bool bActive, float DistM, float HeightOffset = 40.0f, const FString& InactiveText = FString());
	/** 여동생 칸: 적 해킹 대상(ICXHackTarget)에 단말기와 같은 마커. 해킹 가능할 때만 `[X] HACK`(그로기·사망·쿨타임이면 아님). */
	void DrawEnemyHackMarkers();
	/** 두 칸 공통: 그로기 중인 적 머리 위 SYSTEM DOWN + 남은 시간 바. */
	void DrawEnemyStunMarkers();
	/** 여동생 칸: 적 해킹(주파수 동조) 패널 — 단말기 해킹 오버레이와 같은 스타일. */
	void DrawEnemyHackSession(const USisterHackComponent& Hack);
	/** 플레이어 태그 아래 체력 바(10칸 + HP 70/100, 30 이하 붉은 펄스). */
	void DrawPlayerHealth(const UCXHealthComponent& Health, const FLinearColor& Color);
	/** 피격 직후 자기 칸 가장자리 붉은 플래시. */
	void DrawHitFlash(const UCXHealthComponent& Health);
	/** 적 머리 위 체력 바(피격 후 EnemyHealthShowTime초 또는 오빠의 현재 타겟). */
	void DrawEnemyHealthBars(const UBrotherCombatComponent* Combat);
	/** 칸 나눈 체력 바(부분 칸은 비율만큼). */
	void DrawSegmentBar(float X, float Y, float W, float H, int32 Segments, float Fill01, const FLinearColor& Color, float Gap);
	/** 공용 상호작용(F/X) 대상 안내 — 문 버튼 앞에서 "[F] PRESS"(해킹 단말기 마커와 같은 문법, 자기 색). */
	void DrawInteractPrompt(const APawn& MyPawn, const FLinearColor& Color);
	void DrawPistolIcon(float X, float Y, const FLinearColor& Color);
	void DrawHackSession(const USisterHackComponent& Hack);
	void DrawArrowGlyph(const FVector2D& Center, float Size, EHackArrow Direction, const FLinearColor& Color);

	/** 이미 해킹된 단말기는 표시/활성 대상에서 뺀다. */
	static bool IsTerminalAvailable(const AActor* Terminal);

	/** 레벨의 해킹 단말기 목록을 1초마다 갱신한다(매 프레임 전체 액터 순회를 피함). */
	void RefreshHackTerminals();
	void DrawTargetMarker(const UBrotherCombatComponent& Combat);
	void DrawPartnerIndicator(const APawn& Partner, const FLinearColor& Color, const FString& PartnerName, const UCXHealthComponent* PartnerHealth = nullptr);
	void DrawZoneBanner();
	/** 세이브 배너(두 칸) / NEXT AREA OFFLINE(두 칸) / 출구 대기 안내(자기 칸). */
	void DrawFlowNotices(const APawn* MyPawn, bool bDead);
	/** 맵 이동 카메라 페이드는 월드에만 걸리므로 HUD 위에도 같은 검정을 덮는다. */
	void DrawTravelFade();
	/** 사망자 칸: 어둡게 + 비네팅 + 노이즈 + SIGNAL LOST / 남은 초 / 진행 바 / REBOOTING IN ns(게임 오버화면 기획 3장). */
	void DrawRespawnOverlay(const UCXPlayerLifeComponent& Life, const FLinearColor& Color);
	/** 부활 직후 REBOOTED. */
	void DrawRebooted(const UCXPlayerLifeComponent& Life, const FLinearColor& Color);
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
	/** 체력 바를 그릴 적(ICombatDamageable + UCXHealthComponent)과 적 해킹 대상(ICXHackTarget) — 1초마다 갱신. */
	TArray<TWeakObjectPtr<AActor>> EnemyActors;
	TArray<TWeakObjectPtr<AActor>> HackEnemyTargets;
	FTimerHandle HackRefreshTimer;

	float UIScale = 1.0f;
	float StartTime = 0.0f;
	FString ZoneTitle;
	/** 마지막으로 틱 소리를 낸 남은 초(같은 초에 한 번만). */
	int32 LastTickSecond = INDEX_NONE;
};
