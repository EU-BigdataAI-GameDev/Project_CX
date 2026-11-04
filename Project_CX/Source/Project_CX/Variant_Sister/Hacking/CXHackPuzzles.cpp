// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXHackPuzzles.h"

// ---------------------------------------------------------------- 화살표 시퀀스

void FCXArrowSequencePuzzle::Start(int32 Length)
{
	Sequence.Reset();
	Length = FMath::Clamp(Length, 1, 7);
	for (int32 i = 0; i < Length; ++i)
	{
		Sequence.Add(static_cast<EHackArrow>(FMath::RandRange(0, 3)));
	}
	Index = 0;
}

void FCXArrowSequencePuzzle::Reset()
{
	Sequence.Reset();
	Index = 0;
}

ECXArrowStep FCXArrowSequencePuzzle::Input(EHackArrow Direction)
{
	if (!Sequence.IsValidIndex(Index))
	{
		return ECXArrowStep::Ignored;
	}
	if (Sequence[Index] != Direction)
	{
		// 틀리면 처음부터(같은 배열 유지 → 외워서 재도전)
		Index = 0;
		return ECXArrowStep::Wrong;
	}
	++Index;
	return Index >= Sequence.Num() ? ECXArrowStep::Complete : ECXArrowStep::Correct;
}

bool FCXArrowSequencePuzzle::GetExpected(EHackArrow& OutDirection) const
{
	if (!Sequence.IsValidIndex(Index))
	{
		return false;
	}
	OutDirection = Sequence[Index];
	return true;
}

// ---------------------------------------------------------------- 주파수 동조

void FCXFreqLockPuzzle::Start(const TArray<float>& InZoneWidths, const TArray<float>& InCursorSpeeds, float InTimeLimit, float InMissLockout)
{
	ZoneWidths = InZoneWidths;
	CursorSpeeds = InCursorSpeeds;
	TimeLimit = InTimeLimit;
	MissLockout = FMath::Max(0.0f, InMissLockout);
	bActive = true;
	Cursor = 0.0f;
	Direction = 1.0f;
	Restart();
}

void FCXFreqLockPuzzle::Reset()
{
	bActive = false;
	Step = 0;
	Elapsed = 0.0f;
	LockoutUntil = 0.0f;
}

void FCXFreqLockPuzzle::Restart()
{
	Step = 0;
	Elapsed = 0.0f;
	LockoutUntil = 0.0f;
	PlaceZone();
}

bool FCXFreqLockPuzzle::Tick(float DeltaTime)
{
	if (!bActive)
	{
		return false;
	}
	Elapsed += DeltaTime;

	// 커서 왕복(막대 양 끝에서 반사)
	Cursor += Direction * GetCursorSpeed() * DeltaTime;
	for (int32 Guard = 0; Guard < 4 && (Cursor > 1.0f || Cursor < 0.0f); ++Guard)
	{
		if (Cursor > 1.0f)
		{
			Cursor = 2.0f - Cursor;
			Direction = -1.0f;
		}
		else
		{
			Cursor = -Cursor;
			Direction = 1.0f;
		}
	}
	Cursor = FMath::Clamp(Cursor, 0.0f, 1.0f);
	return HasTimeLimit() && Elapsed >= TimeLimit;
}

ECXFreqLockStep FCXFreqLockPuzzle::Confirm()
{
	if (!bActive || Elapsed < LockoutUntil)
	{
		return ECXFreqLockStep::Ignored;
	}
	if (!IsCursorInZone())
	{
		// 빗나감: 1단계부터 다시(새 구간) + 짧은 입력 무시. 시간 제한이 있으면 계속 흐른다 → 벌칙은 "시간 손해"뿐.
		Step = 0;
		LockoutUntil = Elapsed + MissLockout;
		PlaceZone();
		return ECXFreqLockStep::Miss;
	}
	++Step;
	if (Step >= GetStepCount())
	{
		return ECXFreqLockStep::Complete;
	}
	// 다음 단계: 구간이 좁아지고 커서가 빨라진다
	PlaceZone();
	return ECXFreqLockStep::Hit;
}

void FCXFreqLockPuzzle::DebugPlaceCursor(bool bInsideZone)
{
	LockoutUntil = 0.0f;
	const float Half = GetZoneWidth() * 0.5f;
	if (bInsideZone)
	{
		Cursor = ZoneCenter;
	}
	else
	{
		// 구간 바깥에서 더 넓은 쪽
		Cursor = ZoneCenter > 0.5f ? FMath::Max(0.0f, ZoneCenter - Half - 0.1f) : FMath::Min(1.0f, ZoneCenter + Half + 0.1f);
	}
}

float FCXFreqLockPuzzle::GetZoneWidth() const
{
	if (ZoneWidths.Num() == 0)
	{
		return 0.2f;
	}
	return FMath::Clamp(ZoneWidths[FMath::Clamp(Step, 0, ZoneWidths.Num() - 1)], 0.02f, 1.0f);
}

float FCXFreqLockPuzzle::GetCursorSpeed() const
{
	if (CursorSpeeds.Num() == 0)
	{
		return 1.5f;
	}
	return FMath::Max(0.1f, CursorSpeeds[FMath::Clamp(Step, 0, CursorSpeeds.Num() - 1)]);
}

void FCXFreqLockPuzzle::PlaceZone()
{
	// 구간이 막대 안에 들어가고, 커서에서 ZoneMinCursorGap 이상 떨어진 위치
	const float Half = GetZoneWidth() * 0.5f;
	const float Min = Half + 0.02f;
	const float Max = 1.0f - Half - 0.02f;
	float Center = FMath::FRandRange(Min, Max);
	for (int32 Try = 0; Try < 8 && FMath::Abs(Center - Cursor) < ZoneMinCursorGap; ++Try)
	{
		Center = FMath::FRandRange(Min, Max);
	}
	if (FMath::Abs(Center - Cursor) < ZoneMinCursorGap)
	{
		// 운 나쁘면 커서 반대편 끝 쪽으로
		Center = Cursor < 0.5f ? Max : Min;
	}
	ZoneCenter = Center;
}
