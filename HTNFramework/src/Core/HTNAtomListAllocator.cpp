// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "Core/HTNAtomListAllocator.h"

#include "Core/HTNAtomNode.h"

#include <atomic>
#include <cassert>
#include <new>


#if defined(HTN_MEMORY_ATOM_DIAGNOSTICS) && !defined(HTN_MEMORY_LIST_DIAGNOSTICS)
#define HTN_MEMORY_LIST_DIAGNOSTICS
#endif

namespace
{
#ifdef HTN_MEMORY_LIST_DIAGNOSTICS
std::atomic<std::uint64_t> GHTNListLiveNodes{0};
std::atomic<std::uint64_t> GHTNListPeakLiveNodes{0};
std::atomic<std::uint64_t> GHTNNewDeleteLiveNodes{0};
std::atomic<std::uint64_t> GHTNPooledLiveNodes{0};
std::atomic<std::uint64_t> GHTNPooledCapacityNodes{0};
std::atomic<std::uint64_t> GHTNPooledAllocatorCount{0};
std::atomic<std::uint64_t> GHTNListNodeAllocations{0};
std::atomic<std::uint64_t> GHTNListNodeDeallocations{0};
std::atomic<std::uint64_t> GHTNPooledReservedBytes{0};

void UpdatePeak(std::atomic<std::uint64_t>& ioPeak, const std::uint64_t inValue)
{
    std::uint64_t Peak = ioPeak.load(std::memory_order_relaxed);
    while (Peak < inValue && !ioPeak.compare_exchange_weak(Peak, inValue, std::memory_order_relaxed)) {}
}

void TrackListNodeAllocated(const bool inPooled)
{
    GHTNListNodeAllocations.fetch_add(1u, std::memory_order_relaxed);
    const std::uint64_t Live = GHTNListLiveNodes.fetch_add(1u, std::memory_order_relaxed) + 1u;
    UpdatePeak(GHTNListPeakLiveNodes, Live);
    if (inPooled) GHTNPooledLiveNodes.fetch_add(1u, std::memory_order_relaxed);
    else GHTNNewDeleteLiveNodes.fetch_add(1u, std::memory_order_relaxed);
}

void TrackListNodeDeallocated(const bool inPooled)
{
    GHTNListNodeDeallocations.fetch_add(1u, std::memory_order_relaxed);
    GHTNListLiveNodes.fetch_sub(1u, std::memory_order_relaxed);
    if (inPooled) GHTNPooledLiveNodes.fetch_sub(1u, std::memory_order_relaxed);
    else GHTNNewDeleteLiveNodes.fetch_sub(1u, std::memory_order_relaxed);
}
#endif
}

HTNNewDeleteAtomListAllocator& HTNNewDeleteAtomListAllocator::Get()
{
    static HTNNewDeleteAtomListAllocator Allocator;
    return Allocator;
}

HTNAtomNode* HTNNewDeleteAtomListAllocator::Allocate()
{
    auto* Node = static_cast<HTNAtomNode*>(::operator new(sizeof(HTNAtomNode), std::nothrow));
    if (!Node)
        return nullptr;

    Node->next_node = nullptr;
    Node->allocation_cookie = nullptr;
#ifdef HTN_MEMORY_LIST_DIAGNOSTICS
    TrackListNodeAllocated(false);
#endif
    return Node;
}

void HTNNewDeleteAtomListAllocator::Deallocate(HTNAtomNode* inNode)
{
    if (!inNode)
        return;
    HTNAtom_Destroy(&inNode->data);
#ifdef HTN_MEMORY_LIST_DIAGNOSTICS
    TrackListNodeDeallocated(false);
#endif
    ::operator delete(inNode);
}

HTNPooledAtomListAllocator::HTNPooledAtomListAllocator(const uint32 inCapacity) : mSlots(inCapacity)
{
#ifdef HTN_MEMORY_LIST_DIAGNOSTICS
    GHTNPooledAllocatorCount.fetch_add(1u, std::memory_order_relaxed);
    GHTNPooledCapacityNodes.fetch_add(inCapacity, std::memory_order_relaxed);
    GHTNPooledReservedBytes.fetch_add(static_cast<std::uint64_t>(mSlots.size() * sizeof(Slot)), std::memory_order_relaxed);
#endif
    for (uint32 Index = 0; Index < inCapacity; ++Index)
        mSlots[Index].mNextFree = Index + 1 < inCapacity ? Index + 1 : InvalidSlot;

    mFirstFree = inCapacity > 0 ? 0 : InvalidSlot;
}

HTNPooledAtomListAllocator::~HTNPooledAtomListAllocator()
{
    assert(0 == mAllocatedNodeCount && "All pooled HTNAtomList instances must be destroyed before their allocator");
#ifdef HTN_MEMORY_LIST_DIAGNOSTICS
    GHTNPooledReservedBytes.fetch_sub(static_cast<std::uint64_t>(mSlots.size() * sizeof(Slot)), std::memory_order_relaxed);
    GHTNPooledCapacityNodes.fetch_sub(static_cast<std::uint64_t>(mSlots.size()), std::memory_order_relaxed);
    GHTNPooledAllocatorCount.fetch_sub(1u, std::memory_order_relaxed);
#endif
}

HTNAtomNode* HTNPooledAtomListAllocator::Allocate()
{
    if (InvalidSlot == mFirstFree)
        return nullptr;

    const uint32 SlotIndex = mFirstFree;
    Slot&        FreeSlot  = mSlots[SlotIndex];
    mFirstFree = FreeSlot.mNextFree;
    FreeSlot.mNextFree    = InvalidSlot;
    FreeSlot.mIsAllocated = true;
    ++mAllocatedNodeCount;

    auto* Node = reinterpret_cast<HTNAtomNode*>(FreeSlot.mStorage);
    Node->next_node = nullptr;
    Node->allocation_cookie = &FreeSlot;
#ifdef HTN_MEMORY_LIST_DIAGNOSTICS
    TrackListNodeAllocated(true);
#endif
    return Node;
}

void HTNPooledAtomListAllocator::Deallocate(HTNAtomNode* inNode)
{
    if (!inNode)
        return;

    auto* FreedSlot = static_cast<Slot*>(inNode->allocation_cookie);
    assert(FreedSlot && FreedSlot->mIsAllocated);

    HTNAtom_Destroy(&inNode->data);
    FreedSlot->mIsAllocated = false;
    FreedSlot->mNextFree    = mFirstFree;
    mFirstFree = static_cast<uint32>(FreedSlot - mSlots.data());
    --mAllocatedNodeCount;
#ifdef HTN_MEMORY_LIST_DIAGNOSTICS
    TrackListNodeDeallocated(true);
#endif
}

HTNAtomListAllocatorDebugStats HTNAtomListAllocatorDebug_GetStats()
{
    HTNAtomListAllocatorDebugStats Stats{};
#ifdef HTN_MEMORY_LIST_DIAGNOSTICS
    Stats.live_nodes = GHTNListLiveNodes.load(std::memory_order_relaxed);
    Stats.peak_live_nodes = GHTNListPeakLiveNodes.load(std::memory_order_relaxed);
    Stats.new_delete_live_nodes = GHTNNewDeleteLiveNodes.load(std::memory_order_relaxed);
    Stats.pooled_live_nodes = GHTNPooledLiveNodes.load(std::memory_order_relaxed);
    Stats.pooled_capacity_nodes = GHTNPooledCapacityNodes.load(std::memory_order_relaxed);
    Stats.pooled_allocator_count = GHTNPooledAllocatorCount.load(std::memory_order_relaxed);
    Stats.node_allocations = GHTNListNodeAllocations.load(std::memory_order_relaxed);
    Stats.node_deallocations = GHTNListNodeDeallocations.load(std::memory_order_relaxed);
    Stats.pooled_reserved_bytes = GHTNPooledReservedBytes.load(std::memory_order_relaxed);
#endif
    return Stats;
}

void HTNAtomListAllocatorDebug_ResetPeaks()
{
#ifdef HTN_MEMORY_LIST_DIAGNOSTICS
    GHTNListPeakLiveNodes.store(GHTNListLiveNodes.load(std::memory_order_relaxed), std::memory_order_relaxed);
#endif
}

uint32 HTNPooledAtomListAllocator::GetCapacity() const
{
    return static_cast<uint32>(mSlots.size());
}

uint32 HTNPooledAtomListAllocator::GetAvailableNodeCount() const
{
    return GetCapacity() - mAllocatedNodeCount;
}

uint32 HTNPooledAtomListAllocator::GetAllocatedNodeCount() const
{
    return mAllocatedNodeCount;
}
