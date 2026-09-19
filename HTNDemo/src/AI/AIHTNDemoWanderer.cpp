// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "AI/AIHTNDemoWanderer.h"

#include "Core/HtnSymbol.h"
#include "World/DemoWanderer.h"
#include "WorldState/HTNWorldState.h"

namespace
{
const HtnSymbol* sWandererState = HtnSymbol::sGetSymbol("wanderer_state");
const HtnSymbol* sWandererLocation = HtnSymbol::sGetSymbol("wanderer_location");
const HtnSymbol* sWandererDestination = HtnSymbol::sGetSymbol("wanderer_destination");

const HtnSymbol* sWalking = HtnSymbol::sGetSymbol("walking");
const HtnSymbol* sContextual = HtnSymbol::sGetSymbol("contextual");
}

AIHTNDemoWanderer::AIHTNDemoWanderer(DemoWanderer& inWanderer)
    : mWanderer(inWanderer)
{
}

void AIHTNDemoWanderer::Update(const float inDeltaTime)
{
    (void)inDeltaTime;
}

void AIHTNDemoWanderer::OnWriteWorldState(HTNWorldState& ioWorldState)
{
    (void)ioWorldState.ClearFact(sWandererState, 1u);
    (void)ioWorldState.ClearFact(sWandererLocation, 1u);
    (void)ioWorldState.ClearFact(sWandererDestination, 1u);

    HTNAtomOwner CurrentLocationAtom;
    HTNAtomOwner DestinationAtom;
    (void)HTNTryToAtom(mWanderer.GetCurrentLocation(), *CurrentLocationAtom.Get());
    (void)HTNTryToAtom(mWanderer.GetDestination(), *DestinationAtom.Get());

    (void)ioWorldState.WriteFact(
        sWandererState,
        mWanderer.GetState() == DemoWanderer::State::Walking ? sWalking : sContextual);
    (void)ioWorldState.WriteFact(sWandererLocation, *CurrentLocationAtom.Get());

    if (mWanderer.GetState() == DemoWanderer::State::Walking)
        (void)ioWorldState.WriteFact(sWandererDestination, *DestinationAtom.Get());
}
