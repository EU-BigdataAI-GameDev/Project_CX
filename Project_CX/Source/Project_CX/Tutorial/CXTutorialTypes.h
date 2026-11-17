// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  첫 플레이 튜토리얼(코치 마크) 자료형 + 단계 표(Docs/Guide/튜토리얼_가이드.md).
 *
 *  문구·시간을 고칠 때는 CXTutorialTypes.cpp의 CXTutorial::GetSteps() 표 하나만 고치면 된다.
 *  발동 조건·완료 판정은 UCXTutorialSubsystem(CXTutorialSubsystem.cpp)의 ShouldTrigger / IsCardComplete.
 */

/** 단계 ID. 순서 = 발동 우선순위. 저장되는 이름은 FCXTutorialStepDef::Id(문자열)라 순서를 바꿔도 "봤음" 기록은 유지된다. */
enum class ECXTutorialStep : uint8
{
	Move,
	FirstSpider,
	FirstTerminal,
	JeminiHello,
	JeminiRule,
	Warden,
	Count
};

/** 카드 종류. */
enum class ECXTutorialMode : uint8
{
	/** 이 플레이어에게는 카드 없음. */
	None,
	/** 게임을 막지 않는 한 줄 안내(칸 아래 가운데). 행동하거나 시간이 지나면 사라진다. */
	Hint,
	/** 슬로모션(전역 타임 딜레이션) + 행동 유도. 그 행동을 하면 끝난다. 슬로모션은 BlockTimeout초에 풀린다(멈춤 방지). */
	Action,
	/** 월드 일시정지 + 설명 카드. 대상 플레이어가 확인 키를 눌러야 끝난다(BlockTimeout초 뒤 자동 계속). */
	Info
};

/** 강조 상자(스포트라이트)가 감쌀 HUD 요소. */
enum class ECXTutorialSpot : uint8
{
	None,
	/** 우상단 제미니 배터리 패널 + 말 걸기 안내 줄. */
	JeminiPanel,
	/** 여동생 좌하단 HACK LINK 패널. */
	HackLinkPanel,
	/** WARDEN 증거 노트(증거가 하나도 없으면 강조 없음). */
	EvidencePanel,
	/** 월드 액터(처음 만난 거미 등)를 화면에 투영한 상자. */
	WorldTarget,
	Count
};

/** 한 플레이어(오빠 또는 여동생) 칸에 뜨는 카드 하나. Mode가 None이면 그 플레이어는 대상이 아니다. */
struct FCXTutorialCardDef
{
	ECXTutorialMode Mode = ECXTutorialMode::None;
	/** 제목과 본문 2줄. {move} 같은 토큰은 그 플레이어의 조작 장치(키보드/게임패드) 표기로 바뀐다 — CXTutorial::FormatText. */
	const TCHAR* Title = TEXT("");
	const TCHAR* Line1 = TEXT("");
	const TCHAR* Line2 = TEXT("");
	ECXTutorialSpot Spot = ECXTutorialSpot::None;
};

/** 튜토리얼 단계 하나(표의 한 줄). */
struct FCXTutorialStepDef
{
	ECXTutorialStep Step = ECXTutorialStep::Count;
	/** 저장·콘솔 명령에 쓰는 이름(CXTutorialShow <Id>). */
	const TCHAR* Id = TEXT("");
	FCXTutorialCardDef Brother;
	FCXTutorialCardDef Sister;
	/** 앞 단계가 끝난 뒤(또는 맵 시작 뒤) 이 시간(실시간 초)이 지나야 발동할 수 있다. */
	float StartDelay = 0.0f;
	/** Info: 이 시간(실시간 초) 뒤 자동 계속. Action: 슬로모션 최대 시간. Hint: 사용 안 함. */
	float BlockTimeout = 0.0f;
	/** Hint / Action 카드가 떠 있는 최대 시간(실시간 초). 지나면 단계 완료 처리. */
	float Timeout = 0.0f;
	/** 예시 문장 칩(없으면 nullptr). 키보드 플레이어는 [1]~[3]으로 채팅창에 넣을 수 있다. */
	const TCHAR* Chips[3] = { nullptr, nullptr, nullptr };
};

namespace CXTutorial
{
	/** Action 단계 슬로모션 배율(전역 타임 딜레이션). */
	constexpr float SlowDilation = 0.15f;
	/** Info 카드가 뜬 직후 이 시간(실시간 초) 동안은 확인 입력을 받지 않는다(점프·사격 연타로 바로 닫히는 것 방지). */
	constexpr float MinReadTime = 0.7f;
	/** 발동 조건 검사 간격(실시간 초). */
	constexpr float PollInterval = 0.2f;

	/** 단계 표(순서 = ECXTutorialStep). 문구·시간은 전부 여기. */
	const TArray<FCXTutorialStepDef>& GetSteps();

	/** Id(대소문자 무시)로 단계 찾기. 없으면 INDEX_NONE. */
	int32 FindStepIndex(const FString& Id);

	/**
	 *  토큰을 조작 장치 표기로 바꾼다.
	 *  {move} {jump} {fire_ro} {interact} {cancel} {dir} {talk} {talk_do} {chip_hint} {confirm} {skip}
	 */
	FString FormatText(const TCHAR* Text, bool bGamepad);
}

/** HUD(ACoopHUD::DrawTutorial)가 이번 프레임에 그릴 내용 — 플레이어 칸 하나 기준. */
struct FCXTutorialView
{
	ECXTutorialMode Mode = ECXTutorialMode::None;
	/** 자기는 대상이 아니지만 파트너의 Info 카드 때문에 월드가 멈춰 있다 → "파트너가 안내를 읽는 중" 표시만. */
	bool bWaitingNotice = false;
	/** 1부터 시작하는 단계 번호(카드 머리말). */
	int32 StepNumber = 0;
	FString Title;
	FString Line1;
	FString Line2;
	TArray<FString> Chips;
	/** 칩 앞에 [1]~[3] 키 표시(키보드 플레이어). */
	bool bChipKeys = false;
	/** 카드 맨 아래 안내(계속/건너뛰기). 비면 없음. */
	FString Footer;
	ECXTutorialSpot Spot = ECXTutorialSpot::None;
	TWeakObjectPtr<AActor> SpotActor;
	/** 칸 전체를 덮는 어둡기(0 = 없음). */
	float DimAlpha = 0.0f;
	/** 카드가 뜬 뒤 지난 시간(실시간 초) — 등장 페이드. */
	float Age = 0.0f;
	/** 실시간 시계(초) — 깜빡임. 일시정지 중에도 흐른다. */
	float RealTime = 0.0f;
};
