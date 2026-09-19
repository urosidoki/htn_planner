// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "Core/HTNMemoryDebug.h"

#include "Core/HTNAtomC.h"
#include "Core/HTNAtomListAllocator.h"

HTNMemoryDebugStats HTNMemoryDebug_GetStats()
{
    HTNMemoryDebugStats Result{};

    Result.atomSize = static_cast<std::uint32_t>(sizeof(HTNAtom));
    Result.atomListSize = static_cast<std::uint32_t>(sizeof(HTNAtomList));
    Result.atomNodeSize = static_cast<std::uint32_t>(sizeof(HTNAtomNode));

#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    const HTNAtomDebugStats AtomStats = HTNAtomDebug_GetStats();
    const HTNAtomListAllocatorDebugStats ListStats = HTNAtomListAllocatorDebug_GetStats();

    Result.liveHeapStrings = AtomStats.live_heap_strings;
    Result.heapStringBytes = AtomStats.live_heap_string_bytes;
    Result.peakHeapStringBytes = AtomStats.peak_heap_string_bytes;
    Result.heapStringAllocations = AtomStats.heap_string_allocations;
    Result.heapStringFrees = AtomStats.heap_string_frees;

    Result.liveListNodes = ListStats.live_nodes;
    Result.peakLiveListNodes = ListStats.peak_live_nodes;
    Result.newDeleteListNodes = ListStats.new_delete_live_nodes;
    Result.pooledListNodes = ListStats.pooled_live_nodes;
    Result.pooledCapacityNodes = ListStats.pooled_capacity_nodes;
    Result.pooledAllocatorCount = ListStats.pooled_allocator_count;
    Result.listNodeAllocations = ListStats.node_allocations;
    Result.listNodeDeallocations = ListStats.node_deallocations;
    Result.listNodeStorageBytes = ListStats.live_nodes * sizeof(HTNAtomNode);
    Result.pooledReservedBytes = ListStats.pooled_reserved_bytes;

    Result.trackedBytes = Result.heapStringBytes + Result.pooledReservedBytes +
        (Result.newDeleteListNodes * sizeof(HTNAtomNode));
#endif

    return Result;
}

void HTNMemoryDebug_ResetPeaks()
{
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    HTNAtomDebug_ResetPeaks();
    HTNAtomListAllocatorDebug_ResetPeaks();
#endif
}
