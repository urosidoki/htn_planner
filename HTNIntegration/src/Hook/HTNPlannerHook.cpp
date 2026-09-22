// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Hook/HTNPlannerHook.h"
#include "Translator/HTNAllocationTrace.h"

#include "Core/HtnSymbol.h"
#include "Core/HTNAtomC.h"
#include "Translator/HTNGeneratedPlanner.h"
#ifdef HTN_DEBUG_DECOMPOSITION
#include "Translator/HTNGeneratedDebugger.h"
#endif
#include <new>
#include <utility>
#include <vector>

namespace
{
const HTNCallTermRegistry& GetEmptyCallTermRegistry()
{
    static const HTNCallTermRegistry Registry;
    return Registry;
}

HTNDecompositionStatus ExecuteGeneratedPlanner(const HTNGeneratedPlannerDefinition& inDefinition,
                             const void* inPreparedStorage,
                             const HTNPlannerExecutionContext& ExecutionContext,
                             HTNAtomOwner& outPlan,
                             const bool inRequireTopLevel)
{
    if (!inDefinition.decompose_call || !ExecutionContext.Call)
        return HTN_DECOMPOSITION_INVALID_CONTEXT;

    // Host-owned execution inputs and generated preparation are validated once by
    // the generated entry point before any trusted hot-path helper runs.
    HTNGeneratedPlannerContext GeneratedContext{};
    GeneratedContext.world_state = ExecutionContext.WorldState;
    GeneratedContext.client_context = ExecutionContext.ClientContext;
    GeneratedContext.missing_callterm_policy = ExecutionContext.MissingCallTermPolicy;
    GeneratedContext.missing_callterm_callback = ExecutionContext.MissingCallTermCallback;
    GeneratedContext.callterm_binding_context = ExecutionContext.CallTermBindingContext;
    GeneratedContext.backtracking_mode = ExecutionContext.BacktrackingMode;
#ifdef HTN_DEBUG_DECOMPOSITION
    GeneratedContext.debugger = ExecutionContext.GeneratedDebugger;
#endif
    GeneratedContext.execution_storage = ExecutionContext.GeneratedExecutionStorage;
    GeneratedContext.prepared_storage = inPreparedStorage;
#ifdef HTN_DEBUG_DECOMPOSITION
    if (ExecutionContext.GeneratedDebugger)
        ExecutionContext.GeneratedDebugger->Reset(
            inDefinition.debug_metadata ? inDefinition.debug_metadata->source_file : nullptr);
#endif

    HTNAtom Output;
    const HTNDecompositionStatus Result = inDefinition.decompose_call(
        &GeneratedContext,
        ExecutionContext.Call,
        inRequireTopLevel ? 1 : 0,
        &Output);
    if (Result == HTN_DECOMPOSITION_SUCCEEDED)
        outPlan = HTNAtomOwner(std::move(Output));

    HTNAtom::sDestroy(Output);
    return Result;
}
}

HTNPlannerHook::HTNPlannerHook(HTNWorldState& inWorldState)
    : mWorldState(&inWorldState)
    , mCallTermBindingContext(GetEmptyCallTermRegistry())
{
}

HTNPlannerHook::HTNPlannerHook(
    HTNWorldState& inWorldState,
    const HTNCallTermRegistry& inCallTermRegistry)
    : mWorldState(&inWorldState)
    , mCallTermBindingContext(inCallTermRegistry)
{
}

bool HTNPlannerHook::SetGeneratedPlannerDefinition(
    const HTNGeneratedPlannerDefinition* inGeneratedPlannerDefinition)
{
    if (!inGeneratedPlannerDefinition)
    {
        mGeneratedPreparedStorage.reset();
        mGeneratedPlannerDefinition = nullptr;
        mFactRegistry.Reset();
        return true;
    }

    if (!HTNGeneratedPlanner_ValidateDefinition(inGeneratedPlannerDefinition))
        return false;

    void* PreparedStorage = ::operator new(inGeneratedPlannerDefinition->prepared_storage_size, std::nothrow);
    if (!PreparedStorage || !inGeneratedPlannerDefinition->initialize_prepared_storage(PreparedStorage))
    {
        ::operator delete(PreparedStorage);
        HTN_LOG_ERROR("Could not prepare generated domain metadata");
        return false;
    }

    std::shared_ptr<void> GeneratedPreparedStorage(
        PreparedStorage,
        [inGeneratedPlannerDefinition](void* inPreparedStorage)
        {
            inGeneratedPlannerDefinition->destroy_prepared_storage(inPreparedStorage);
            ::operator delete(inPreparedStorage);
        });

    mGeneratedPreparedStorage = std::move(GeneratedPreparedStorage);
    mGeneratedPlannerDefinition = inGeneratedPlannerDefinition;
    mFactRegistry.Reset();
    for (uint32_t Index = 0u; Index < inGeneratedPlannerDefinition->fact_count; ++Index)
        (void)mFactRegistry.Register(HtnSymbol::sGetSymbol(inGeneratedPlannerDefinition->fact_names[Index]));
    return true;
}

HTNDecompositionStatus HTNPlannerHook::Decompose(const HTNPlannerExecutionContext& inExecutionContext,
                                                   HTNAtomOwner& outPlan,
                                                   const bool inRequireTopLevel) const
{
    HTNAllocationTrace::PhaseScope AllocationPhase(HTNAllocationTrace::Phase::GeneratedExecution);
    if (!mGeneratedPlannerDefinition)
        return HTN_DECOMPOSITION_INVALID_CONTEXT;
    return ExecuteGeneratedPlanner(*mGeneratedPlannerDefinition, mGeneratedPreparedStorage.get(),
                                   inExecutionContext, outPlan, inRequireTopLevel);
}
