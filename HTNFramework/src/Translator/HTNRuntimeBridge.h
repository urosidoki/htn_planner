// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtomC.h"
#include "Core/HtnSymbolGenerated.h"
#include "Translator/HTNCallTermBridge.h"
#include "Translator/HTNGeneratedBacktracking.h"
#include "Translator/HTNGeneratedDebug.h"
#include "Translator/HTNGeneratedProfiling.h"
#include "WorldState/HTNGeneratedWorldState.h"

#include <stdint.h>

// Revision 3 exposes the execution reset used by ProfileDetailed generated domains.
// Rebuild hosts and
// domain modules together; the planner descriptor ABI and service exports are unchanged.
#if defined(HTN_DEBUG_DECOMPOSITION) && defined(HTN_GENERATED_EXECUTION_PROFILING)
#define HTN_RUNTIME_BRIDGE_ABI_VERSION UINT32_C(0x485B0003)
#elif defined(HTN_DEBUG_DECOMPOSITION)
#define HTN_RUNTIME_BRIDGE_ABI_VERSION UINT32_C(0x48590003)
#elif defined(HTN_GENERATED_EXECUTION_PROFILING)
#define HTN_RUNTIME_BRIDGE_ABI_VERSION UINT32_C(0x485A0003)
#else
#define HTN_RUNTIME_BRIDGE_ABI_VERSION UINT32_C(0x48580003)
#endif

#ifdef _WIN32
#  ifdef HTN_RUNTIME_BRIDGE_EXPORTS
#    define HTN_RUNTIME_BRIDGE_EXPORT __declspec(dllexport)
#  else
#    define HTN_RUNTIME_BRIDGE_EXPORT
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define HTN_RUNTIME_BRIDGE_EXPORT __attribute__((visibility("default")))
#else
#  define HTN_RUNTIME_BRIDGE_EXPORT
#endif

#define HTN_RUNTIME_BRIDGE_BASE_FUNCTIONS(X) \
    X(void, HTNAtom_Init, (HTNAtom* atom), (atom)) \
    X(void, HTNAtom_InitRange, (HTNAtom* atoms, uint32_t count), (atoms, count)) \
    X(void, HTNAtom_Destroy, (HTNAtom* atom), (atom)) \
    X(void, HTNAtom_DestroyRange, (HTNAtom* atoms, uint32_t count), (atoms, count)) \
    X(int, HTNAtom_Copy, (HTNAtom* out_atom, const HTNAtom* atom), (out_atom, atom)) \
    X(int, HTNAtom_AssignCopy, (HTNAtom* out_atom, const HTNAtom* atom), (out_atom, atom)) \
    X(void, HTNAtom_AssignMove, (HTNAtom* out_atom, HTNAtom* atom), (out_atom, atom)) \
    X(int, HTNAtom_CreateCallFromPointers, (HTNAtom* out_atom, const void* head, const HTNAtom* const* arguments, uint32_t count), (out_atom, head, arguments, count)) \
    X(void, HTNAtom_SetEmptyList, (HTNAtom* atom), (atom)) \
    X(void, HTNAtom_Unbind, (HTNAtom* atom), (atom)) \
    X(int, HTNAtom_Equals, (const HTNAtom* left, const HTNAtom* right), (left, right)) \
    X(HTNAtomType, HTNAtom_GetType, (const HTNAtom* atom), (atom)) \
    X(int, HTNAtom_IsBound, (const HTNAtom* atom), (atom)) \
    X(int, HTNAtom_PushBackListElementMove, (HTNAtom* atom, HTNAtom* value), (atom, value)) \
    X(const HTNAtom*, HTNAtom_GetListElement, (const HTNAtom* atom, uint32_t index), (atom, index)) \
    X(int32_t, HTNAtom_GetListSize, (const HTNAtom* atom), (atom)) \
    X(int, HTNAtomList_RemoveAt, (HTNAtomList* list, uint32_t index), (list, index)) \
    X(int, HTNAtomList_Split, (const HTNAtomList* list, HTNAtomListSplitDirection direction, HTNAtom* out_element, HTNAtom* out_remainder), (list, direction, out_element, out_remainder)) \
    X(const HtnSymbol*, HtnSymbol_InternGenerated, (const char* text), (text)) \
    X(uint64_t, HTNWorldState_GetFactStorageGeneration, (const HTNWorldState* world_state), (world_state)) \
    X(const void*, HTNWorldState_ResolveGeneratedFactTables, (HTNWorldState* world_state, const HtnSymbol* fact), (world_state, fact)) \
    X(void, HTNWorldState_BeginGeneratedFactRowCursor, (const void* tables, uint32_t count, HTNGeneratedFactRowCursor* out_cursor), (tables, count, out_cursor)) \
    X(int, HTNWorldState_NextGeneratedFactRow, (HTNGeneratedFactRowCursor* cursor, const HTNAtom** out_arguments), (cursor, out_arguments)) \
    X(HTNGeneratedCallTerm, HTNCallTermRegistry_ResolveGeneratedCallTerm, (const HTNCallTermBindingContext* context, const char* name), (context, name)) \
    X(int, HTNCallTermRegistry_InvokeGeneratedCallTerm, (const HTNCallTermBindingContext* context, const HTNGeneratedCallTerm* callterm, const HTNAtom* const* arguments, uint32_t count, HTNAtom* out_result), (context, callterm, arguments, count, out_result)) \
    X(HTNGeneratedBacktrackingOverflow*, HTNGeneratedBacktracking_CreateOverflow, (void), ()) \
    X(void, HTNGeneratedBacktracking_ResetOverflow, (HTNGeneratedBacktrackingOverflow* overflow), (overflow)) \
    X(void, HTNGeneratedBacktracking_DestroyOverflow, (HTNGeneratedBacktrackingOverflow* overflow), (overflow)) \
    X(int, HTNGeneratedBacktracking_PushContinuationSnapshotOverflow, (HTNGeneratedBacktrackingOverflow* overflow, uint32_t slot, const HTNAtom* value), (overflow, slot, value)) \
    X(int, HTNGeneratedBacktracking_PushPendingContinuationOverflow, (HTNGeneratedBacktrackingOverflow* overflow, uint64_t frame, HTNGeneratedTaskContinuationFn continuation, uint32_t count), (overflow, frame, continuation, count)) \
    X(HTNGeneratedTaskContinuationFn, HTNGeneratedBacktracking_PopPendingContinuationOverflow, (HTNGeneratedBacktrackingOverflow* overflow, uint64_t* out_frame, uint32_t* out_count), (overflow, out_frame, out_count)) \
    X(void, HTNGeneratedBacktracking_PopContinuationSnapshotOverflow, (HTNGeneratedBacktrackingOverflow* overflow, uint32_t* out_slot, HTNAtom* out_value), (overflow, out_slot, out_value)) \
    X(HTNGeneratedProfilingState*, HTNGeneratedProfiling_Create, (void), ()) \
    X(void, HTNGeneratedProfiling_Destroy, (HTNGeneratedProfilingState* state), (state)) \
    X(void, HTNGeneratedProfiling_ProfileBegin, (HTNGeneratedProfilingState* state, uint32_t category), (state, category)) \
    X(void, HTNGeneratedProfiling_ProfileEnd, (HTNGeneratedProfilingState* state, uint32_t category), (state, category)) \
    X(void, HTNGeneratedProfiling_ResetExecution, (HTNGeneratedProfilingState* state), (state))

#ifdef HTN_GENERATED_EXECUTION_PROFILING
#  define HTN_RUNTIME_BRIDGE_PROFILING_FUNCTIONS(X) \
    X(void, HTNGeneratedProfiling_BeginPreparationTiming, (HTNGeneratedProfilingState* state, int fact_hit, int callterm_hit), (state, fact_hit, callterm_hit)) \
    X(void, HTNGeneratedProfiling_MarkPreparationTiming, (HTNGeneratedProfilingState* state, HTNGeneratedPreparationStage stage), (state, stage)) \
    X(HTNGeneratedStructuralCounters*, HTNGeneratedProfiling_GetStructuralCountersMutable, (HTNGeneratedProfilingState* state), (state))
#else
#  define HTN_RUNTIME_BRIDGE_PROFILING_FUNCTIONS(X)
#endif

#ifdef HTN_DEBUG_DECOMPOSITION
#  define HTN_RUNTIME_BRIDGE_DEBUG_FUNCTIONS(X) \
    X(void, HTNGeneratedEventDebug_BeginPlan, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index), (debugger, variables, domain, index)) \
    X(void, HTNGeneratedEventDebug_EndPlan, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result), (debugger, variables, domain, result)) \
    X(void, HTNGeneratedEventDebug_BeginMethod, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index), (debugger, variables, domain, index)) \
    X(void, HTNGeneratedEventDebug_EndMethod, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result), (debugger, variables, domain, result)) \
    X(void, HTNGeneratedEventDebug_BeginBranch, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index), (debugger, variables, domain, index)) \
    X(void, HTNGeneratedEventDebug_EndBranch, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result), (debugger, variables, domain, result)) \
    X(void, HTNGeneratedEventDebug_CapturePendingTask, (HTNGeneratedDebugger* debugger, uint32_t index), (debugger, index)) \
    X(void, HTNGeneratedEventDebug_BeginTask, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index), (debugger, variables, domain, index)) \
    X(void, HTNGeneratedEventDebug_EndTask, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result), (debugger, variables, domain, result)) \
    X(void, HTNGeneratedEventDebug_BeginCondition, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index), (debugger, variables, domain, index)) \
    X(void, HTNGeneratedEventDebug_EndCondition, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result), (debugger, variables, domain, result)) \
    X(void, HTNGeneratedEventDebug_BeginAxiom, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index), (debugger, variables, domain, index)) \
    X(void, HTNGeneratedEventDebug_EndAxiom, (HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result), (debugger, variables, domain, result))
#else
#  define HTN_RUNTIME_BRIDGE_DEBUG_FUNCTIONS(X)
#endif

#define HTN_RUNTIME_BRIDGE_FUNCTIONS(X) \
    HTN_RUNTIME_BRIDGE_BASE_FUNCTIONS(X) \
    HTN_RUNTIME_BRIDGE_PROFILING_FUNCTIONS(X) \
    HTN_RUNTIME_BRIDGE_DEBUG_FUNCTIONS(X)

typedef struct HTNHostRuntimeAPI
{
    uint32_t abi_version;
    uint32_t size;
#define HTN_RUNTIME_BRIDGE_FIELD(return_type, name, parameters, arguments) return_type (*name) parameters;
    HTN_RUNTIME_BRIDGE_FUNCTIONS(HTN_RUNTIME_BRIDGE_FIELD)
#undef HTN_RUNTIME_BRIDGE_FIELD
} HTNHostRuntimeAPI;

typedef int (*HTNRuntimeBridgeBindFn)(const HTNHostRuntimeAPI* api);

#ifdef __cplusplus
extern "C" {
#endif
HTN_RUNTIME_BRIDGE_EXPORT int HTNRuntimeBridge_Bind(const HTNHostRuntimeAPI* api);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
inline HTNHostRuntimeAPI HTNCreateHostRuntimeAPI()
{
    HTNHostRuntimeAPI API{};
    API.abi_version = HTN_RUNTIME_BRIDGE_ABI_VERSION;
    API.size = sizeof(API);
#define HTN_RUNTIME_BRIDGE_ASSIGN(return_type, name, parameters, arguments) API.name = &name;
    HTN_RUNTIME_BRIDGE_FUNCTIONS(HTN_RUNTIME_BRIDGE_ASSIGN)
#undef HTN_RUNTIME_BRIDGE_ASSIGN
    return API;
}
#endif
