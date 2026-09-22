// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include "Core/HTNBacktrackingMode.h"
#include "Core/HTNMissingCallTerm.h"

/*
 * Input/output descriptor for one generated HTN call decomposition.
 *
 * Keep this structure POD/C-ABI-friendly: the integration layer and generated
 * planner context reference the same descriptor.
 *
 * Result is reserved for a future typed call-result API. The decomposition plan itself is
 * returned by the generated output path and stored by the integration layer.
 */

#ifdef __cplusplus
struct HTNAtom;
class HTNCallTermBindingContext;
class HTNWorldState;
class HTNGeneratedDebugger;
#else
typedef struct HTNAtom HTNAtom;
typedef struct HTNCallTermBindingContext HTNCallTermBindingContext;
typedef struct HTNWorldState HTNWorldState;
typedef struct HTNGeneratedDebugger HTNGeneratedDebugger;
#endif

typedef struct HTNPlannerExecutionContext
{
    HTNWorldState* WorldState;
    const HTNCallTermBindingContext* CallTermBindingContext;
    const HTNAtom* Call;
    HTNAtom* Result;
    HTNBacktrackingMode BacktrackingMode;

    // Caller-owned reusable generated execution storage. Ownership remains with
    // the caller so one immutable
    // generated definition can be shared safely by many agents/jobs.
    void* GeneratedExecutionStorage;

#ifdef HTN_DEBUG_DECOMPOSITION
    // Optional caller-owned event debugger for generated execution.
    // This field does not exist in non-debug builds.
    HTNGeneratedDebugger* GeneratedDebugger;
#endif
    // Borrowed client services for this execution and its synchronous callbacks.
    void* ClientContext;
    HTNMissingCallTermPolicy MissingCallTermPolicy;
    HTNMissingCallTermCallback MissingCallTermCallback;
} HTNPlannerExecutionContext;
