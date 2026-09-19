// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Hook/HTNPlanningUnit.h"

#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "Translator/HTNGeneratedProfiling.h"
#include "Translator/HTNAllocationTrace.h"

#ifdef HTN_GENERATED_EXECUTION_PROFILING
#include <chrono>
#endif
#include <new>
#include <utility>

HTNPlanningUnit::HTNPlanningUnit(HTNDatabaseHook& inDatabaseHook, HTNPlannerHook& inPlannerHook,
                                 const std::string& inDefaultTopLevelMethodID)
    : HTNPlanningUnit(inDatabaseHook, inPlannerHook, HtnSymbol::sGetSymbol(inDefaultTopLevelMethodID))
{
}

HTNPlanningUnit::HTNPlanningUnit(HTNDatabaseHook& inDatabaseHook, HTNPlannerHook& inPlannerHook,
                                 const HtnSymbol* inDefaultTopLevelMethod)
    : mDatabaseHook(inDatabaseHook)
    , mPlannerHook(inPlannerHook)
    , mDefaultTopLevelMethod(inDefaultTopLevelMethod)
{
}


HTNPlanningUnit::HTNPlanningUnit(HTNPlanningUnit&& inOther) noexcept
    : mDatabaseHook(inOther.mDatabaseHook)
    , mPlannerHook(inOther.mPlannerHook)
    , mDefaultTopLevelMethod(inOther.mDefaultTopLevelMethod)
    , mBacktrackingMode(inOther.mBacktrackingMode)
    , mLastDecomposition(std::move(inOther.mLastDecomposition))
    , mCurrentPlan(std::move(inOther.mCurrentPlan))
    , mCurrentPrimitiveTaskIndex(inOther.mCurrentPrimitiveTaskIndex)
    , mGeneratedExecutionStorage(std::exchange(inOther.mGeneratedExecutionStorage, nullptr))
    , mGeneratedExecutionDefinition(std::exchange(inOther.mGeneratedExecutionDefinition, nullptr))
#ifdef HTN_DEBUG_DECOMPOSITION
    , mGeneratedDebugger(std::exchange(inOther.mGeneratedDebugger, nullptr))
#endif
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    , mLastGeneratedTimingBreakdown(inOther.mLastGeneratedTimingBreakdown)
    , mLastGeneratedStructuralCounters(inOther.mLastGeneratedStructuralCounters)
    , mGeneratedExecutionStorageCreatedThisExecution(inOther.mGeneratedExecutionStorageCreatedThisExecution)
#endif
#ifdef HTN_DEBUG_DECOMPOSITION
#endif
{
}

HTNPlanningUnit::~HTNPlanningUnit()
{
    if (mGeneratedExecutionStorage && mGeneratedExecutionDefinition)
    {
        mGeneratedExecutionDefinition->destroy_execution_storage(mGeneratedExecutionStorage);
        ::operator delete(mGeneratedExecutionStorage);
    }
}

bool HTNPlanningUnit::EnsureGeneratedExecutionStorage()
{
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    mGeneratedExecutionStorageCreatedThisExecution = false;
#endif
    const HTNGeneratedPlannerDefinition* Definition = mPlannerHook.GetGeneratedPlannerDefinition();
    if (!Definition)
        return true;

    if (mGeneratedExecutionStorage && mGeneratedExecutionDefinition == Definition)
        return true;

    if (mGeneratedExecutionStorage && mGeneratedExecutionDefinition)
    {
        mGeneratedExecutionDefinition->destroy_execution_storage(mGeneratedExecutionStorage);
        ::operator delete(mGeneratedExecutionStorage);
        mGeneratedExecutionStorage = nullptr;
        mGeneratedExecutionDefinition = nullptr;
    }

    mGeneratedExecutionStorage = ::operator new(Definition->execution_storage_size, std::nothrow);
    if (mGeneratedExecutionStorage && !Definition->initialize_execution_storage(mGeneratedExecutionStorage))
    {
        ::operator delete(mGeneratedExecutionStorage);
        mGeneratedExecutionStorage = nullptr;
    }
    mGeneratedExecutionDefinition = mGeneratedExecutionStorage ? Definition : nullptr;
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    mGeneratedExecutionStorageCreatedThisExecution = mGeneratedExecutionStorage != nullptr;
#endif

    if (!mGeneratedExecutionStorage)
    {
        HTN_LOG_ERROR("Could not create generated execution storage for planning unit");
        return false;
    }

    return true;
}

HTNDecompositionStatus HTNPlanningUnit::DecomposeTopLevelMethod(const HTNAtom& inCall)
{
    const HTNDecompositionStatus Result = ExecuteCall(inCall, true, mLastDecomposition);
    if (Result == HTN_DECOMPOSITION_SUCCEEDED)
        SetCurrentPlanFromLastDecomposition();
    else
        ClearCurrentPlan();
    return Result;
}

HTNDecompositionStatus HTNPlanningUnit::ExecuteCall(const HTNAtom& inCall, const bool inRequireTopLevel, HTNGeneratedPlanResult& outDecomposition)
{
    HTNAllocationTrace::PhaseScope AllocationPhase(HTNAllocationTrace::Phase::ContextAndStorage);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    mLastGeneratedTimingBreakdown = HTNPlanningUnitGeneratedTimingBreakdown{};
    mLastGeneratedStructuralCounters = HTNGeneratedStructuralCounters{};
    const auto GeneratedEnsureStart = std::chrono::steady_clock::now();
#endif
    if (!EnsureGeneratedExecutionStorage())
        return HTN_DECOMPOSITION_OUT_OF_MEMORY;
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    const auto GeneratedEnsureEnd = std::chrono::steady_clock::now();
    mLastGeneratedTimingBreakdown.EnsureExecutionStorageMilliseconds =
        std::chrono::duration<double, std::milli>(GeneratedEnsureEnd - GeneratedEnsureStart).count();
    mLastGeneratedTimingBreakdown.ExecutionStorageCreated = mGeneratedExecutionStorageCreatedThisExecution;
#endif

    HTNPlannerExecutionContext ExecutionContext{
        &mPlannerHook.GetWorldState(),
        &mPlannerHook.GetCallTermBindingContext(),
        &inCall,
        nullptr,
        mBacktrackingMode,
        mGeneratedExecutionStorage
#ifdef HTN_DEBUG_DECOMPOSITION
        , mGeneratedDebugger
#endif
    };

#ifdef HTN_GENERATED_EXECUTION_PROFILING
    const auto GeneratedContextStart = std::chrono::steady_clock::now();
#endif
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    const auto GeneratedContextEnd = std::chrono::steady_clock::now();
    mLastGeneratedTimingBreakdown.ContextConstructionMilliseconds =
        std::chrono::duration<double, std::milli>(GeneratedContextEnd - GeneratedContextStart).count();
#endif
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    const auto GeneratedDecompositionStart = std::chrono::steady_clock::now();
#endif
    HTNAtomOwner GeneratedPlan(HTNAtomListOwner{});
    const HTNDecompositionStatus Result = mPlannerHook.Decompose(ExecutionContext, GeneratedPlan, inRequireTopLevel);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    if (mGeneratedExecutionStorage)
        (void)HTNGeneratedProfiling_GetStructuralCounters(mGeneratedExecutionDefinition->get_execution_profiling(mGeneratedExecutionStorage), &mLastGeneratedStructuralCounters);
    const auto GeneratedDecompositionEnd = std::chrono::steady_clock::now();
    mLastGeneratedTimingBreakdown.DecompositionMilliseconds =
        std::chrono::duration<double, std::milli>(GeneratedDecompositionEnd - GeneratedDecompositionStart).count();
    if (mGeneratedExecutionStorage)
        HTNGeneratedProfiling_GetLastPreparationTiming(mGeneratedExecutionDefinition->get_execution_profiling(mGeneratedExecutionStorage), &mLastGeneratedTimingBreakdown.Preparation);
#endif
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    const auto GeneratedDecompositionCopyStart = std::chrono::steady_clock::now();
#endif
    // The completed context is local to this call. Transfer its owned result instead
    // of deep-copying it immediately before the context is destroyed.
    outDecomposition.SetResult(std::move(GeneratedPlan));
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    const auto GeneratedDecompositionCopyEnd = std::chrono::steady_clock::now();
    mLastGeneratedTimingBreakdown.DecompositionCopyMilliseconds =
        std::chrono::duration<double, std::milli>(GeneratedDecompositionCopyEnd - GeneratedDecompositionCopyStart).count();
#endif

    return Result;
}

void HTNPlanningUnit::SetCurrentPlanFromLastDecomposition()
{
    HTNAllocationTrace::PhaseScope AllocationPhase(HTNAllocationTrace::Phase::ActivePlanCopy);
    mCurrentPlan.clear();
    mCurrentPrimitiveTaskIndex = 0u;

    const HTNAtomOwner& Plan = mLastDecomposition.GetResult();
    const int32 PlanStepCount = Plan.GetListSize();
    if (PlanStepCount <= 0)
        return;

    mCurrentPlan.reserve(static_cast<std::size_t>(PlanStepCount));
    for (int32 PlanStepIndex = 0; PlanStepIndex < PlanStepCount; ++PlanStepIndex)
        mCurrentPlan.emplace_back(Plan.GetListElement(static_cast<uint32>(PlanStepIndex)));
}

HTNPrimitiveTaskResolution HTNPlanningUnit::ResolveCurrentPrimitiveTask()
{
    while (mCurrentPrimitiveTaskIndex < mCurrentPlan.size())
    {
        const HTNPlanStepKind StepKind = HTNGetPlanStepKind(mCurrentPlan[mCurrentPrimitiveTaskIndex]);
        if (StepKind == HTNPlanStepKind::PrimitiveTask)
            return HTNPrimitiveTaskResolution::TaskReady;

        if (StepKind != HTNPlanStepKind::DeferredCall)
        {
            ClearCurrentPlan();
            return HTNPrimitiveTaskResolution::Failed;
        }

        const HTNAtomOwner Call = HTNMakeCallFromDeferredPlanStep(mCurrentPlan[mCurrentPrimitiveTaskIndex]);
        if (!HTNIsValidCall(Call))
        {
            ClearCurrentPlan();
            return HTNPrimitiveTaskResolution::Failed;
        }

        HTNGeneratedPlanResult DeferredDecomposition;
        const HTNDecompositionStatus Result = ExecuteCall(*Call.Get(), false, DeferredDecomposition);
        if (Result != HTN_DECOMPOSITION_SUCCEEDED)
        {
            ClearCurrentPlan();
            return HTNPrimitiveTaskResolution::Failed;
        }

        const HTNAtomOwner& DeferredPlan = DeferredDecomposition.GetResult();
        const int32 DeferredStepCount = DeferredPlan.GetListSize();
        std::vector<HTNAtomOwner> Replacement;
        if (DeferredStepCount > 0)
        {
            Replacement.reserve(static_cast<std::size_t>(DeferredStepCount));
            for (int32 StepIndex = 0; StepIndex < DeferredStepCount; ++StepIndex)
                Replacement.emplace_back(DeferredPlan.GetListElement(static_cast<uint32>(StepIndex)));
        }

        const auto Current = mCurrentPlan.begin() + static_cast<std::ptrdiff_t>(mCurrentPrimitiveTaskIndex);
        mCurrentPlan.erase(Current);
        mCurrentPlan.insert(
            mCurrentPlan.begin() + static_cast<std::ptrdiff_t>(mCurrentPrimitiveTaskIndex),
            std::make_move_iterator(Replacement.begin()),
            std::make_move_iterator(Replacement.end()));
    }

    return HTNPrimitiveTaskResolution::PlanCompleted;
}

void HTNPlanningUnit::CompleteCurrentPrimitiveTask()
{
    if (GetCurrentPrimitiveTask())
        ++mCurrentPrimitiveTaskIndex;
}

void HTNPlanningUnit::ClearCurrentPlan()
{
    mCurrentPlan.clear();
    mCurrentPrimitiveTaskIndex = 0u;
}


HTNDecompositionStatus HTNPlanningUnit::DecomposeTopLevelMethod(const HtnSymbol* inTopLevelMethod)
{
    return DecomposeTopLevelMethod<>(inTopLevelMethod);
}

HTNDecompositionStatus HTNPlanningUnit::DecomposeTopLevelMethod()
{
    return DecomposeTopLevelMethod<>(mDefaultTopLevelMethod);
}
