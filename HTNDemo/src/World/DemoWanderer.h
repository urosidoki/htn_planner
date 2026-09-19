// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "World/DemoGridTerrain.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Persistent gameplay state for one civilian. This is plain demo/game state,
// not a daemon and not HTN-specific. Daemons observe it and publish knowledge.
class DemoWanderer
{
public:
    enum class State : std::uint8_t
    {
        Walking,
        Contextual
    };

    explicit DemoWanderer(const DemoGridTerrain& inTerrain);

    void Reset(std::size_t inWaypointIndex = 0u);
    void NotifyWalkSegmentCompleted(const Cell& inLocation);
    void NotifyContextualAnimationCompleted();

    [[nodiscard]] State GetState() const { return mState; }
    [[nodiscard]] const char* GetStateName() const;
    [[nodiscard]] const Cell& GetCurrentLocation() const { return mCurrentLocation; }
    [[nodiscard]] const Cell& GetDestination() const { return mDestination; }
    [[nodiscard]] float GetMoveSpeedCellsPerSecond() const { return mMoveSpeedCellsPerSecond; }
    void SetMoveSpeedCellsPerSecond(float inSpeed);
    [[nodiscard]] const char* GetCurrentContextAnimationName() const;
    [[nodiscard]] std::uint64_t GetJourneyCount() const { return mJourneyCount; }
    [[nodiscard]] static constexpr std::size_t GetWaypointCount() { return WaypointCount; }
    [[nodiscard]] Cell GetWaypointLocation(std::size_t inIndex) const;

private:
    struct Waypoint
    {
        Cell Location;
    };

    // A slightly richer patrol loop makes the demo traverse the interior of
    // larger terrains instead of hugging the guaranteed-walkable border.
    static constexpr std::size_t WaypointCount = 12u;

    void BuildWaypoints();

    void ArriveAtDestination();
    void SelectNextDestination();
    const Waypoint& GetDestinationWaypoint() const;

    const DemoGridTerrain& mTerrain;
    std::array<Waypoint, WaypointCount> mWaypoints{};
    State mState = State::Walking;
    Cell mCurrentLocation{0, 0};
    Cell mDestination{0, 0};
    float mMoveSpeedCellsPerSecond = 5.5f;
    std::size_t mDestinationWaypointIndex = 1u;
    std::uint64_t mJourneyCount = 0u;
};
