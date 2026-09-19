// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "AI/AIHtnDaemonBase.h"

class DemoGridTerrain;
class HTNWorldState;

// Publishes shared environmental affordances from DemoGridTerrain into one
// planner/world-state. The terrain owns the data; this daemon only mirrors it.
class AIHTNDemoGridTerrainDaemon final : public AIHtnDaemonBase
{
public:
    explicit AIHTNDemoGridTerrainDaemon(const DemoGridTerrain& inTerrain);

    void Update(float inDeltaTime) override;

protected:
    void OnWriteWorldState(HTNWorldState& ioWorldState) override;

private:
    const DemoGridTerrain& mTerrain;
};
