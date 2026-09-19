// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNGeneratedPlanner.h"

class HTNWorldState;
class HTNCallTermBindingContext;
class HtnSymbol;

struct HTNGeneratedPlannerBenchmarkSample
{
    bool Succeeded = false;
    double PlannerMilliseconds = 0.0;
};

// Deliberately low-level benchmark helper. Normal gameplay/demo execution must go
// through HTNPlanningUnit -> HTNPlannerHook for generated execution.
HTNGeneratedPlannerBenchmarkSample BenchmarkGeneratedPlannerOnce(
    void* inExecutionStorage,
    HTNGeneratedDecomposeCallFn inDecomposeCall,
    const void* inPreparedStorage,
    HTNWorldState& inWorldState,
    const HTNCallTermBindingContext& inCallTermBindingContext,
    const HtnSymbol* inTopLevelMethod,
    HTNBacktrackingMode inBacktrackingMode);
