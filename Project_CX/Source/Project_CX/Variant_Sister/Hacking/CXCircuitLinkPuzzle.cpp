// Copyright Epic Games, Inc. All Rights Reserved.

#include "CXCircuitLinkPuzzle.h"

namespace CXCircuit
{
	// 손으로 만든 레이아웃(6x5). 시작점 / 도착점 짝 순서 = 오빠 화면 번호(1~). 모두 답 있음.
	struct FLayoutDef
	{
		int32 Width;
		int32 Height;
		TArray<FIntPoint> Starts;
		TArray<FIntPoint> Goals;
	};

	const TArray<FLayoutDef>& BuiltIns()
	{
		static const TArray<FLayoutDef> Layouts = {
			// 0: 4쌍 — 위쪽 긴 줄 + 가운데 짧은 짝(기본 레이아웃)
			{ 6, 5, { {0, 0}, {1, 1}, {0, 1}, {2, 1} }, { {5, 1}, {4, 3}, {5, 4}, {4, 1} } },
			// 1: 3쌍 — 바깥을 크게 도는 두 짝 사이로 대각 짝
			{ 6, 5, { {0, 0}, {1, 0}, {1, 3} }, { {3, 4}, {5, 4}, {4, 1} } },
			// 2: 4쌍 — 왼쪽 아래에서 오른쪽 위로 감는 짝
			{ 6, 5, { {0, 4}, {1, 1}, {1, 3}, {1, 4} }, { {5, 0}, {5, 3}, {3, 2}, {5, 4} } },
			// 3: 3쌍 — 위쪽 가운데에서 갈라지는 세 짝
			{ 6, 5, { {2, 0}, {3, 0}, {5, 0} }, { {3, 4}, {1, 3}, {2, 2} } },
		};
		return Layouts;
	}

	/** Solve 내부 상태. */
	struct FSolver
	{
		const FCXCircuitLinkPuzzle& P;
		TArray<uint8> Occupied;
		TArray<TArray<int32>>& Out;
		int32 Steps = 0;
		int32 MaxSteps = 0;
		bool bAborted = false;

		FSolver(const FCXCircuitLinkPuzzle& InP, TArray<TArray<int32>>& InOut, int32 InMax) : P(InP), Out(InOut), MaxSteps(InMax) {}

		int32 Neighbor(int32 Cell, int32 Dir) const
		{
			const int32 X = Cell % P.Width;
			const int32 Y = Cell / P.Width;
			switch (Dir)
			{
			case 0: return Y > 0 ? Cell - P.Width : INDEX_NONE;
			case 1: return Y < P.Height - 1 ? Cell + P.Width : INDEX_NONE;
			case 2: return X > 0 ? Cell - 1 : INDEX_NONE;
			default: return X < P.Width - 1 ? Cell + 1 : INDEX_NONE;
			}
		}

		/** 빈 칸만 지나 From에서 To로 갈 수 있나(BFS). */
		bool Reachable(int32 From, int32 To) const
		{
			TArray<uint8> Seen;
			Seen.Init(0, P.NumCells());
			TArray<int32> Queue;
			Queue.Add(From);
			Seen[From] = 1;
			for (int32 Head = 0; Head < Queue.Num(); ++Head)
			{
				for (int32 Dir = 0; Dir < 4; ++Dir)
				{
					const int32 N = Neighbor(Queue[Head], Dir);
					if (N == INDEX_NONE || Seen[N])
					{
						continue;
					}
					if (N == To)
					{
						return true;
					}
					if (!Occupied[N])
					{
						Seen[N] = 1;
						Queue.Add(N);
					}
				}
			}
			return false;
		}

		bool RemainingReachable(int32 FromPair) const
		{
			for (int32 Pair = FromPair; Pair < P.NumPairs(); ++Pair)
			{
				if (!Reachable(P.Endpoints[Pair * 2], P.Endpoints[Pair * 2 + 1]))
				{
					return false;
				}
			}
			return true;
		}

		bool SolvePair(int32 Pair)
		{
			if (Pair >= P.NumPairs())
			{
				return true;
			}
			TArray<int32> Path;
			Path.Add(P.Endpoints[Pair * 2]);
			return Extend(Pair, Path);
		}

		bool Extend(int32 Pair, TArray<int32>& Path)
		{
			if (++Steps > MaxSteps)
			{
				bAborted = true;
				return false;
			}
			const int32 Goal = P.Endpoints[Pair * 2 + 1];
			for (int32 Dir = 0; Dir < 4 && !bAborted; ++Dir)
			{
				const int32 N = Neighbor(Path.Last(), Dir);
				if (N == INDEX_NONE)
				{
					continue;
				}
				if (N == Goal)
				{
					Path.Add(N);
					Out[Pair] = Path;
					if (RemainingReachable(Pair + 1) && SolvePair(Pair + 1))
					{
						return true;
					}
					Path.Pop();
					continue;
				}
				if (Occupied[N])
				{
					continue;
				}
				Occupied[N] = 1;
				Path.Add(N);
				if (Extend(Pair, Path))
				{
					return true;
				}
				Path.Pop();
				Occupied[N] = 0;
			}
			return false;
		}
	};
}

bool FCXCircuitLinkPuzzle::IsValidLayout(int32 InWidth, int32 InHeight, const TArray<FIntPoint>& Starts, const TArray<FIntPoint>& Goals, FString* OutError)
{
	auto Fail = [OutError](const FString& Msg)
	{
		if (OutError)
		{
			*OutError = Msg;
		}
		return false;
	};
	if (InWidth < 2 || InHeight < 2 || InWidth > 12 || InHeight > 12)
	{
		return Fail(FString::Printf(TEXT("grid %dx%d out of range (2..12)"), InWidth, InHeight));
	}
	if (Starts.Num() != Goals.Num() || Starts.Num() < 1 || Starts.Num() > 8)
	{
		return Fail(FString::Printf(TEXT("pairs starts=%d goals=%d (need 1..8, equal)"), Starts.Num(), Goals.Num()));
	}
	TSet<FIntPoint> Used;
	for (int32 i = 0; i < Starts.Num() * 2; ++i)
	{
		const FIntPoint C = (i % 2 == 0) ? Starts[i / 2] : Goals[i / 2];
		if (C.X < 0 || C.Y < 0 || C.X >= InWidth || C.Y >= InHeight)
		{
			return Fail(FString::Printf(TEXT("endpoint (%d,%d) outside grid"), C.X, C.Y));
		}
		if (Used.Contains(C))
		{
			return Fail(FString::Printf(TEXT("endpoint (%d,%d) used twice"), C.X, C.Y));
		}
		Used.Add(C);
	}
	return true;
}

bool FCXCircuitLinkPuzzle::Start(int32 InWidth, int32 InHeight, const TArray<FIntPoint>& Starts, const TArray<FIntPoint>& Goals)
{
	Reset();
	if (!IsValidLayout(InWidth, InHeight, Starts, Goals))
	{
		return false;
	}
	Width = InWidth;
	Height = InHeight;
	for (int32 i = 0; i < Starts.Num(); ++i)
	{
		Endpoints.Add(Starts[i].Y * Width + Starts[i].X);
		Endpoints.Add(Goals[i].Y * Width + Goals[i].X);
	}
	Cursor = Endpoints[0];
	return true;
}

void FCXCircuitLinkPuzzle::Reset()
{
	Width = Height = 0;
	Endpoints.Reset();
	Paths.Reset();
	Active = INDEX_NONE;
	Cursor = 0;
}

ECXCircuitStep FCXCircuitLinkPuzzle::Input(ECXCircuitOp Op)
{
	if (!IsActive())
	{
		return ECXCircuitStep::Ignored;
	}
	switch (Op)
	{
	case ECXCircuitOp::Up:		return Move(0, -1);
	case ECXCircuitOp::Down:	return Move(0, 1);
	case ECXCircuitOp::Left:	return Move(-1, 0);
	case ECXCircuitOp::Right:	return Move(1, 0);
	case ECXCircuitOp::Confirm:	return Confirm();
	default: return ECXCircuitStep::Ignored;
	}
}

int32 FCXCircuitLinkPuzzle::EndpointAt(int32 Cell) const
{
	return Endpoints.IndexOfByKey(Cell);
}

int32 FCXCircuitLinkPuzzle::PathAt(int32 Cell) const
{
	for (int32 i = 0; i < Paths.Num(); ++i)
	{
		if (Paths[i].Cells.Contains(Cell))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 FCXCircuitLinkPuzzle::CountLinked() const
{
	int32 Count = 0;
	for (const FPath& Path : Paths)
	{
		Count += IsLinked(Path) ? 1 : 0;
	}
	return Count;
}

ECXCircuitStep FCXCircuitLinkPuzzle::Move(int32 DX, int32 DY)
{
	const int32 X = Cursor % Width + DX;
	const int32 Y = Cursor / Width + DY;
	if (X < 0 || Y < 0 || X >= Width || Y >= Height)
	{
		// 가장자리에서 멈춤(감아 돌리지 않음 — 메모리 패턴 커서와 같은 규칙)
		return ECXCircuitStep::Ignored;
	}
	const int32 Next = Y * Width + X;

	if (!Paths.IsValidIndex(Active))
	{
		Cursor = Next;
		return ECXCircuitStep::Moved;
	}

	FPath& Path = Paths[Active];
	// 되돌리기: 바로 앞 칸으로 돌아가면 회로가 한 칸 줄어든다
	if (Path.Cells.Num() >= 2 && Path.Cells[Path.Cells.Num() - 2] == Next)
	{
		Path.Cells.Pop();
		Cursor = Next;
		return ECXCircuitStep::Backtracked;
	}
	// 자기 회로·다른 회로와 교차/겹침 불가
	if (Path.Cells.Contains(Next) || PathAt(Next) != INDEX_NONE)
	{
		return ECXCircuitStep::Blocked;
	}
	const int32 Endpoint = EndpointAt(Next);
	if (Endpoint != INDEX_NONE)
	{
		// 같은 종류 끝점(시작점 → 시작점)은 이을 수 없다
		if (IsStartEndpoint(Endpoint) == IsStartEndpoint(Path.Origin))
		{
			return ECXCircuitStep::Blocked;
		}
		// 반대 종류의 빈 끝점 → 회로 완성. 짝이 다르면 MISMATCH(연결은 되지만 성공 아님).
		Path.Cells.Add(Next);
		Path.End = Endpoint;
		Cursor = Next;
		Active = INDEX_NONE;
		if (IsMismatch(Path))
		{
			return ECXCircuitStep::Mismatch;
		}
		return IsComplete() ? ECXCircuitStep::Complete : ECXCircuitStep::Linked;
	}
	Path.Cells.Add(Next);
	Cursor = Next;
	return ECXCircuitStep::Extended;
}

ECXCircuitStep FCXCircuitLinkPuzzle::Confirm()
{
	if (Paths.IsValidIndex(Active))
	{
		// 그리는 중 확인 = 그리던 회로 지우기(끝점에 닿으면 자동으로 끝나므로 "끝내기"는 따로 없다)
		RemovePath(Active);
		return ECXCircuitStep::Deleted;
	}
	const int32 OnPath = PathAt(Cursor);
	if (OnPath != INDEX_NONE)
	{
		RemovePath(OnPath);
		return ECXCircuitStep::Deleted;
	}
	const int32 Endpoint = EndpointAt(Cursor);
	if (Endpoint == INDEX_NONE)
	{
		return ECXCircuitStep::Ignored;
	}
	FPath& NewPath = Paths.AddDefaulted_GetRef();
	NewPath.Origin = Endpoint;
	NewPath.Cells.Add(Cursor);
	Active = Paths.Num() - 1;
	return ECXCircuitStep::Started;
}

void FCXCircuitLinkPuzzle::RemovePath(int32 Index)
{
	if (!Paths.IsValidIndex(Index))
	{
		return;
	}
	Paths.RemoveAt(Index);
	if (Active == Index)
	{
		Active = INDEX_NONE;
	}
	else if (Active > Index)
	{
		--Active;
	}
}

bool FCXCircuitLinkPuzzle::Solve(TArray<TArray<int32>>& OutPaths, int32 MaxSteps) const
{
	OutPaths.Reset();
	if (!IsActive())
	{
		return false;
	}
	OutPaths.SetNum(NumPairs());
	CXCircuit::FSolver Solver(*this, OutPaths, FMath::Max(1000, MaxSteps));
	Solver.Occupied.Init(0, NumCells());
	for (const int32 Cell : Endpoints)
	{
		Solver.Occupied[Cell] = 1;
	}
	const bool bSolved = Solver.RemainingReachable(0) && Solver.SolvePair(0);
	if (!bSolved)
	{
		OutPaths.Reset();
	}
	return bSolved;
}

bool FCXCircuitLinkPuzzle::ApplySolution()
{
	TArray<TArray<int32>> Solution;
	if (!Solve(Solution))
	{
		return false;
	}
	Paths.Reset();
	for (int32 Pair = 0; Pair < Solution.Num(); ++Pair)
	{
		FPath& Path = Paths.AddDefaulted_GetRef();
		Path.Cells = Solution[Pair];
		Path.Origin = Pair * 2;
		Path.End = Pair * 2 + 1;
	}
	Active = INDEX_NONE;
	return true;
}

bool FCXCircuitLinkPuzzle::FindRoute(int32 FromCell, int32 ToCell, TArray<int32>& OutCells) const
{
	OutCells.Reset();
	if (!IsActive() || FromCell < 0 || ToCell < 0 || FromCell >= NumCells() || ToCell >= NumCells())
	{
		return false;
	}
	auto Blocked = [this, FromCell, ToCell](int32 Cell)
	{
		if (Cell == FromCell || Cell == ToCell)
		{
			return false;
		}
		return PathAt(Cell) != INDEX_NONE || EndpointAt(Cell) != INDEX_NONE;
	};
	// Prev: 아직 안 본 칸 = Unseen, 출발 칸 = INDEX_NONE(경로 되짚기 끝)
	const int32 Unseen = INDEX_NONE - 1;
	TArray<int32> Prev;
	Prev.Init(Unseen, NumCells());
	TArray<int32> Queue;
	Queue.Add(FromCell);
	Prev[FromCell] = INDEX_NONE;
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 C = Queue[Head];
		if (C == ToCell)
		{
			for (int32 At = ToCell; At != INDEX_NONE; At = Prev[At])
			{
				OutCells.Insert(At, 0);
			}
			return true;
		}
		const int32 X = C % Width;
		const int32 Y = C / Width;
		const int32 Candidates[4] = { Y > 0 ? C - Width : INDEX_NONE, Y < Height - 1 ? C + Width : INDEX_NONE, X > 0 ? C - 1 : INDEX_NONE, X < Width - 1 ? C + 1 : INDEX_NONE };
		for (const int32 N : Candidates)
		{
			if (N != INDEX_NONE && Prev[N] == Unseen && !Blocked(N))
			{
				Prev[N] = C;
				Queue.Add(N);
			}
		}
	}
	return false;
}

int32 FCXCircuitLinkPuzzle::NumBuiltInLayouts()
{
	return CXCircuit::BuiltIns().Num();
}

void FCXCircuitLinkPuzzle::GetBuiltInLayout(int32 Index, int32& OutWidth, int32& OutHeight, TArray<FIntPoint>& OutStarts, TArray<FIntPoint>& OutGoals)
{
	const TArray<CXCircuit::FLayoutDef>& Layouts = CXCircuit::BuiltIns();
	const CXCircuit::FLayoutDef& L = Layouts[FMath::Clamp(Index, 0, Layouts.Num() - 1)];
	OutWidth = L.Width;
	OutHeight = L.Height;
	OutStarts = L.Starts;
	OutGoals = L.Goals;
}

FString FCXCircuitLinkPuzzle::ToDebugString() const
{
	FString Eps;
	for (int32 e = 0; e < Endpoints.Num(); ++e)
	{
		Eps += FString::Printf(TEXT("%s%c%d=%d"), e ? TEXT(",") : TEXT(""), IsStartEndpoint(e) ? TEXT('S') : TEXT('G'), PairOf(e) + 1, Endpoints[e]);
	}
	FString PathText;
	for (const FPath& Path : Paths)
	{
		FString Cells;
		for (const int32 C : Path.Cells)
		{
			Cells += FString::Printf(TEXT("%s%d"), Cells.IsEmpty() ? TEXT("") : TEXT("-"), C);
		}
		PathText += FString::Printf(TEXT(" [%d>%d %s]"), Path.Origin, Path.End, *Cells);
	}
	return FString::Printf(TEXT("grid=%dx%d eps=%s cursor=%d active=%d linked=%d/%d paths=%s"), Width, Height, *Eps, Cursor, Active, CountLinked(), NumPairs(), *PathText);
}
