// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  회로 연결(Circuit Link) 퍼즐 규칙(.claude/Plans/맵2 개선 기획.md 3장) — 상태와 판정만 가진 순수 C++ 구조체.
 *  CXHackPuzzles.h의 다른 퍼즐과 같은 방식이지만, AHackTerminal(서버)이 직접 들고 있어야 해서
 *  HackTerminal.h에 의존하지 않는 별도 헤더로 둔다(CXHackPuzzles.h → HackTerminal.h → 이 헤더).
 *
 *  - 격자 Width x Height, 칸 번호 = Y * Width + X.
 *  - 끝점(Endpoint) e: 짝 = e / 2, 종류 = e % 2(0 시작점 / 1 도착점). Endpoints[e] = 칸 번호.
 *  - 회로(Path)는 끝점 하나(Origin)에서 시작해 상하좌우로 늘어나고, 반대 종류의 빈 끝점에 닿으면 끝난다(End).
 *    짝이 맞으면 연결(Linked), 다르면 MISMATCH(연결은 되지만 성공으로 치지 않는다).
 *  - 한 칸에는 회로 하나만(교차·겹침 불가). 모든 칸을 채울 필요는 없다. 실패·제한 시간 없음.
 */

/** 입력 하나. Up~Right는 EHackArrow와 같은 순서. */
enum class ECXCircuitOp : uint8
{
	Up,
	Down,
	Left,
	Right,
	/** 확인(F/X): 빈 끝점 위 = 그리기 시작, 회로 위 = 그 회로 삭제, 그리는 중 = 그리던 회로 삭제. */
	Confirm
};

/** 입력 하나의 판정. */
enum class ECXCircuitStep : uint8
{
	Ignored,
	/** 커서만 이동(그리는 중 아님). */
	Moved,
	Started,
	Extended,
	/** 왔던 칸으로 되돌아가 회로가 한 칸 줄었다. */
	Backtracked,
	/** 다른 회로·자기 회로·같은 종류 끝점 칸이라 못 간다. */
	Blocked,
	Deleted,
	/** 맞는 짝에 연결(아직 남은 짝 있음). */
	Linked,
	/** 틀린 짝에 연결 — 빨갛게 표시, 성공으로 치지 않음. */
	Mismatch,
	/** 마지막 짝까지 모두 맞게 연결. */
	Complete,
	/** 서버가 세션/입력을 거부(거리·사망·이미 해킹됨) — 여동생 세션 닫기. */
	Rejected
};

struct FCXCircuitLinkPuzzle
{
	struct FPath
	{
		TArray<int32> Cells;
		int32 Origin = INDEX_NONE;
		int32 End = INDEX_NONE;
	};

	int32 Width = 0;
	int32 Height = 0;
	/** 짝마다 2개(시작점, 도착점)의 칸 번호. */
	TArray<int32> Endpoints;
	TArray<FPath> Paths;
	/** 지금 그리는 회로(Paths 인덱스), 없으면 INDEX_NONE. */
	int32 Active = INDEX_NONE;
	int32 Cursor = 0;

	/** 레이아웃 구조 검사(크기, 짝 수 1~8, 칸 범위, 끝점 겹침). 답이 있는지는 Solve로 따로. */
	static bool IsValidLayout(int32 InWidth, int32 InHeight, const TArray<FIntPoint>& Starts, const TArray<FIntPoint>& Goals, FString* OutError = nullptr);

	/** 레이아웃을 깔고 회로를 비운다. 구조가 틀리면 false(상태는 비운 채). 커서는 첫 시작점. */
	bool Start(int32 InWidth, int32 InHeight, const TArray<FIntPoint>& Starts, const TArray<FIntPoint>& Goals);
	void Reset();
	bool IsActive() const { return Width > 0 && Endpoints.Num() > 0; }

	ECXCircuitStep Input(ECXCircuitOp Op);

	int32 NumPairs() const { return Endpoints.Num() / 2; }
	int32 NumCells() const { return Width * Height; }
	static int32 PairOf(int32 Endpoint) { return Endpoint / 2; }
	static bool IsStartEndpoint(int32 Endpoint) { return Endpoint % 2 == 0; }

	/** 그 칸의 끝점(없으면 INDEX_NONE). */
	int32 EndpointAt(int32 Cell) const;
	/** 그 칸을 지나는 회로(없으면 INDEX_NONE). */
	int32 PathAt(int32 Cell) const;
	bool IsLinked(const FPath& Path) const { return Path.End != INDEX_NONE && PairOf(Path.End) == PairOf(Path.Origin); }
	bool IsMismatch(const FPath& Path) const { return Path.End != INDEX_NONE && PairOf(Path.End) != PairOf(Path.Origin); }
	/** 맞는 짝으로 연결된 짝 수. */
	int32 CountLinked() const;
	bool IsComplete() const { return NumPairs() > 0 && CountLinked() == NumPairs(); }

	/**
	 *  답 찾기: 짝마다 시작점 → 도착점 단순 경로를 깊이 우선으로 놓고, 한 짝을 놓을 때마다 남은 짝이
	 *  빈 칸으로 아직 이어질 수 있는지 BFS로 가지치기. 지금 그린 회로는 무시(빈 판 기준). MaxSteps 넘으면 포기(false).
	 *  OutPaths[짝] = 시작점부터 도착점까지 칸 번호.
	 */
	bool Solve(TArray<TArray<int32>>& OutPaths, int32 MaxSteps = 200000) const;

	/** 빈 판 기준 정답 회로로 채운다(디버그 / 제미니 대행 / CXSolveHack 표시용). 답이 없으면 false. */
	bool ApplySolution();

	/**
	 *  지금 판에서 From → To 최단 경로(BFS, 상하좌우). 다른 회로가 지나는 칸과 From/To가 아닌 끝점은 피한다.
	 *  From/To 포함. 없으면 false. (테스트: 일부러 틀린 짝을 잇는 경로 찾기)
	 */
	bool FindRoute(int32 FromCell, int32 ToCell, TArray<int32>& OutCells) const;

	/** 기본 레이아웃(손으로 만든 6x5, 모두 답 있음 — 에디터 밖 Python 검사 + 시작 시 Solve 검사). */
	static int32 NumBuiltInLayouts();
	static void GetBuiltInLayout(int32 Index, int32& OutWidth, int32& OutHeight, TArray<FIntPoint>& OutStarts, TArray<FIntPoint>& OutGoals);

	/** 사람이 읽는 상태 한 줄(테스트 로그). */
	FString ToDebugString() const;

private:
	ECXCircuitStep Move(int32 DX, int32 DY);
	ECXCircuitStep Confirm();
	void RemovePath(int32 Index);
};
