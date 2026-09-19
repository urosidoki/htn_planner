// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtomC.h"
#include "Core/HTNBacktrackingMode.h"
#include "Core/HTNDecompositionStatus.h"

#include <stddef.h>
#include <stdint.h>

#ifndef HTN_GENERATED_PLANNER_ABI_VERSION
#if defined(HTN_DEBUG_DECOMPOSITION) && defined(HTN_GENERATED_EXECUTION_PROFILING)
#define HTN_GENERATED_PLANNER_ABI_VERSION UINT32_C(0x48570003)
#elif defined(HTN_DEBUG_DECOMPOSITION)
#define HTN_GENERATED_PLANNER_ABI_VERSION UINT32_C(0x48550003)
#elif defined(HTN_GENERATED_EXECUTION_PROFILING)
#define HTN_GENERATED_PLANNER_ABI_VERSION UINT32_C(0x48560002)
#else
#define HTN_GENERATED_PLANNER_ABI_VERSION UINT32_C(0x48540002)
#endif
#endif

#if defined(_WIN32) && defined(HTN_GENERATED_MODULE_EXPORTS)
#define HTN_GENERATED_MODULE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define HTN_GENERATED_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define HTN_GENERATED_MODULE_EXPORT
#endif

#define HTN_GENERATED_NO_INDEX UINT32_MAX
#define HTN_GENERATED_MAX_VARIABLE_SLOTS 256u
#define HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS ((HTN_GENERATED_MAX_VARIABLE_SLOTS + 63u) / 64u)

#ifdef __cplusplus
class HTNCallTermBindingContext;
class HTNGeneratedDebugger;
class HTNWorldState;
class HtnSymbol;
extern "C" {
#else
typedef struct HTNCallTermBindingContext HTNCallTermBindingContext;
typedef struct HTNGeneratedDebugger HTNGeneratedDebugger;
typedef struct HTNWorldState HTNWorldState;
typedef struct HtnSymbol HtnSymbol;
#endif

#ifdef HTN_DEBUG_DECOMPOSITION
typedef struct HTNGeneratedDebugMetadata HTNGeneratedDebugMetadata;
#endif
#ifdef HTN_GENERATED_EXECUTION_PROFILING
typedef struct HTNGeneratedProfilingState HTNGeneratedProfilingState;
#endif
typedef struct HTNGeneratedPlannerContext HTNGeneratedPlannerContext;

typedef enum HTNGeneratedPlannerFeatures
{
    HTN_GENERATED_FEATURE_NONE = 0u,
    HTN_GENERATED_FEATURE_RUNTIME_BACKTRACKING = 1u << 0u
} HTNGeneratedPlannerFeatures;


/* C-compatible variable view shared by generated execution and the event debugger.
   Generated C owns slot mutation and checkpoint bookkeeping directly. */
typedef struct HTNGeneratedVariableStorage
{
    HTNAtom* values;
    uint64_t* bound_mask;
    uint32_t value_count;
} HTNGeneratedVariableStorage;

static inline int HTNGeneratedVariables_IsBound(const HTNGeneratedVariableStorage* storage, uint32_t slot)
{
    return (storage->bound_mask[slot >> 6u] & (UINT64_C(1) << (slot & 63u))) != 0u;
}

static inline const HTNAtom* HTNGeneratedVariables_Get(const HTNGeneratedVariableStorage* storage, uint32_t slot)
{
    return HTNGeneratedVariables_IsBound(storage, slot) ? &storage->values[slot] : NULL;
}

/* Generated planners own both opaque storage layouts and their lifecycle. */
typedef int (*HTNGeneratedInitializeStorageFn)(void* storage);
typedef void (*HTNGeneratedDestroyStorageFn)(void* storage);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
typedef HTNGeneratedProfilingState* (*HTNGeneratedGetExecutionProfilingFn)(void* storage);
#endif

struct HTNGeneratedPlannerContext
{
    /* Minimal generated-execution ABI. Generated code treats this descriptor as
       immutable; all pointers are borrowed and caller-owned. */
    HTNWorldState* world_state;
    const HTNCallTermBindingContext* callterm_binding_context;
    HTNBacktrackingMode backtracking_mode;
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNGeneratedDebugger* debugger;
#endif
    void* execution_storage;
    const void* prepared_storage;
};

typedef int (*HTNGeneratedTaskContinuationFn)(const HTNGeneratedPlannerContext* context, HTNAtom* out_result);

/* Input/output contract:
   - call is borrowed and uses the common HTN call representation: (head arg0 ... argN).
   - require_top_level != 0 restricts dispatch to explicit top_level_method entries;
     zero additionally permits generated targets referenced by #deferred calls.
   - the generated dispatch accepts methods marked externally decomposable. Public planning
     currently exposes only explicit top-level methods; deferred-call targets will reuse the
     same dispatch without becoming public top-level methods.
   - out_result must not contain a live atom on entry. The entry point initializes it.
   - on success out_result owns the complete plan (a list of call-shaped plan steps).
   - on failure out_result is left initialized, unbound and safe to destroy. */
typedef HTNDecompositionStatus (*HTNGeneratedDecomposeCallFn)(const HTNGeneratedPlannerContext* context,
                                                                 const HTNAtom* call,
                                                                 int require_top_level,
                                                                 HTNAtom* out_result);

/* Immutable descriptor exported by each generated domain. The host explicitly chooses
   which generated planner definition it owns/uses; there is no global registry. */
typedef struct HTNGeneratedPlannerDefinition
{
    /* Must be the first field so a host can reject an incompatible descriptor
       before reading any configuration-dependent fields. */
    uint32_t abi_version;
    uint32_t features;
#ifdef HTN_DEBUG_DECOMPOSITION
    /* Debugger-only metadata. It is absent from the ABI when decomposition debugging is disabled. */
    const HTNGeneratedDebugMetadata* debug_metadata;
#endif
    size_t prepared_storage_size;
    HTNGeneratedInitializeStorageFn initialize_prepared_storage;
    HTNGeneratedDestroyStorageFn destroy_prepared_storage;
    size_t execution_storage_size;
    HTNGeneratedInitializeStorageFn initialize_execution_storage;
    HTNGeneratedDestroyStorageFn destroy_execution_storage;
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    HTNGeneratedGetExecutionProfilingFn get_execution_profiling;
#endif
    HTNGeneratedDecomposeCallFn decompose_call;
    const char* const* fact_names;
    uint32_t fact_count;
} HTNGeneratedPlannerDefinition;


/* Host-side core validation; no hooks, allocation or module loading required.
   Returns 1 for a matching ABI and valid storage/callback contract, otherwise 0.
   The caller guarantees a readable first ABI word and, for a matching version,
   a complete descriptor. It retains ownership of the definition/module. */
int HTNGeneratedPlanner_ValidateDefinition(const HTNGeneratedPlannerDefinition* definition);

#ifdef __cplusplus
}
#endif
