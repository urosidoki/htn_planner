// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Generated/HTNGeneratedPlannerDemo.h"
#include "HTNDemoCallTermReporting.h"

#include "WorldState/HTNWorldState.h"

#include <chrono>


HTNGeneratedPlannerBenchmarkSample BenchmarkGeneratedPlannerOnce(
    void* inExecutionStorage,
    HTNGeneratedDecomposeCallFn inDecomposeCall,
    const void* inPreparedStorage,
    HTNWorldState& inWorldState,
    const HTNCallTermBindingContext& inCallTermBindingContext,
    const HtnSymbol* inTopLevelMethod,
    const HTNBacktrackingMode inBacktrackingMode)
{
    HTNGeneratedPlannerBenchmarkSample Sample;
    if (!inExecutionStorage || !inDecomposeCall || !inPreparedStorage || !inTopLevelMethod)
        return Sample;

    HTNGeneratedPlannerContext Context{};
    Context.missing_callterm_policy = HTNMissingCallTermPolicy::Report;
    Context.missing_callterm_callback = ReportGeneratedDemoMissingCallTerm;
    Context.world_state = &inWorldState;
    Context.callterm_binding_context = &inCallTermBindingContext;
    Context.backtracking_mode = inBacktrackingMode;
    Context.execution_storage = inExecutionStorage;
    Context.prepared_storage = inPreparedStorage;

    HTNAtom Call = HTNAtom::sCreateCall(inTopLevelMethod);
    if (!HTNAtom_IsBound(&Call))
    {
        HTNAtom::sDestroy(Call);
        return Sample;
    }
    HTNAtom Plan{};
    const auto StartTime = std::chrono::steady_clock::now();
    const HTNDecompositionStatus DecompositionResult = inDecomposeCall(&Context, &Call, 1, &Plan);
    Sample.Succeeded = DecompositionResult == HTN_DECOMPOSITION_SUCCEEDED;
    const auto EndTime = std::chrono::steady_clock::now();
    Sample.PlannerMilliseconds = std::chrono::duration<double, std::milli>(EndTime - StartTime).count();
    HTNAtom::sDestroy(Plan);
    HTNAtom::sDestroy(Call);

    return Sample;
}
