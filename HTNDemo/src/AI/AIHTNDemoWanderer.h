// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "AI/AIHtnDaemonBase.h"

class DemoWanderer;
class HTNWorldState;

// Publishes one DemoWanderer's persistent gameplay state into HTN knowledge.
// The daemon does not own or advance the Wanderer itself.
class AIHTNDemoWanderer final : public AIHtnDaemonBase
{
public:
    explicit AIHTNDemoWanderer(DemoWanderer& inWanderer);

    void Update(float inDeltaTime) override;

protected:
    void OnWriteWorldState(HTNWorldState& ioWorldState) override;

private:
    DemoWanderer& mWanderer;
};
