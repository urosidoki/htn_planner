// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "AI/AIHtnDaemonBase.h"

#include "Hook/HTNPlannerHook.h"
#include "WorldState/HTNWorldState.h"

void AIHtnDaemonBase::Initialize(HTNPlannerHook* inPlannerHook)
{
    mFactRegistry = inPlannerHook ? &inPlannerHook->GetFactRegistry() : nullptr;
    mWorldState = inPlannerHook ? &inPlannerHook->GetWorldState() : nullptr;
}

void AIHtnDaemonBase::WriteWorldState(HTNWorldState& ioWorldState)
{
    ioWorldState.SetFactRegistry(mFactRegistry);

    OnWriteWorldState(ioWorldState);
}
