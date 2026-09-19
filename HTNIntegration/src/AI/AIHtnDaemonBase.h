// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

class HTNPlannerHook;
class HTNFactRegistry;
class HTNWorldState;

/**
 * Base interface for C++ daemons that feed and update an HTN planner.
 *
 * The daemon does not own the planner hook passed to Initialize(). The caller
 * must guarantee that the planner hook outlives the daemon.
 */
class AIHtnDaemonBase
{
public:
    virtual ~AIHtnDaemonBase() = default;

    // Initializes the daemon with the planner hook it will interact with.
    void Initialize(HTNPlannerHook* inPlannerHook);

    // Writes the daemon-owned knowledge into the supplied world state. Before
    // dispatching to the derived daemon, binds the world state to this planner's
    // domain-specific fact registry.
    void WriteWorldState(HTNWorldState& ioWorldState);

    // Updates the daemon state.
    virtual void Update(float inDeltaTime) = 0;

protected:
    virtual void OnWriteWorldState(HTNWorldState& ioWorldState) = 0;

    const HTNFactRegistry* mFactRegistry = nullptr;
    HTNWorldState* mWorldState = nullptr;
};
