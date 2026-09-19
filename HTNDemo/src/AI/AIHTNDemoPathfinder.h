// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "AI/AIHtnDaemonBase.h"
#include "World/DemoGridTerrain.h"

#include <cstdint>
#include <optional>
#include <vector>

class HTNCallTermRegistry;
class HTNWorldState;

class AIHTNDemoPathfinder final : public AIHtnDaemonBase
{
public:
    explicit AIHTNDemoPathfinder(const DemoGridTerrain& inTerrain);

    static void BindCallTerms(HTNCallTermRegistry& ioRegistry);
    void OnWriteWorldState(HTNWorldState& ioWorldState) override;
    void Update(float inDeltaTime) override;

    // Debug/visualization helper: returns the remaining resolved path for the
    // current request, starting at inCurrentLocation and ending at destination.
    [[nodiscard]] bool GetRemainingPath(
        const Cell& inCurrentLocation,
        const Cell& inDestination,
        std::vector<Cell>& outPath) const;

private:
    enum class RequestStatus : std::uint8_t
    {
        InProgress,
        Succeeded,
        Failed
    };

    struct Request
    {
        int32 Id = 0;
        Cell From;
        Cell To;
        RequestStatus Status = RequestStatus::InProgress;
        int TicksRemaining = 0;
        std::vector<Cell> Path;
    };

    int32 RequestPath(const Cell& inFrom, const Cell& inTo);
    static bool IsSameLocation(const Cell& inFrom, const Cell& inTo);

    static HTNAtomOwner MakeCellAtom(const Cell& inCell);

    bool FindPath(const Cell& inStart, const Cell& inGoal, std::vector<Cell>& outPath) const;
    [[nodiscard]] bool HasLineOfSight(const Cell& inFrom, const Cell& inTo) const;
    void StringPullPath(const std::vector<Cell>& inRawPath, std::vector<Cell>& outPath) const;

    void WriteRequestFacts(HTNWorldState& ioWorldState, const Request& inRequest) const;
    void WritePathSegments(HTNWorldState& ioWorldState, const Request& inRequest) const;
    const DemoGridTerrain& mTerrain;
    std::optional<Request> mRequest;
    int32 mNextRequestId = 1;
};
