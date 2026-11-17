// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CoopInteractable.h"

/**
 *  기능 B. WARDEN 설득 퍼즐 — 증거 3개(.claude/Plans/제미니 고도화 기획 ver2.md 3장).
 *  서버(Jemini/app/persuasion.py EVIDENCE)와 같은 id·내용. 게임은 id만 보내고 판정용 원문은 서버가 가진다.
 *  여기 글자는 HUD 증거 노트 표시용이다.
 */
namespace CXWarden
{
	constexpr int32 EvidenceCount = 3;

	struct FEvidenceInfo
	{
		const TCHAR* Id;
		/** 노트 제목(한 줄). */
		const TCHAR* Title;
		/** 노트 본문(획득한 본인 칸에만 전체 표시 — 상대에게는 말로 전해야 한다). */
		const TCHAR* Body;
		/** 획득 담당(노트 비대칭 표시 기준). */
		ECoopCharacterRole Finder;
	};

	inline const FEvidenceInfo& GetInfo(int32 Index)
	{
		static const FEvidenceInfo Infos[EvidenceCount] = {
			{ TEXT("E1"), TEXT("안전 규정 제7조"), TEXT("작업자의 생명이 위협받으면 대피가 모든 잠금·통제 명령보다 우선한다."), ECoopCharacterRole::Brother },
			{ TEXT("E2"), TEXT("냉각 고장 로그"), TEXT("C-3 냉각 펌프 고장. 과열 경보 6회, 모두 '확인 안 함'으로 무시됨. 온도 상승 중."), ECoopCharacterRole::Sister },
			{ TEXT("E3"), TEXT("은폐 기록"), TEXT("관리자 계정이 냉각 경보를 수동으로 껐다. 작업자 3명이 아직 냉각 구역 안에 있다."), ECoopCharacterRole::Sister },
		};
		return Infos[FMath::Clamp(Index, 0, EvidenceCount - 1)];
	}

	/** "E1"~"E3" → 0~2, 그 외 INDEX_NONE. 대소문자 무시, "1"~"3"도 받는다. */
	inline int32 IndexOf(const FString& Id)
	{
		FString S = Id.TrimStartAndEnd().ToUpper();
		S.RemoveFromStart(TEXT("E"));
		const int32 N = FCString::Atoi(*S);
		return (S.IsNumeric() && N >= 1 && N <= EvidenceCount) ? N - 1 : INDEX_NONE;
	}

	inline int32 IndexOf(FName Id)
	{
		return Id.IsNone() ? INDEX_NONE : IndexOf(Id.ToString());
	}
}
