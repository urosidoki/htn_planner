// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "World/DemoWanderer.h"

#include <algorithm>
#include <array>
#include <queue>
#include <random>
#include <vector>

DemoWanderer::DemoWanderer(const DemoGridTerrain& inTerrain)
    : mTerrain(inTerrain)
{
    BuildWaypoints();
}

void DemoWanderer::BuildWaypoints()
{
    const int Width = mTerrain.GetWidth();
    const int Height = mTerrain.GetHeight();
    const std::size_t CellCount = static_cast<std::size_t>(Width * Height);

    const auto ToIndex = [Width](const Cell& inCell)
    {
        return static_cast<std::size_t>(inCell.Y * Width + inCell.X);
    };

    // Only use cells connected to the first traversable cell. Random terrain can
    // contain isolated walkable islands; choosing from one connected component
    // guarantees every consecutive waypoint can actually be reached by A*.
    Cell Start{0, 0};
    if (mTerrain.IsBlocked(Start))
    {
        bool FoundStart = false;
        for (int Y = 0; Y < Height && !FoundStart; ++Y)
        {
            for (int X = 0; X < Width; ++X)
            {
                if (!mTerrain.IsBlocked({X, Y}))
                {
                    Start = {X, Y};
                    FoundStart = true;
                    break;
                }
            }
        }
    }

    std::vector<bool> Visited(CellCount, false);
    std::vector<Cell> ReachableCells;
    ReachableCells.reserve(CellCount);

    std::queue<Cell> Pending;
    Pending.push(Start);
    Visited[ToIndex(Start)] = true;

    static constexpr std::array<Cell, 4> Directions = {{
        { 1,  0},
        {-1,  0},
        { 0,  1},
        { 0, -1},
    }};

    while (!Pending.empty())
    {
        const Cell Current = Pending.front();
        Pending.pop();
        ReachableCells.push_back(Current);

        for (const Cell& Direction : Directions)
        {
            const Cell Next{Current.X + Direction.X, Current.Y + Direction.Y};
            if (!mTerrain.IsInside(Next) || mTerrain.IsBlocked(Next))
                continue;

            const std::size_t NextIndex = ToIndex(Next);
            if (Visited[NextIndex])
                continue;

            Visited[NextIndex] = true;
            Pending.push(Next);
        }
    }

    // Deterministic for a given terrain size/content so runs remain reproducible.
    // The reachable cell count folds terrain topology into the seed as well.
    const std::uint32_t Seed =
        0x91E10DA5u ^
        (static_cast<std::uint32_t>(Width) * 73856093u) ^
        (static_cast<std::uint32_t>(Height) * 19349663u) ^
        static_cast<std::uint32_t>(ReachableCells.size() * 83492791u);
    std::mt19937 Generator(Seed);
    std::shuffle(ReachableCells.begin(), ReachableCells.end(), Generator);

    std::vector<Cell> Chosen;
    Chosen.reserve(WaypointCount);

    const auto AddUnique = [&Chosen](const Cell& inCell)
    {
        if (std::find(Chosen.begin(), Chosen.end(), inCell) != Chosen.end())
            return false;

        Chosen.push_back(inCell);
        return true;
    };

    // Keep a few contextual objects in the route so the richer world-state demo
    // still exercises contextual animations while the remaining points roam the map.
    std::vector<Cell> ReachableInteractables;
    for (const DemoGridInteractable& Interactable : mTerrain.GetInteractables())
    {
        if (mTerrain.IsInside(Interactable.Location) &&
            Visited[ToIndex(Interactable.Location)])
        {
            ReachableInteractables.push_back(Interactable.Location);
        }
    }
    std::shuffle(ReachableInteractables.begin(), ReachableInteractables.end(), Generator);

    const std::size_t ContextualWaypointCount =
        std::min<std::size_t>(3u, ReachableInteractables.size());
    for (std::size_t Index = 0; Index < ContextualWaypointCount; ++Index)
        AddUnique(ReachableInteractables[Index]);

    // Fill the rest with reachable cells from the whole terrain, including the
    // interior. This is intentionally not restricted to the border anymore.
    for (const Cell& Candidate : ReachableCells)
    {
        if (Chosen.size() >= WaypointCount)
            break;
        AddUnique(Candidate);
    }

    // Tiny maps may expose fewer unique traversable cells than WaypointCount.
    // Repeat the available route rather than leaving default (0,0) entries that
    // might be invalid for a custom terrain.
    if (Chosen.empty())
        Chosen.push_back(Start);

    for (std::size_t Index = 0; Index < WaypointCount; ++Index)
        mWaypoints[Index].Location = Chosen[Index % Chosen.size()];
}

void DemoWanderer::Reset(const std::size_t inWaypointIndex)
{
    const std::size_t CurrentIndex = inWaypointIndex % mWaypoints.size();
    mCurrentLocation = mWaypoints[CurrentIndex].Location;
    mDestinationWaypointIndex = (CurrentIndex + 1u) % mWaypoints.size();
    mDestination = GetDestinationWaypoint().Location;
    mState = State::Walking;
    mJourneyCount = 0u;
}

void DemoWanderer::NotifyWalkSegmentCompleted(const Cell& inLocation)
{
    if (mState != State::Walking)
        return;

    mCurrentLocation = inLocation;
    if (mCurrentLocation == mDestination)
        ArriveAtDestination();
}

void DemoWanderer::NotifyContextualAnimationCompleted()
{
    if (mState == State::Contextual)
        SelectNextDestination();
}

const char* DemoWanderer::GetStateName() const
{
    return mState == State::Walking ? "Walking" : "Contextual animation";
}

void DemoWanderer::SetMoveSpeedCellsPerSecond(const float inSpeed)
{
    mMoveSpeedCellsPerSecond = std::max(0.01f, inSpeed);
}

const char* DemoWanderer::GetCurrentContextAnimationName() const
{
    if (mState != State::Contextual)
        return nullptr;

    const DemoGridInteractable* Interactable = mTerrain.GetInteractableAt(mCurrentLocation);
    return Interactable ? Interactable->ContextAnimation : nullptr;
}

Cell DemoWanderer::GetWaypointLocation(const std::size_t inIndex) const
{
    return mWaypoints[inIndex % mWaypoints.size()].Location;
}

void DemoWanderer::ArriveAtDestination()
{
    ++mJourneyCount;

    if (mTerrain.GetInteractableAt(mCurrentLocation))
    {
        mState = State::Contextual;
        return;
    }

    SelectNextDestination();
}

void DemoWanderer::SelectNextDestination()
{
    mDestinationWaypointIndex = (mDestinationWaypointIndex + 1u) % mWaypoints.size();
    mDestination = GetDestinationWaypoint().Location;
    mState = State::Walking;
}

const DemoWanderer::Waypoint& DemoWanderer::GetDestinationWaypoint() const
{
    return mWaypoints[mDestinationWaypointIndex];
}
