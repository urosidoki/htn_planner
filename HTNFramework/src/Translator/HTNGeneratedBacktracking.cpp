// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNGeneratedBacktracking.h"
#include "Translator/HTNGeneratedBacktrackingInternal.h"
#include "Translator/HTNAllocationTrace.h"

#include "Core/HTNAtom.h"

#include <cstdint>
#include <new>
#include <utility>

extern "C" HTNGeneratedBacktrackingOverflow* HTNGeneratedBacktracking_CreateOverflow(void)
{
    return new (std::nothrow) HTNGeneratedBacktrackingOverflow();
}

extern "C" void HTNGeneratedBacktracking_ResetOverflow(HTNGeneratedBacktrackingOverflow* inOverflow)
{
    if (!inOverflow)
        return;
    inOverflow->PendingContinuations.clear();
    inOverflow->ContinuationSnapshots.clear();
}

extern "C" void HTNGeneratedBacktracking_DestroyOverflow(HTNGeneratedBacktrackingOverflow* inOverflow)
{
    delete inOverflow;
}

extern "C" int HTNGeneratedBacktracking_PushContinuationSnapshotOverflow(
    HTNGeneratedBacktrackingOverflow* inOverflow,
    const uint32_t inVariableSlot,
    const HTNAtom* inValue)
{
    if (!inOverflow)
        return 0;

    HTNGeneratedBacktrackingOverflow::ContinuationSnapshotEntry Entry;
    Entry.Variable = inVariableSlot;
    if (inValue)
        Entry.Value = *inValue;

    HTNAllocationTrace::Scope AllocationScope(HTNAllocationTrace::Source::PendingContinuationSnapshot);
    return inOverflow->ContinuationSnapshots.emplace_back(std::move(Entry)) ? 1 : 0;
}

extern "C" int HTNGeneratedBacktracking_PushPendingContinuationOverflow(
    HTNGeneratedBacktrackingOverflow* inOverflow,
    const uint64_t inVariableFrameId,
    const HTNGeneratedTaskContinuationFn inContinuation,
    const uint32_t inRestoreSnapshotCount)
{
    if (!inOverflow || !inContinuation)
        return 0;

    return inOverflow->PendingContinuations.emplace_back(HTNGeneratedPendingContinuation{
        inContinuation,
        inRestoreSnapshotCount,
        inVariableFrameId
    }) ? 1 : 0;
}

extern "C" HTNGeneratedTaskContinuationFn HTNGeneratedBacktracking_PopPendingContinuationOverflow(
    HTNGeneratedBacktrackingOverflow* inOverflow,
    uint64_t* outVariableFrameId,
    uint32_t* outRestoreSnapshotCount)
{
    if (!inOverflow || !outVariableFrameId || !outRestoreSnapshotCount)
        return nullptr;

    if (inOverflow->PendingContinuations.empty())
        return nullptr;

    const HTNGeneratedPendingContinuation Pending = inOverflow->PendingContinuations.pop_back();
    *outVariableFrameId = Pending.VariableFrameId;
    *outRestoreSnapshotCount = Pending.RestoreSnapshotCount;
    return Pending.Continuation;
}

extern "C" void HTNGeneratedBacktracking_PopContinuationSnapshotOverflow(
    HTNGeneratedBacktrackingOverflow* inOverflow,
    uint32_t* outVariableSlot,
    HTNAtom* outValue)
{
    if (!inOverflow || !outVariableSlot || !outValue)
        return;

    HTNGeneratedBacktrackingOverflow::ContinuationSnapshotEntry Entry =
        inOverflow->ContinuationSnapshots.pop_back();
    *outVariableSlot = Entry.Variable;
    HTNAtom_Move(outValue, Entry.Value.Get());
}
