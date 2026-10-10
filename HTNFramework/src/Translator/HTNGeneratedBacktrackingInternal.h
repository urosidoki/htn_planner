// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNGeneratedBacktracking.h"

#include "Core/HTNAtomOwner.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

// Container allocations are independent of the allocator used by atom payloads.
struct HTNGeneratedBacktrackingMemory
{
    HTNBacktrackingAllocator Allocator{};
    HTNBacktrackingAllocationStats* Stats = nullptr;
    size_t ReservedBytes = 0u;

    void* Allocate(size_t inSize, size_t inAlignment)
    {
        if (Stats)
        {
            Stats->requested_bytes += inSize;
            Stats->largest_request_bytes = std::max(Stats->largest_request_bytes, inSize);
            Stats->max_alignment = std::max(Stats->max_alignment, inAlignment);
        }
        void* Memory = Allocator.allocate
            ? Allocator.allocate(Allocator.user_data, inSize, inAlignment)
            : ::operator new(inSize, std::align_val_t(inAlignment), std::nothrow);
        if (!Memory)
        {
            if (Stats) ++Stats->failed_allocation_count;
            return nullptr;
        }
        ReservedBytes += inSize;
        if (Stats)
        {
            ++Stats->allocation_count;
            Stats->current_bytes += inSize;
            Stats->peak_bytes = std::max(Stats->peak_bytes, Stats->current_bytes);
        }
        return Memory;
    }

    void Deallocate(void* inMemory, size_t inSize, size_t inAlignment)
    {
        if (Allocator.deallocate)
            Allocator.deallocate(Allocator.user_data, inMemory, inSize, inAlignment);
        else
            ::operator delete(inMemory, std::align_val_t(inAlignment));
        ReservedBytes -= inSize;
        if (Stats) Stats->current_bytes -= inSize;
    }
};

// Internal C++ storage used only by the generated backtracking overflow bridge.
// None of these container details cross the C ABI.
template <typename T, size_t BlockCapacity>
class HTNGeneratedLifoTrailArena
{
public:
    explicit HTNGeneratedLifoTrailArena(HTNGeneratedBacktrackingMemory& inMemory)
        : mMemory(inMemory), mActiveBlock(&mFirstBlock)
    {
    }

    ~HTNGeneratedLifoTrailArena()
    {
        Block* Current = mFirstBlock.Next;
        while (Current)
        {
            Block* Next = Current->Next;
            Current->~Block();
            mMemory.Deallocate(Current, sizeof(Block), alignof(Block));
            Current = Next;
        }
    }

    HTNGeneratedLifoTrailArena(const HTNGeneratedLifoTrailArena&) = delete;
    HTNGeneratedLifoTrailArena& operator=(const HTNGeneratedLifoTrailArena&) = delete;

    size_t size() const
    {
        return mSize;
    }

    bool empty() const
    {
        return mSize == 0u;
    }

    T& back()
    {
        return mActiveBlock->Entries[mActiveBlock->Used - 1u];
    }

    const T& back() const
    {
        return mActiveBlock->Entries[mActiveBlock->Used - 1u];
    }

    bool emplace_back(T&& inValue)
    {
        if (mActiveBlock->Used == BlockCapacity)
        {
            if (!mActiveBlock->Next)
            {
                void* Raw = mMemory.Allocate(sizeof(Block), alignof(Block));
                if (!Raw)
                    return false;
                Block* Next = new (Raw) Block();
                Next->Previous = mActiveBlock;
                mActiveBlock->Next = Next;
            }
            mActiveBlock = mActiveBlock->Next;
        }

        mActiveBlock->Entries[mActiveBlock->Used++] = std::move(inValue);
        ++mSize;
        return true;
    }

    T pop_back()
    {
        assert(mSize != 0u);
        T Value = std::move(mActiveBlock->Entries[mActiveBlock->Used - 1u]);
        mActiveBlock->Entries[mActiveBlock->Used - 1u] = T{};
        --mActiveBlock->Used;
        --mSize;
        if (mActiveBlock->Used == 0u && mActiveBlock->Previous)
            mActiveBlock = mActiveBlock->Previous;
        return Value;
    }

    void clear()
    {
        while (mSize != 0u)
            (void)pop_back();
    }

private:
    struct Block
    {
        std::array<T, BlockCapacity> Entries{};
        size_t Used = 0u;
        Block* Previous = nullptr;
        Block* Next = nullptr;
    };

    HTNGeneratedBacktrackingMemory& mMemory;
    Block mFirstBlock;
    Block* mActiveBlock = nullptr;
    size_t mSize = 0u;
};

struct HTNGeneratedPendingContinuation
{
    HTNGeneratedTaskContinuationFn Continuation = nullptr;
    uint32_t RestoreSnapshotCount = 0u;
    uint64_t VariableFrameId = 0u;
};

constexpr size_t HTN_GENERATED_PENDING_CONTINUATION_BLOCK_CAPACITY = 32u;

struct HTNGeneratedBacktrackingOverflow
{
    explicit HTNGeneratedBacktrackingOverflow(const HTNGeneratedBacktrackingMemory& inMemory)
        : Memory(inMemory), ContinuationSnapshots(Memory), PendingContinuations(Memory) {}

    struct ContinuationSnapshotEntry
    {
        uint32_t Variable = HTN_GENERATED_NO_INDEX;
        HTNAtomOwner Value;
    };

    HTNGeneratedBacktrackingMemory Memory;
    HTNGeneratedLifoTrailArena<ContinuationSnapshotEntry, HTN_GENERATED_MAX_VARIABLE_SLOTS> ContinuationSnapshots;
    HTNGeneratedLifoTrailArena<HTNGeneratedPendingContinuation, HTN_GENERATED_PENDING_CONTINUATION_BLOCK_CAPACITY> PendingContinuations;
};
