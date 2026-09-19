// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HtnSymbol.h"
#include "HTNCoreMinimal.h"

#include <limits>
#include <unordered_map>
#include <vector>

using HTNFactSlot = uint32;
static constexpr HTNFactSlot HTN_INVALID_FACT_SLOT = std::numeric_limits<HTNFactSlot>::max();

/**
 * Domain-specific mapping from interned fact symbols to compact slots.
 * Only facts referenced by the parsed domain are registered.
 *
 * Threading contract:
 * Reset()/Register() are configuration-time operations and are not synchronized
 * with readers. Once construction is complete, FindSlot()/GetSymbol()/GetSlotCount()
 * are read-only and the registry may be shared by concurrent planning units.
 */
class HTNFactRegistry final
{
public:
    void Reset()
    {
        mSlots.clear();
        mSymbols.clear();
    }

    HTNFactSlot Register(const HtnSymbol* inSymbol)
    {
        if (!inSymbol)
            return HTN_INVALID_FACT_SLOT;

        const auto Existing = mSlots.find(inSymbol);
        if (Existing != mSlots.end())
            return Existing->second;

        const HTNFactSlot Slot = static_cast<HTNFactSlot>(mSymbols.size());
        mSlots.emplace(inSymbol, Slot);
        mSymbols.emplace_back(inSymbol);
        return Slot;
    }

    HTN_NODISCARD HTNFactSlot FindSlot(const HtnSymbol* inSymbol) const
    {
        if (!inSymbol)
            return HTN_INVALID_FACT_SLOT;
        const auto It = mSlots.find(inSymbol);
        return It != mSlots.end() ? It->second : HTN_INVALID_FACT_SLOT;
    }

    HTN_NODISCARD const HtnSymbol* GetSymbol(const HTNFactSlot inSlot) const
    {
        return inSlot < mSymbols.size() ? mSymbols[inSlot] : nullptr;
    }

    HTN_NODISCARD uint32 GetSlotCount() const
    {
        return static_cast<uint32>(mSymbols.size());
    }

private:
    std::unordered_map<const HtnSymbol*, HTNFactSlot> mSlots;
    std::vector<const HtnSymbol*> mSymbols;
};
