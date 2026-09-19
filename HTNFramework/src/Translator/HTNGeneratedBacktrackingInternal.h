// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNGeneratedBacktracking.h"

#include "Core/HTNAtomOwner.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

// Internal C++ storage used only by the generated backtracking overflow bridge.
// None of these container details cross the C ABI.
template <typename T, size_t BlockCapacity>
class HTNGeneratedLifoTrailArena
{
public:
    HTNGeneratedLifoTrailArena()
        : mActiveBlock(&mFirstBlock)
    {
    }

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
                std::unique_ptr<Block> Next(new (std::nothrow) Block());
                if (!Next)
                    return false;
                Next->Previous = mActiveBlock;
                mActiveBlock->Next = std::move(Next);
            }
            mActiveBlock = mActiveBlock->Next.get();
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
        std::unique_ptr<Block> Next;
    };

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
    struct ContinuationSnapshotEntry
    {
        uint32_t Variable = HTN_GENERATED_NO_INDEX;
        HTNAtomOwner Value;
    };

    HTNGeneratedLifoTrailArena<ContinuationSnapshotEntry, HTN_GENERATED_MAX_VARIABLE_SLOTS> ContinuationSnapshots;
    HTNGeneratedLifoTrailArena<HTNGeneratedPendingContinuation, HTN_GENERATED_PENDING_CONTINUATION_BLOCK_CAPACITY> PendingContinuations;
};

