// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "AI/AIHTNDemoGridTerrainDaemon.h"

#include "Core/HtnSymbol.h"
#include "World/DemoGridTerrain.h"
#include "WorldState/HTNWorldState.h"

namespace
{
const HtnSymbol* sContextualAnimationAt = HtnSymbol::sGetSymbol("contextual_animation_at");
}

AIHTNDemoGridTerrainDaemon::AIHTNDemoGridTerrainDaemon(const DemoGridTerrain& inTerrain)
    : mTerrain(inTerrain)
{
}

void AIHTNDemoGridTerrainDaemon::Update(const float inDeltaTime)
{
    (void)inDeltaTime;
}

void AIHTNDemoGridTerrainDaemon::OnWriteWorldState(HTNWorldState& ioWorldState)
{
    (void)ioWorldState.ClearFact(sContextualAnimationAt, 2u);

    for (const DemoGridInteractable& Interactable : mTerrain.GetInteractables())
    {
        HTNAtomOwner LocationAtom;
        if (!HTNTryToAtom(Interactable.Location, *LocationAtom.Get()))
            continue;

        (void)ioWorldState.WriteFact(
            sContextualAnimationAt,
            *LocationAtom.Get(),
            HtnSymbol::sGetSymbol(Interactable.ContextAnimation));
    }
}
