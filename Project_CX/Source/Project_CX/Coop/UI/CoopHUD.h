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

/** HUD 글자 단계(예전 엔진 Tiny/Small/Medium/Large 폰트 자리). 실제 크기·굵기는 ACoopHUD::GameFont/SysFont가 정한다. */
enum class ECXHudFont : uint8 { Tiny, Small, Medium, Large };

/** 제미니 음성 말풍선 스타일: 최종 Heard(따옴표, 흰색) / 실시간 자막 듣는 중(글자 + 점) / 뗀 뒤 최종 대기(흐린 글자 + 느린 점). */
enum class ECXSpeakerBubble : uint8 { Final, Listening, Pending };

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
 *    + 적 해킹 대상(ICXHackTarget — 거미로봇)에도 같은 마름모 마커/`[X] HACK`(쿨타임 중엔 RELINK n초), 적 해킹 중 화살표 시퀀스 패널(1x5, 4초)
 *  - 공통: 그로기 중인 적 머리 위 SYSTEM DOWN + 남은 시간 바(두 플레이어 칸 모두 — 해킹 시스템 기획 10장)
 *  - 공통: 세이브포인트 저장 배너 PROGRESS SAVED, 출구 NEXT AREA OFFLINE(두 칸), 출구 안 대기 WAITING FOR PARTNER(자기 칸),
 *    맵 이동 카메라 페이드를 HUD 위에도 덮음(세이브포인트·맵 이동 기획 5장)
 *    (해킹 UI는 여동생 폰을 가진 HUD에서만 그린다 → 오빠 화면에는 절대 나타나지 않는다)
 *  - 예외(해킹 시스템 기획 11장 메모리 패턴): 메모리 패턴 단말기 세션 중 오빠 칸에 PATTERN RELAY 패널 — 패턴 점멸은 오빠 칸에만,
 *    여동생 칸은 같은 3x3 숫자판(점멸 없음) + 커서. 오빠가 중계 거리 밖이면 MOVE CLOSER // SIGNAL WEAK
 *  - 예외(맵2 개선 기획 3장 회로 연결): 세션 중 오빠 칸에 CIRCUIT RELAY 미니 격자(짝 색·번호). 여동생 칸은 짝 정보 없는 격자.
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

	/** 메모리 패턴 중계 패널(오빠 칸)에서 노드가 켜질 때마다 내는 짧은 틱 — 노드와 무관하게 같은 피치(소리로 정답이 새지 않게). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hack")
	TObjectPtr<USoundBase> MemoryFlashSound;

	/** 메모리 패턴 점멸 색(밝은 청록). */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Hack")
	FLinearColor MemoryFlashColor = FLinearColor(0.1f, 0.95f, 1.0f, 1.0f);

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
	/** 여동생 칸: 적 해킹(화살표 5칸 + 4초 시간 바) 패널 — 단말기 해킹 오버레이와 같은 스타일, 칸 위쪽 가운데. */
	void DrawEnemyHackSession(const USisterHackComponent& Hack);
	/** 화살표 칸 한 줄(완료 채움 / 현재 펄스 / 대기 흐림). CenterX 기준 가운데 정렬. */
	void DrawArrowCells(const TArray<EHackArrow>& Sequence, int32 DoneCount, bool bAllDone, float CenterX, float Y, float Cell, float Gap, const FLinearColor& Accent);
	/** 주파수 동조 막대(파형 + 목표 구간 LOCK + 왕복 커서) — 단말기 주파수 동조 패널. */
	void DrawFreqLockBar(const USisterHackComponent& Hack, float BarX, float BarY, float BarW, float BarH, bool bGranted, bool bMiss);
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
	/** 여동생 칸: 메모리 패턴 세션(3x3 숫자판 + 커서, 점멸 없음) + 중계 상태. */
	void DrawMemoryHackSession(const USisterHackComponent& Hack, const AHackTerminal& Terminal);
	/** 오빠 칸: 진행 중인 메모리 패턴 단말기가 있으면 PATTERN RELAY 패널(패턴 점멸) / 거리 밖이면 MOVE CLOSER. */
	void DrawMemoryRelay(const APawn& MyPawn);
	/** 3x3 노드 한 칸(전화 키패드 숫자 반투명). Flash 0~1 = 점멸 세기. */
	void DrawMemoryNode(float X, float Y, float Cell, int32 Node, const FLinearColor& Accent, float Dim, float Flash, bool bCursor, bool bPressed);
	void DrawArrowGlyph(const FVector2D& Center, float Size, EHackArrow Direction, const FLinearColor& Color);
	/** 여동생 칸: 회로 연결 세션(맵2 개선 기획 3장) — 격자(시작점 파랑 / 도착점 초록, 짝 정보 없음) + 커서 + 중계 상태. */
	void DrawCircuitHackSession(const USisterHackComponent& Hack, const AHackTerminal& Terminal);
	/** 오빠 칸: 진행 중인 회로 연결 단말기가 있으면 CIRCUIT RELAY 미니 격자(짝마다 색·번호) / 거리 밖이면 MOVE CLOSER. */
	void DrawCircuitRelay(const APawn& MyPawn);
	/** 회로 격자 공용 그림(단말기 복제값). bPairView = 짝 색·번호(오빠), 아니면 시작점 파랑·도착점 초록(여동생). */
	void DrawCircuitGrid(const AHackTerminal& Terminal, float GX, float GY, float Cell, bool bPairView, bool bShowCursor, float Dim);
	/** 둥근 연결점(가로·세로 사각형 겹침). */
	void DrawCircuitDot(const FVector2D& Center, float Radius, const FLinearColor& Color);
	/** 짝 번호(0~)별 네온 색(노랑, 보라, 주황, 청록, 연두, 분홍). */
	static FLinearColor CircuitPairColor(int32 Pair);

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
	/**
	 *  제미니(제미니 AI봇 기획 B6): 배터리 게이지(우상단) + 상태, 대사 말풍선(두 칸), 자기 입력 상태(녹음/막힘/키 안내),
	 *  오빠 탄약 0 안내, 여동생 해킹 대행 진행도.
	 */
	void DrawJemini(const APawn* MyPawn, bool bDead, bool bIsSister, const UBrotherCombatComponent* MyCombat);
	/**
	 *  음성으로 말한 플레이어 머리 위 STT 말풍선(Age = 응답 도착 후 경과 초).
	 *  말하는 도중에는 이 PC의 로컬 플레이어 실시간 자막(UCXJeminiInputComponent)을 먼저 그리고, 그 사람의 이전 Heard는 숨긴다.
	 */
	void DrawHeardBubble(const class ACXJeminiDrone& Drone, float Age);
	/** 말한 사람 머리 위 말풍선 하나(Style: ECXSpeakerBubble). */
	void DrawSpeakerBubble(const APawn* Speaker, const FString& Text, float Fade, ECXSpeakerBubble Style);
	/** STT 말풍선 표시 시간(초). */
	UPROPERTY(EditAnywhere, Category = "HUD|Jemini")
	float HeardBubbleDuration = 4.0f;

	/**
	 *  기능 B. WARDEN 설득 퍼즐(맵에 ACXWardenTerminal이 있을 때만): 증거 노트(왼쪽, 획득한 것 한 줄씩 — 본문은 획득 담당 칸에만),
	 *  새 증거 알림, 단말기 근처 WARDEN 대사 패널(두 칸 모두). DrawJemini보다 먼저(노트를 말풍선 배치 장애물로 등록).
	 */
	void DrawWarden(const APawn* MyPawn, bool bDead, bool bIsSister);
	uint8 LastEvidenceMask = 0;
	bool bEvidenceMaskInit = false;
	int32 EvidenceToastIndex = INDEX_NONE;
	float EvidenceToastTime = -100.0f;

	/** 자기 칸 체력 바 옆: 제미니 보호막 남은 시간 + 회복 +N 팝업(제미니 기능 기획 2-6, 2-9). */
	void DrawJeminiHealthExtras(const UCXHealthComponent& Health, const UCXPlayerLifeComponent* Life);

	/** 대사 말풍선 표시 시간(초). 대사에 줄바꿈이 있으면 줄마다 다음 말풍선(줄당 JeminiLineDuration, 마지막 줄은 이 시간). */
	float JeminiDialogueDuration = 5.0f;
	float JeminiLineDuration = 3.0f;
	int32 LastJeminiSeq = 0;
	float JeminiDialogueTime = -100.0f;

	/** 회복 팝업: 마지막으로 본 체력, 팝업 시작 시각과 양. */
	float LastSeenHealth = -1.0f;
	float HealPopupTime = -100.0f;
	int32 HealPopupAmount = 0;

	/** 분할 칸 기준 좌표(1920x540)를 실제 픽셀로 변환하는 배율. */
	float S() const { return UIScale; }

	void Panel(float X, float Y, float W, float H, const FLinearColor& Accent);
	void Label(const FString& Text, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale, bool bCenterX = false);

	/**
	 *  UI 폰트(UCXUIFonts). GameFont = Oxanium — 플레이어에게 주는 게임 정보(태그·체력·탄약·배너·알림).
	 *  SysFont = IBM Plex Mono — 게임 세계 시스템 출력(해킹 퍼즐·단말기 마커·SYSTEM DOWN·제미니 상태/대사·음성 자막).
	 */
	static UFont* GameFont(ECXHudFont Size);
	static UFont* SysFont(ECXHudFont Size);
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
	/** 이번 프레임 증거 노트 패널의 아래 끝 Y(없으면 0). 제미니 규칙 목록이 이 아래로 밀린다. */
	float EvidencePanelBottom = 0.0f;

	/**
	 *  이번 프레임 이 칸에 떠 있는 큰 패널(해킹 퍼즐·중계 패널) 영역(픽셀). DrawHUD 시작 때 비운다.
	 *  제미니 대사 말풍선·말하기 안내·해킹 대행 진행 바가 이 영역을 피해 배치된다.
	 */
	TArray<FBox2D> FrameBlockers;
	void AddBlocker(float X, float Y, float W, float H) { FrameBlockers.Add(FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H))); }
	bool IsBlocked(const FBox2D& Rect) const;

	/** Text를 MaxWidth(픽셀) 안에 들도록 단어 단위로 줄바꿈(최대 MaxLines줄, 넘치면 마지막 줄 끝을 …). */
	TArray<FString> WrapText(const FString& Text, UFont* Font, float Scale, float MaxWidth, int32 MaxLines) const;
	float StartTime = 0.0f;
	FString ZoneTitle;
	/** 메모리 패턴 점멸 틱 중복 방지(마지막으로 소리를 낸 재생 회차·순번). */
	int64 LastMemoryFlashKey = -1;

	/** 마지막으로 틱 소리를 낸 남은 초(같은 초에 한 번만). */
	int32 LastTickSecond = INDEX_NONE;
};
