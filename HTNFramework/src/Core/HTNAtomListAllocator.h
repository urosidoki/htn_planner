// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include "Core/HTNAtomNode.h"
#include "HTNCoreMinimal.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

struct HTNAtom;


struct HTNAtomListAllocatorDebugStats
{
    std::uint64_t live_nodes = 0;
    std::uint64_t peak_live_nodes = 0;
    std::uint64_t new_delete_live_nodes = 0;
    std::uint64_t pooled_live_nodes = 0;
    std::uint64_t pooled_capacity_nodes = 0;
    std::uint64_t pooled_allocator_count = 0;
    std::uint64_t node_allocations = 0;
    std::uint64_t node_deallocations = 0;
    std::uint64_t pooled_reserved_bytes = 0;
};

HTNAtomListAllocatorDebugStats HTNAtomListAllocatorDebug_GetStats();
void HTNAtomListAllocatorDebug_ResetPeaks();

/**
 * Allocation policy used by HTNAtomList. Implement this interface to store list nodes in a
 * client-owned arena, frame allocator, fixed pool, or any other engine-specific storage.
 * The allocator must outlive every HTNAtomList that references it.
 */
class HTNAtomListAllocator
{
public:
    virtual ~HTNAtomListAllocator() = default;

    // Allocates raw node storage. HTNAtomList owns construction/destruction of node data.
    HTN_NODISCARD virtual HTNAtomNode* Allocate() = 0;
    virtual void Deallocate(HTNAtomNode* inNode) = 0;
};

/** Baseline allocator used to compare the pooled implementation against one new/delete per node. */
class HTNNewDeleteAtomListAllocator final : public HTNAtomListAllocator
{
public:
    HTN_NODISCARD static HTNNewDeleteAtomListAllocator& Get();

    HTN_NODISCARD HTNAtomNode* Allocate() final;
    void Deallocate(HTNAtomNode* inNode) final;
};

/**
 * Fixed-capacity, linked-node pool. All backing storage is allocated by the constructor;
 * Allocate/Deallocate perform no dynamic memory allocation afterwards.
 */
class HTNPooledAtomListAllocator final : public HTNAtomListAllocator
{
public:
    explicit HTNPooledAtomListAllocator(uint32 inCapacity);
    ~HTNPooledAtomListAllocator() final;

    HTNPooledAtomListAllocator(const HTNPooledAtomListAllocator&)            = delete;
    HTNPooledAtomListAllocator& operator=(const HTNPooledAtomListAllocator&) = delete;
    HTNPooledAtomListAllocator(HTNPooledAtomListAllocator&&)                 = delete;
    HTNPooledAtomListAllocator& operator=(HTNPooledAtomListAllocator&&)      = delete;

    HTN_NODISCARD HTNAtomNode* Allocate() final;
    void Deallocate(HTNAtomNode* inNode) final;

    HTN_NODISCARD uint32 GetCapacity() const;
    HTN_NODISCARD uint32 GetAvailableNodeCount() const;
    HTN_NODISCARD uint32 GetAllocatedNodeCount() const;

private:
    static constexpr uint32 InvalidSlot = std::numeric_limits<uint32>::max();

    struct Slot
    {
        alignas(HTNAtomNode) std::byte mStorage[sizeof(HTNAtomNode)];
        uint32                         mNextFree   = InvalidSlot;
        bool                           mIsAllocated = false;
    };

    std::vector<Slot> mSlots;
    uint32            mFirstFree          = InvalidSlot;
    uint32            mAllocatedNodeCount = 0;
};
