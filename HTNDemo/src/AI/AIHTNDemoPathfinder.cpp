// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "AI/AIHTNDemoPathfinder.h"

#include "Core/HTNAtom.h"
#include "Core/HtnSymbol.h"
#include "Core/HTNCallTermBinding.h"
#include "Hook/HTNPlannerHook.h"
#include "WorldState/HTNWorldState.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <queue>

namespace
{
const HtnSymbol* sPathfindState = HtnSymbol::sGetSymbol("pathfind_state");
const HtnSymbol* sPathfinder = HtnSymbol::sGetSymbol("pathfinder");
const HtnSymbol* sPathfindingSegment = HtnSymbol::sGetSymbol("pathfinding_segment");

const HtnSymbol* sInProgress = HtnSymbol::sGetSymbol("in_progress");
const HtnSymbol* sSucceeded = HtnSymbol::sGetSymbol("succeeded");
const HtnSymbol* sFailed = HtnSymbol::sGetSymbol("failed");

}

AIHTNDemoPathfinder::AIHTNDemoPathfinder(const DemoGridTerrain& inTerrain)
    : mTerrain(inTerrain)
{
}

void AIHTNDemoPathfinder::BindCallTerms(HTNCallTermRegistry& ioRegistry)
{
    HTN_CALLTERM_BIND_MEMBER(ioRegistry,
                             "request_path_from_to",
                             AIHTNDemoPathfinder,
                             RequestPath);
    HTN_CALLTERM_BIND(ioRegistry, "same_location", AIHTNDemoPathfinder, IsSameLocation);
}

void AIHTNDemoPathfinder::OnWriteWorldState(HTNWorldState& ioWorldState)
{
    // This daemon owns these dynamic fact tables. Rebuild them from the persistent
    // request state so replanning always sees a coherent pathfinding snapshot.
    (void)ioWorldState.ClearFact(sPathfindState, 4u);
    (void)ioWorldState.ClearFact(sPathfinder, 4u);
    (void)ioWorldState.ClearFact(sPathfindingSegment, 3u);

    if (mRequest)
    {
        WriteRequestFacts(ioWorldState, *mRequest);
    }
}


bool AIHTNDemoPathfinder::GetRemainingPath(
    const Cell& inCurrentLocation,
    const Cell& inDestination,
    std::vector<Cell>& outPath) const
{
    outPath.clear();

    if (!mRequest ||
        mRequest->Status != RequestStatus::Succeeded ||
        !(mRequest->To == inDestination))
    {
        return false;
    }

    outPath.push_back(inCurrentLocation);

    if (inCurrentLocation == inDestination)
        return true;

    // Before the first segment executes, the actor is still at Request::From.
    if (inCurrentLocation == mRequest->From)
    {
        outPath.insert(outPath.end(), mRequest->Path.begin(), mRequest->Path.end());
        return !outPath.empty();
    }

    const auto CurrentIt = std::find(
        mRequest->Path.begin(),
        mRequest->Path.end(),
        inCurrentLocation);

    if (CurrentIt == mRequest->Path.end())
    {
        outPath.clear();
        return false;
    }

    const auto NextIt = CurrentIt + 1;
    outPath.insert(outPath.end(), NextIt, mRequest->Path.end());
    return true;
}

void AIHTNDemoPathfinder::Update(float inDeltaTime)
{
    (void)inDeltaTime;

    if (!mRequest || mRequest->Status != RequestStatus::InProgress)
    {
        return;
    }

    Request& RequestEntry = *mRequest;

    --RequestEntry.TicksRemaining;
    if (RequestEntry.TicksRemaining > 0)
    {
        return;
    }

    if (FindPath(RequestEntry.From, RequestEntry.To, RequestEntry.Path))
    {
        RequestEntry.Status = RequestStatus::Succeeded;
    }
    else
    {
        RequestEntry.Status = RequestStatus::Failed;
        RequestEntry.Path.clear();
    }
}

bool AIHTNDemoPathfinder::IsSameLocation(const Cell& inFrom, const Cell& inTo)
{
    return inFrom == inTo;
}

int32 AIHTNDemoPathfinder::RequestPath(
    const Cell& inFrom,
    const Cell& inTo)
{
    // Type/shape conversion has already been performed by HTN_CALLTERM_BIND.
    // Grid bounds are pathfinder semantics, so they remain a responsibility of
    // this subsystem rather than of the generic HTN conversion layer.
    if (!mWorldState || !mTerrain.IsInside(inFrom) || !mTerrain.IsInside(inTo))
        return 0;

    HTNWorldState& WorldState = *mWorldState;

    // Re-requesting the same path must not restart its countdown. This happens
    // naturally when the agent replans while the asynchronous query is running.
    if (mRequest && mRequest->From == inFrom && mRequest->To == inTo)
    {
        WriteRequestFacts(WorldState, *mRequest);
        return mRequest->Id;
    }

    Request NewRequest;
    NewRequest.Id = mNextRequestId++;
    NewRequest.From = inFrom;
    NewRequest.To = inTo;
    NewRequest.TicksRemaining =
        std::abs(inTo.X - inFrom.X) + std::abs(inTo.Y - inFrom.Y);

    // A zero-distance query requires zero asynchronous ticks. Resolve it now so
    // its published state is immediately coherent.
    if (NewRequest.TicksRemaining == 0)
    {
        if (FindPath(NewRequest.From, NewRequest.To, NewRequest.Path))
        {
            NewRequest.Status = RequestStatus::Succeeded;
        }
        else
        {
            NewRequest.Status = RequestStatus::Failed;
        }
    }

    // The demo pathfinder intentionally owns only one request. A new request
    // replaces the previous one completely, including its published facts.
    mRequest = std::move(NewRequest);

    (void)WorldState.ClearFact(sPathfindState, 4u);
    (void)WorldState.ClearFact(sPathfinder, 4u);
    (void)WorldState.ClearFact(sPathfindingSegment, 3u);

    // The callterm can run during a decomposition before the next daemon world-state
    // publication. Publish the new request immediately so the planning pass sees it.
    WriteRequestFacts(WorldState, *mRequest);

    return mRequest->Id;
}

HTNAtomOwner AIHTNDemoPathfinder::MakeCellAtom(const Cell& inCell)
{
    HTNAtomOwner Atom;
    const bool Converted = HTNTryToAtom(inCell, *Atom.Get());
    assert(Converted);
    return Atom;
}

bool AIHTNDemoPathfinder::FindPath(
    const Cell& inStart,
    const Cell& inGoal,
    std::vector<Cell>& outPath) const
{
    struct OpenNode
    {
        int Index = 0;
        int F = 0;
    };

    struct Greater
    {
        bool operator()(const OpenNode& inA, const OpenNode& inB) const
        {
            return inA.F > inB.F;
        }
    };

    const int GridWidth = mTerrain.GetWidth();
    const int GridHeight = mTerrain.GetHeight();
    const int CellCount = GridWidth * GridHeight;
    const auto ToIndex = [GridWidth](const int inX, const int inY)
    {
        return inY * GridWidth + inX;
    };

    const int StartIndex = ToIndex(inStart.X, inStart.Y);
    const int GoalIndex = ToIndex(inGoal.X, inGoal.Y);

    if (mTerrain.IsBlocked(inStart) || mTerrain.IsBlocked(inGoal))
    {
        return false;
    }

    if (StartIndex == GoalIndex)
    {
        outPath.clear();
        return true;
    }

    constexpr int Infinity = std::numeric_limits<int>::max();

    std::vector<int> GScore(static_cast<std::size_t>(CellCount), Infinity);
    std::vector<int> Parent(static_cast<std::size_t>(CellCount), -1);

    const auto Heuristic = [&inGoal](const int inX, const int inY)
    {
        return std::abs(inX - inGoal.X) + std::abs(inY - inGoal.Y);
    };

    std::priority_queue<OpenNode, std::vector<OpenNode>, Greater> Open;
    GScore[StartIndex] = 0;
    Open.push({StartIndex, Heuristic(inStart.X, inStart.Y)});

    constexpr int Directions[4][2] =
    {
        { 1,  0},
        {-1,  0},
        { 0,  1},
        { 0, -1}
    };

    while (!Open.empty())
    {
        const OpenNode Current = Open.top();
        Open.pop();

        if (Current.Index == GoalIndex)
        {
            break;
        }

        const int X = Current.Index % GridWidth;
        const int Y = Current.Index / GridWidth;

        for (const auto& Direction : Directions)
        {
            const int NextX = X + Direction[0];
            const int NextY = Y + Direction[1];

            if (NextX < 0 || NextX >= GridWidth ||
                NextY < 0 || NextY >= GridHeight)
            {
                continue;
            }

            const int NextIndex = ToIndex(NextX, NextY);
            if (mTerrain.IsBlocked({static_cast<int32>(NextX), static_cast<int32>(NextY)}))
            {
                continue;
            }

            const int TentativeG = GScore[Current.Index] + 1;
            if (TentativeG >= GScore[NextIndex])
            {
                continue;
            }

            GScore[NextIndex] = TentativeG;
            Parent[NextIndex] = Current.Index;
            Open.push({NextIndex, TentativeG + Heuristic(NextX, NextY)});
        }
    }

    if (GScore[GoalIndex] == Infinity)
    {
        return false;
    }

    std::vector<Cell> RawPath;
    for (int Index = GoalIndex; Index != -1; Index = Parent[Index])
    {
        RawPath.push_back({
            static_cast<int32>(Index % GridWidth),
            static_cast<int32>(Index / GridWidth)});
    }

    std::reverse(RawPath.begin(), RawPath.end());

    // A* operates on the 4-connected grid and therefore produces one node per
    // crossed cell. Those nodes are search implementation detail, not useful
    // movement segments. Collapse every run that has direct visibility into a
    // single endpoint before publishing the path to the HTN.
    StringPullPath(RawPath, outPath);
    return true;
}

bool AIHTNDemoPathfinder::HasLineOfSight(
    const Cell& inFrom,
    const Cell& inTo) const
{
    if (!mTerrain.IsInside(inFrom) ||
        !mTerrain.IsInside(inTo) ||
        mTerrain.IsBlocked(inFrom) ||
        mTerrain.IsBlocked(inTo))
    {
        return false;
    }

    int X = inFrom.X;
    int Y = inFrom.Y;

    const int DeltaX = std::abs(inTo.X - inFrom.X);
    const int DeltaY = std::abs(inTo.Y - inFrom.Y);
    const int StepX = (inTo.X > inFrom.X) ? 1 : ((inTo.X < inFrom.X) ? -1 : 0);
    const int StepY = (inTo.Y > inFrom.Y) ? 1 : ((inTo.Y < inFrom.Y) ? -1 : 0);

    int CrossedX = 0;
    int CrossedY = 0;

    // Supercover grid traversal between cell centres. When the segment passes
    // exactly through a grid corner we conservatively require both side cells
    // to be free. This prevents a straight segment from clipping/cutting the
    // corner between blocked cells.
    while (CrossedX < DeltaX || CrossedY < DeltaY)
    {
        const int Decision =
            (1 + 2 * CrossedX) * DeltaY -
            (1 + 2 * CrossedY) * DeltaX;

        if (Decision == 0)
        {
            const Cell SideX{
                static_cast<int32>(X + StepX),
                static_cast<int32>(Y)};
            const Cell SideY{
                static_cast<int32>(X),
                static_cast<int32>(Y + StepY)};

            if ((StepX != 0 && mTerrain.IsBlocked(SideX)) ||
                (StepY != 0 && mTerrain.IsBlocked(SideY)))
            {
                return false;
            }

            X += StepX;
            Y += StepY;
            ++CrossedX;
            ++CrossedY;
        }
        else if (Decision < 0)
        {
            X += StepX;
            ++CrossedX;
        }
        else
        {
            Y += StepY;
            ++CrossedY;
        }

        const Cell CrossedCell{
            static_cast<int32>(X),
            static_cast<int32>(Y)};

        if (!mTerrain.IsInside(CrossedCell) || mTerrain.IsBlocked(CrossedCell))
        {
            return false;
        }
    }

    return true;
}

void AIHTNDemoPathfinder::StringPullPath(
    const std::vector<Cell>& inRawPath,
    std::vector<Cell>& outPath) const
{
    outPath.clear();

    if (inRawPath.size() <= 1u)
    {
        return;
    }

    std::size_t AnchorIndex = 0u;
    while (AnchorIndex + 1u < inRawPath.size())
    {
        // Greedily connect the current anchor to the furthest A* node that can
        // be reached in a straight line. Only that endpoint becomes an HTN
        // pathfinding_segment / walk_segment.
        std::size_t FurthestVisibleIndex = AnchorIndex + 1u;

        for (std::size_t CandidateIndex = inRawPath.size() - 1u;
             CandidateIndex > AnchorIndex + 1u;
             --CandidateIndex)
        {
            if (HasLineOfSight(inRawPath[AnchorIndex], inRawPath[CandidateIndex]))
            {
                FurthestVisibleIndex = CandidateIndex;
                break;
            }
        }

        outPath.push_back(inRawPath[FurthestVisibleIndex]);
        AnchorIndex = FurthestVisibleIndex;
    }
}

void AIHTNDemoPathfinder::WriteRequestFacts(
    HTNWorldState& ioWorldState,
    const Request& inRequest) const
{
    const HTNAtomOwner From = MakeCellAtom(inRequest.From);
    const HTNAtomOwner To = MakeCellAtom(inRequest.To);

    const HtnSymbol* State = nullptr;
    switch (inRequest.Status)
    {
    case RequestStatus::InProgress:
        State = sInProgress;
        break;

    case RequestStatus::Succeeded:
        State = sSucceeded;
        break;

    case RequestStatus::Failed:
        State = sFailed;
        break;
    }

    (void)ioWorldState.WriteFact(sPathfindState, State, From, To, inRequest.Id);

    if (inRequest.Status != RequestStatus::Succeeded)
    {
        return;
    }

    (void)ioWorldState.WriteFact(
        sPathfinder,
        inRequest.Id,
        static_cast<int32>(inRequest.Path.size()),
        From,
        To);

    WritePathSegments(ioWorldState, inRequest);
}

void AIHTNDemoPathfinder::WritePathSegments(
    HTNWorldState& ioWorldState,
    const Request& inRequest) const
{
    for (size Index = 0u; Index < inRequest.Path.size(); ++Index)
    {
        (void)ioWorldState.WriteFact(
            sPathfindingSegment,
            inRequest.Id,
            static_cast<int32>(Index),
            MakeCellAtom(inRequest.Path[Index]));
    }
}
