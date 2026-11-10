// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HackTerminal.h"

/**
 *  해킹 퍼즐 규칙(.claude/Plans/해킹 시스템 기획.md) — 상태와 판정만 가진 순수 C++ 구조체.
 *  USisterHackComponent가 세션 종류(단말기 / 적)에 맞게 들고 쓰고, HUD는 컴포넌트의 조회 함수로 읽는다.
 *  입력 장치·소리·세션 수명은 컴포넌트 담당이다(여기는 규칙만 → 단말기/적 경로가 같은 코드를 공유).
 *  회로 연결(FCXCircuitLinkPuzzle)은 서버 단말기가 직접 들고 있어야 해서 CXCircuitLinkPuzzle.h에 따로 있다(HackTerminal.h가 포함).
 */

/** 화살표 입력 한 번의 판정. */
enum class ECXArrowStep : uint8
{
	Ignored,
	/** 틀림 → 같은 배열 처음부터(입력 위치 0). */
	Wrong,
	Correct,
	/** 마지막 칸까지 맞힘. */
	Complete
};

/**
 *  화살표 시퀀스(퍼즐 A) — 적 해킹 전용(해킹 시스템 기획 3장 / 10장): 1라운드 N칸, 틀리면 같은 배열 처음부터.
 *  제한 시간은 세션(적 해킹)이 따로 잰다.
 */
struct FCXArrowSequencePuzzle
{
	TArray<EHackArrow> Sequence;
	int32 Index = 0;

	/** 새 랜덤 배열(길이 1~7). */
	void Start(int32 Length);
	void Reset();
	ECXArrowStep Input(EHackArrow Direction);

	int32 Num() const { return Sequence.Num(); }
	bool IsActive() const { return Sequence.Num() > 0; }
	/** 테스트: 다음에 눌러야 할 방향(없으면 false). */
	bool GetExpected(EHackArrow& OutDirection) const;
};

/** 주파수 동조 확인 입력(F/X) 한 번의 판정. */
enum class ECXFreqLockStep : uint8
{
	/** 입력 무시 시간 중 / 세션 없음. */
	Ignored,
	/** 빗나감 → 1단계부터 다시(새 구간) + 짧은 입력 무시. */
	Miss,
	Hit,
	/** 마지막 단계 적중. */
	Complete
};

/**
 *  주파수 동조(Frequency Lock, 퍼즐 C) — 단말기 퍼즐(해킹 시스템 기획 12장, 원래 10장 적 해킹용).
 *  막대 위를 왕복하는 커서가 목표 구간 안일 때 확인 → 단계마다 구간이 좁아지고 커서가 빨라진다. N연속 적중이면 완료.
 */
struct FCXFreqLockPuzzle
{
	// 설정(Start에서 복사)
	TArray<float> ZoneWidths;
	TArray<float> CursorSpeeds;
	/** 0 이하 = 제한 없음. */
	float TimeLimit = 0.0f;
	float MissLockout = 0.25f;
	/** 새 목표 구간은 커서에서 최소 이만큼(0~1) 떨어진 곳에 생긴다(바로 눌러 맞히는 일 방지). */
	float ZoneMinCursorGap = 0.3f;

	// 상태
	int32 Step = 0;
	float Cursor = 0.0f;
	float Direction = 1.0f;
	float ZoneCenter = 0.5f;
	float Elapsed = 0.0f;
	float LockoutUntil = 0.0f;
	bool bActive = false;

	void Start(const TArray<float>& InZoneWidths, const TArray<float>& InCursorSpeeds, float InTimeLimit, float InMissLockout);
	void Reset();
	/** 1단계부터 다시(새 구간, 경과 시간 0) — 단말기 시간 초과 시. */
	void Restart();

	/** 커서 이동 + 경과 시간. 제한 시간이 있고 넘었으면 true. */
	bool Tick(float DeltaTime);
	ECXFreqLockStep Confirm();

	/** 테스트: 커서를 목표 구간 안/밖에 놓고 입력 무시 시간을 없앤다. */
	void DebugPlaceCursor(bool bInsideZone);

	float GetZoneWidth() const;
	float GetCursorSpeed() const;
	int32 GetStepCount() const { return FMath::Max(1, ZoneWidths.Num()); }
	bool HasTimeLimit() const { return TimeLimit > 0.0f; }
	float GetTimeRemaining() const { return HasTimeLimit() ? FMath::Max(0.0f, TimeLimit - Elapsed) : 0.0f; }
	bool IsCursorInZone() const { return FMath::Abs(Cursor - ZoneCenter) <= GetZoneWidth() * 0.5f; }

private:
	void PlaceZone();
};
