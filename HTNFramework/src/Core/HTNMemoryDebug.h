// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include <cstdint>

struct HTNMemoryDebugStats
{
    std::uint64_t liveHeapStrings = 0;
    std::uint64_t heapStringBytes = 0;
    std::uint64_t peakHeapStringBytes = 0;
    std::uint64_t heapStringAllocations = 0;
    std::uint64_t heapStringFrees = 0;

    std::uint64_t liveListNodes = 0;
    std::uint64_t peakLiveListNodes = 0;
    std::uint64_t newDeleteListNodes = 0;
    std::uint64_t pooledListNodes = 0;
    std::uint64_t pooledCapacityNodes = 0;
    std::uint64_t pooledAllocatorCount = 0;
    std::uint64_t listNodeAllocations = 0;
    std::uint64_t listNodeDeallocations = 0;
    std::uint64_t listNodeStorageBytes = 0;
    std::uint64_t pooledReservedBytes = 0;

    std::uint64_t trackedBytes = 0;

    std::uint32_t atomSize = 0;
    std::uint32_t atomListSize = 0;
    std::uint32_t atomNodeSize = 0;
};

HTNMemoryDebugStats HTNMemoryDebug_GetStats();
void HTNMemoryDebug_ResetPeaks();
