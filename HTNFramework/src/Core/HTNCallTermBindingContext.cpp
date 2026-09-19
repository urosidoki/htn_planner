// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "Core/HTNCallTermBindingContext.h"

#include "Core/HTNCallTermRegistry.h"

HTNCallTermBindingContext::HTNCallTermBindingContext(const HTNCallTermRegistry& inRegistry)
    : mRegistry(inRegistry)
{
}

bool HTNCallTermBindingContext::SetDaemon(const std::string& inID, void* inDaemon)
{
    const std::size_t Slot = mRegistry.FindDaemonSlot(inID);
    if (Slot >= mDaemonSlots.size())
    {
        HTN_LOG_ERROR("Callterm daemon type [{}] is not registered", inID);
        return false;
    }

    mDaemonSlots[Slot] = inDaemon;
    return true;
}

const HTNCallTermRegistry& HTNCallTermBindingContext::GetRegistry() const
{
    return mRegistry;
}

void* HTNCallTermBindingContext::GetDaemon(const std::size_t inSlot) const
{
    return inSlot < mDaemonSlots.size() ? mDaemonSlots[inSlot] : nullptr;
}
