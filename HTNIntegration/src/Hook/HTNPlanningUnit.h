// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNPlannerExecutionContext.h"

#include "Core/HtnSymbol.h"
#include "Core/HTNBacktrackingMode.h"
#include "Core/HTNAtomCpp.h"
#include "Core/HTNDecompositionStatus.h"
#include "Hook/HTNGeneratedPlanResult.h"
#include "Hook/HTNPrimitiveTaskResolution.h"
#include "Core/HTNTask.h"
#include "HTNCoreMinimal.h"
#include "Translator/HTNAllocationTrace.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#ifdef HTN_GENERATED_EXECUTION_PROFILING
#include "Translator/HTNGeneratedProfiling.h"

struct HTNPlanningUnitGeneratedTimingBreakdown
{
    double EnsureExecutionStorageMilliseconds = 0.0;
    double ContextConstructionMilliseconds = 0.0;
    double DecompositionMilliseconds = 0.0;
    double DecompositionCopyMilliseconds = 0.0;
    bool ExecutionStorageCreated = false;
    HTNGeneratedPreparationTimingBreakdown Preparation{};
};
#endif


class HTNDatabaseHook;
class HTNPlannerHook;
struct HTNGeneratedPlannerDefinition;
class HTNGeneratedDebugger;

// Planning unit structure that holds the planner hook and the database
class HTNPlanningUnit
{
public:
    explicit HTNPlanningUnit(HTNDatabaseHook& inDatabaseHook, HTNPlannerHook& inPlannerHook,
                             const std::string& inDefaultTopLevelMethodID);
    explicit HTNPlanningUnit(HTNDatabaseHook& inDatabaseHook, HTNPlannerHook& inPlannerHook,
                             const HtnSymbol* inDefaultTopLevelMethod);
    ~HTNPlanningUnit();

    HTNPlanningUnit(const HTNPlanningUnit&) = delete;
    HTNPlanningUnit& operator=(const HTNPlanningUnit&) = delete;

    // Planning units own per-agent generated execution storage, so copying would
    // accidentally duplicate ownership/state. Moving is safe and is required by
    // containers such as std::vector.
    HTNPlanningUnit(HTNPlanningUnit&& inOther) noexcept;
    HTNPlanningUnit& operator=(HTNPlanningUnit&&) = delete;

#ifdef HTN_DEBUG_DECOMPOSITION
    // Optional per-instance event debugger for generated execution. Caller retains ownership.
    void SetGeneratedDebugger(HTNGeneratedDebugger* inDebugger) { mExecutionContext.GeneratedDebugger = inDebugger; }
    HTN_NODISCARD HTNGeneratedDebugger* GetGeneratedDebugger() const { return mExecutionContext.GeneratedDebugger; }
#endif

    // Configure runtime options directly while idle. Call/world/storage fields are
    // supplied by the planning unit for each execution, including deferred calls.
    HTNPlannerExecutionContext& GetExecutionContext() { return mExecutionContext; }
    const HTNPlannerExecutionContext& GetExecutionContext() const { return mExecutionContext; }

    // Borrowed services copied into every execution, including deferred calls.
    // Configure only while idle; keep the payload alive while the plan is used.
    void SetClientContext(void* inClientContext) { mExecutionContext.ClientContext = inClientContext; }
    void* GetClientContext() const { return mExecutionContext.ClientContext; }

    // Decomposes a caller-owned top-level call and installs the active plan.
    // The call is borrowed during synchronous planning, never retained or modified.
    // Planning may invoke callterms; it does not execute the resulting primitives.
    HTNDecompositionStatus DecomposeTopLevelMethod(const HTNAtom& inCall);

    // Convenience overloads for zero-argument top-level methods.
    HTNDecompositionStatus DecomposeTopLevelMethod(const HtnSymbol* inTopLevelMethod);

    // Convenience overloads that create/destroy the transient call internally.
    template<typename... TArguments>
    HTNDecompositionStatus DecomposeTopLevelMethod(const HtnSymbol* inTopLevelMethod, TArguments&&... inArguments);

    // Decomposes the default top-level method of the domain.
    HTNDecompositionStatus DecomposeTopLevelMethod();

    // Returns the database hook
    HTN_NODISCARD const HTNDatabaseHook& GetDatabaseHook() const;

    // Returns the planner hook
    HTN_NODISCARD const HTNPlannerHook& GetPlannerHook() const;

    // Returns the default top-level method.
    HTN_NODISCARD const HtnSymbol* GetDefaultTopLevelMethod() const;
    HTN_NODISCARD const std::string& GetDefaultTopLevelMethodID() const;

    // Selects which forms of semantic backtracking this planning unit may use at runtime.
    // The generated planner does not change and does not need to be regenerated.
    void SetBacktrackingMode(HTNBacktrackingMode inBacktrackingMode);
    HTN_NODISCARD HTNBacktrackingMode GetBacktrackingMode() const;

    // Returns the last decomposition.
    HTN_NODISCARD const HTNGeneratedPlanResult& GetLastDecomposition() const;

    // The planning unit owns the active plan produced by DecomposeTopLevelMethod.
    // ResolveCurrentPrimitiveTask expands any deferred calls at the current slot
    // until either a primitive task is ready, the plan is complete, or a deferred
    // decomposition fails. Consumers never need to inspect or splice &calls.
    HTNPrimitiveTaskResolution ResolveCurrentPrimitiveTask();
    HTN_NODISCARD const HTNAtomOwner* GetCurrentPrimitiveTask() const;
    void CompleteCurrentPrimitiveTask();
    void ClearCurrentPlan();
    HTN_NODISCARD const std::vector<HTNAtomOwner>& GetCurrentPlan() const;
    HTN_NODISCARD std::size_t GetCurrentPrimitiveTaskIndex() const;
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    HTN_NODISCARD const HTNPlanningUnitGeneratedTimingBreakdown& GetLastGeneratedTimingBreakdown() const;
    HTN_NODISCARD const HTNGeneratedStructuralCounters& GetLastGeneratedStructuralCounters() const;
#endif

private:
    // Deferred targets are an internal execution detail. Consumers may enter a
    // domain only through an explicit top-level method.
    HTNDecompositionStatus ExecuteCall(const HTNAtom& inCall, bool inRequireTopLevel, HTNGeneratedPlanResult& outDecomposition);
    void SetCurrentPlanFromLastDecomposition();
    bool EnsureGeneratedExecutionStorage();

    HTNPlannerExecutionContext mExecutionContext{};
    HTNDatabaseHook& mDatabaseHook;
    HTNPlannerHook&        mPlannerHook;
    const HtnSymbol*        mDefaultTopLevelMethod = nullptr;

    HTNGeneratedPlanResult mLastDecomposition;
    std::vector<HTNAtomOwner> mCurrentPlan;
    std::size_t mCurrentPrimitiveTaskIndex = 0u;

    // Mutable generated execution scratch belongs to the planning unit (one AI/job),
    // never to HTNPlannerHook or the immutable generated definition. It is reused across
    // DecomposeTopLevelMethod calls so steady-state planning can reuse its buffers.
    void* mGeneratedExecutionStorage = nullptr;
    const HTNGeneratedPlannerDefinition* mGeneratedExecutionDefinition = nullptr;
#ifdef HTN_DEBUG_DECOMPOSITION
#endif
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    HTNPlanningUnitGeneratedTimingBreakdown mLastGeneratedTimingBreakdown;
    HTNGeneratedStructuralCounters mLastGeneratedStructuralCounters{};
    bool mGeneratedExecutionStorageCreatedThisExecution = false;
#endif

};

template<typename... TArguments>
inline HTNDecompositionStatus HTNPlanningUnit::DecomposeTopLevelMethod(
    const HtnSymbol* inTopLevelMethod, TArguments&&... inArguments)
{
    HTNAllocationTrace::PhaseScope AllocationPhase(HTNAllocationTrace::Phase::CallConstruction);
    if (!inTopLevelMethod)
        return HTN_DECOMPOSITION_INVALID_CALL;

    HTNAtom Call = HTNAtom::sCreateCallWithContext(mExecutionContext.ClientContext, inTopLevelMethod, std::forward<TArguments>(inArguments)...);
    if (!HTNAtom_IsBound(&Call))
    {
        HTNAtom::sDestroy(Call);
        return HTN_DECOMPOSITION_OUT_OF_MEMORY;
    }

    const HTNDecompositionStatus Result = DecomposeTopLevelMethod(Call);
    HTNAtom::sDestroy(Call);
    return Result;
}

inline const HTNDatabaseHook& HTNPlanningUnit::GetDatabaseHook() const
{
    return mDatabaseHook;
}

inline const HTNPlannerHook& HTNPlanningUnit::GetPlannerHook() const
{
    return mPlannerHook;
}

inline const HtnSymbol* HTNPlanningUnit::GetDefaultTopLevelMethod() const
{
    return mDefaultTopLevelMethod;
}

inline const std::string& HTNPlanningUnit::GetDefaultTopLevelMethodID() const
{
    static const std::string Empty;
    return mDefaultTopLevelMethod ? mDefaultTopLevelMethod->GetString() : Empty;
}

inline void HTNPlanningUnit::SetBacktrackingMode(const HTNBacktrackingMode inBacktrackingMode)
{
    mExecutionContext.BacktrackingMode = inBacktrackingMode;
}

inline HTNBacktrackingMode HTNPlanningUnit::GetBacktrackingMode() const
{
    return mExecutionContext.BacktrackingMode;
}

inline const HTNGeneratedPlanResult& HTNPlanningUnit::GetLastDecomposition() const
{
    return mLastDecomposition;
}

inline const HTNAtomOwner* HTNPlanningUnit::GetCurrentPrimitiveTask() const
{
    if (mCurrentPrimitiveTaskIndex >= mCurrentPlan.size())
        return nullptr;

    const HTNAtomOwner& Step = mCurrentPlan[mCurrentPrimitiveTaskIndex];
    return HTNGetPlanStepKind(Step) == HTNPlanStepKind::PrimitiveTask ? &Step : nullptr;
}

inline const std::vector<HTNAtomOwner>& HTNPlanningUnit::GetCurrentPlan() const
{
    return mCurrentPlan;
}

inline std::size_t HTNPlanningUnit::GetCurrentPrimitiveTaskIndex() const
{
    return mCurrentPrimitiveTaskIndex;
}

#ifdef HTN_GENERATED_EXECUTION_PROFILING
inline const HTNPlanningUnitGeneratedTimingBreakdown& HTNPlanningUnit::GetLastGeneratedTimingBreakdown() const
{
    return mLastGeneratedTimingBreakdown;
}

inline const HTNGeneratedStructuralCounters& HTNPlanningUnit::GetLastGeneratedStructuralCounters() const
{
    return mLastGeneratedStructuralCounters;
}
#endif
