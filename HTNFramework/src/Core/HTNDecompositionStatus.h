// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Describes why a planning request finished. A successful decomposition may still
// contain zero primitive tasks; plan emptiness is independent from decomposition status.
typedef enum HTNDecompositionStatus
{
    // Planning completed normally and produced a valid decomposition.
    HTN_DECOMPOSITION_SUCCEEDED = 0,

    // Planning completed normally, but no valid decomposition exists for the current
    // world state and requested top-level method.
    HTN_DECOMPOSITION_NO_PLAN,

    // A planner generated with FixedCapacity needed more pending backtracking
    // continuations than the configured generated capacity can store.
    HTN_DECOMPOSITION_BACKTRACKING_CAPACITY_EXCEEDED,

    // Generated planning required dynamic backtracking storage, but allocation failed.
    HTN_DECOMPOSITION_OUT_OF_MEMORY,

    // Required execution inputs such as world state, callterm registry, execution
    // storage or prepared storage were missing.
    HTN_DECOMPOSITION_INVALID_CONTEXT,

    // The supplied top-level call is malformed, names no generated top-level method,
    // or has an argument count incompatible with that method.
    HTN_DECOMPOSITION_INVALID_CALL,

    // Generated fact/callterm preparation failed before decomposition could execute.
    HTN_DECOMPOSITION_PREPARATION_FAILED,

    // No decomposition has been requested yet. This state is used by host-side
    // tooling before the first planning request and is never returned by generated code.
    HTN_DECOMPOSITION_NOT_RUN
} HTNDecompositionStatus;

#ifdef __cplusplus
}
#endif
