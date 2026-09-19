// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HTNGeneratedProfilingState HTNGeneratedProfilingState;

/* Optional fine-grained profiler for generated planner execution. Both inclusive
   and exclusive/self time are collected. */
typedef enum HTNGeneratedProfileCategory
{
    HTN_GENERATED_PROFILE_FACT = 0,
    HTN_GENERATED_PROFILE_WORLDSTATE_COUNT,
    HTN_GENERATED_PROFILE_WORLDSTATE_CHECK,
    HTN_GENERATED_PROFILE_AXIOM,
    HTN_GENERATED_PROFILE_CALLTERM,
    HTN_GENERATED_PROFILE_AND,
    HTN_GENERATED_PROFILE_OR,
    HTN_GENERATED_PROFILE_ALT,
    HTN_GENERATED_PROFILE_NOT,
    HTN_GENERATED_PROFILE_RESOLVE_VALUE,
    HTN_GENERATED_PROFILE_METADATA_LOOKUP,
    HTN_GENERATED_PROFILE_ENVIRONMENT_COPY,
    HTN_GENERATED_PROFILE_BINDING_TRAIL,
    HTN_GENERATED_PROFILE_ENTER_COMPOUND,
    HTN_GENERATED_PROFILE_PENDING_PUSH,
    HTN_GENERATED_PROFILE_PENDING_POP,
    HTN_GENERATED_PROFILE_GENERATED_METHOD,
    HTN_GENERATED_PROFILE_GENERATED_BRANCH,
    HTN_GENERATED_PROFILE_GENERATED_TASK,
    HTN_GENERATED_PROFILE_GENERATED_DISPATCH,
    HTN_GENERATED_PROFILE_CONTINUATION_TRAIL,
    /* Detailed-only subdivision of Generated branch/CFG. These categories are
       nested beneath HTN_GENERATED_PROFILE_GENERATED_BRANCH, so their self time
       removes the corresponding work from the parent branch bucket. */
    HTN_GENERATED_PROFILE_BRANCH_SETUP,
    HTN_GENERATED_PROFILE_BRANCH_CONDITION_CFG,
    HTN_GENERATED_PROFILE_BRANCH_TASK_SCHEDULING,
    HTN_GENERATED_PROFILE_BRANCH_COMMIT,
    HTN_GENERATED_PROFILE_BRANCH_RETRY,
    /* Detailed-only subdivision of branch condition CFG. Keep these scopes
       relatively coarse: ProfileDetailed is diagnostic, while Profile compiles
       every profiling macro out completely. */
    HTN_GENERATED_PROFILE_CONDITION_FACT_CURSOR_SETUP,
    HTN_GENERATED_PROFILE_CONDITION_FACT_SCAN_UNIFY,
    HTN_GENERATED_PROFILE_CONDITION_CHOICE_BACKTRACK,
    HTN_GENERATED_PROFILE_CONDITION_AXIOM_CONTROL,
    HTN_GENERATED_PROFILE_CONDITION_CALL_CONTROL,
    HTN_GENERATED_PROFILE_CATEGORY_COUNT
} HTNGeneratedProfileCategory;

typedef struct HTNGeneratedProfileSample
{
    uint64_t nanoseconds;
    uint64_t self_nanoseconds;
    uint64_t calls;
} HTNGeneratedProfileSample;

#ifdef HTN_GENERATED_EXECUTION_PROFILING
typedef enum HTNGeneratedStructuralEvent
{
    HTN_GENERATED_STRUCTURAL_FACT_QUERY = 0,
    HTN_GENERATED_STRUCTURAL_FACT_ROW_TESTED,
    HTN_GENERATED_STRUCTURAL_FACT_ROW_MATCHED,
    HTN_GENERATED_STRUCTURAL_FACT_CHOICE_POINT,
    HTN_GENERATED_STRUCTURAL_FACT_CHOICE_RETRY,
    HTN_GENERATED_STRUCTURAL_CHECKPOINT_PUSH,
    HTN_GENERATED_STRUCTURAL_CHECKPOINT_ROLLBACK,
    HTN_GENERATED_STRUCTURAL_CHECKPOINT_COMMIT,
    HTN_GENERATED_STRUCTURAL_EVENT_COUNT
} HTNGeneratedStructuralEvent;

typedef struct HTNGeneratedStructuralCounters
{
    uint64_t calls[HTN_GENERATED_PROFILE_CATEGORY_COUNT];
    uint64_t events[HTN_GENERATED_STRUCTURAL_EVENT_COUNT];
} HTNGeneratedStructuralCounters;
#endif


#ifdef HTN_GENERATED_EXECUTION_PROFILING
/* Coarse generated-entry diagnostics used to compare cold and warm execution.
   This instrumentation is independent from decomposition debugging and from the
   fine-grained HTN_PROFILE_DETAILED profiler. */
typedef struct HTNGeneratedPreparationTimingBreakdown
{
    double total_milliseconds;
    double reset_milliseconds;
    double fact_slots_milliseconds;
    double callterm_slots_milliseconds;
    double top_level_method_milliseconds;
    int fact_slots_cache_hit;
    int callterm_slots_cache_hit;
} HTNGeneratedPreparationTimingBreakdown;
#endif

#ifdef HTN_GENERATED_EXECUTION_PROFILING
int HTNGeneratedProfiling_GetLastPreparationTiming(
    const HTNGeneratedProfilingState* state,
    HTNGeneratedPreparationTimingBreakdown* out_timing);
typedef enum HTNGeneratedPreparationStage
{
    HTN_GENERATED_PREPARATION_RESET = 0,
    HTN_GENERATED_PREPARATION_FACT_SLOTS,
    HTN_GENERATED_PREPARATION_CALLTERM_SLOTS,
    HTN_GENERATED_PREPARATION_TOP_LEVEL_METHOD,
    HTN_GENERATED_PREPARATION_COMPLETE
} HTNGeneratedPreparationStage;
void HTNGeneratedProfiling_BeginPreparationTiming(HTNGeneratedProfilingState* state,
                                                  int fact_slots_cache_hit,
                                                  int callterm_slots_cache_hit);
void HTNGeneratedProfiling_MarkPreparationTiming(HTNGeneratedProfilingState* state,
                                                 HTNGeneratedPreparationStage stage);
#define HTN_GENERATED_PREPARATION_BEGIN(state, fact_hit, callterm_hit) \
    HTNGeneratedProfiling_BeginPreparationTiming((state), (fact_hit), (callterm_hit))
#define HTN_GENERATED_PREPARATION_MARK(state, stage) \
    HTNGeneratedProfiling_MarkPreparationTiming((state), (stage))
#else
#define HTN_GENERATED_PREPARATION_BEGIN(...) ((void)0)
#define HTN_GENERATED_PREPARATION_MARK(...) ((void)0)
#endif

HTNGeneratedProfilingState* HTNGeneratedProfiling_Create(void);
void HTNGeneratedProfiling_Destroy(HTNGeneratedProfilingState* state);
void HTNGeneratedProfiling_ResetExecution(HTNGeneratedProfilingState* state);
void HTNGeneratedProfiling_SetEnabled(HTNGeneratedProfilingState* state, int enabled);
void HTNGeneratedProfiling_ResetProfile(HTNGeneratedProfilingState* state);
int HTNGeneratedProfiling_GetProfileSample(const HTNGeneratedProfilingState* state,
                                           uint32_t category,
                                           HTNGeneratedProfileSample* out_sample);
void HTNGeneratedProfiling_ProfileBegin(HTNGeneratedProfilingState* state, uint32_t category);
void HTNGeneratedProfiling_ProfileEnd(HTNGeneratedProfilingState* state, uint32_t category);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
HTNGeneratedStructuralCounters* HTNGeneratedProfiling_GetStructuralCountersMutable(HTNGeneratedProfilingState* state);
int HTNGeneratedProfiling_GetStructuralCounters(const HTNGeneratedProfilingState* state, HTNGeneratedStructuralCounters* out_counters);
#endif

/*
 * Fine-grained profiling is a compile-time feature. Profile builds intentionally
 * erase every internal span so their timings represent the planner itself.
 * ProfileDetailed keeps the exact same generated C but expands these wrappers
 * to the hierarchical profiler calls. Arguments are not evaluated in Profile/Release.
 */
#ifdef HTN_PROFILE_DETAILED
#  define HTN_GENERATED_PROFILE_BEGIN(context, category) \
    HTNGeneratedProfiling_ProfileBegin(HTN_GENERATED_PROFILING(context), (category))
#  define HTN_GENERATED_PROFILE_END(context, category) \
    HTNGeneratedProfiling_ProfileEnd(HTN_GENERATED_PROFILING(context), (category))
#elif defined(HTN_GENERATED_EXECUTION_PROFILING)
#  define HTN_GENERATED_PROFILE_BEGIN(context, category) \
    do { HTNGeneratedStructuralCounters* counters = HTN_GENERATED_STRUCTURAL_COUNTERS(context); if (counters) ++counters->calls[(category)]; } while (0)
#  define HTN_GENERATED_PROFILE_END(...) ((void)0)
#else
#  define HTN_GENERATED_PROFILE_BEGIN(...) ((void)0)
#  define HTN_GENERATED_PROFILE_END(...) ((void)0)
#endif

#ifdef HTN_GENERATED_EXECUTION_PROFILING
#  define HTN_GENERATED_STRUCTURAL_EVENT(context, event) \
    do { HTNGeneratedStructuralCounters* counters = HTN_GENERATED_STRUCTURAL_COUNTERS(context); if (counters) ++counters->events[(event)]; } while (0)
#else
#  define HTN_GENERATED_STRUCTURAL_EVENT(...) ((void)0)
#endif


#ifdef __cplusplus
}
#endif
