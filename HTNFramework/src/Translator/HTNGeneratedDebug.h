// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNGeneratedPlanner.h"

#include <stdint.h>

#ifdef __cplusplus
class HTNGeneratedDebugger;
extern "C" {
#else
typedef struct HTNGeneratedDebugger HTNGeneratedDebugger;
#endif

typedef enum HTNGeneratedConditionKind
{
    HTN_CONDITION_FACT = 0,
    HTN_CONDITION_AXIOM = 1,
    HTN_CONDITION_AND = 2,
    HTN_CONDITION_OR = 3,
    HTN_CONDITION_ALT = 4,
    HTN_CONDITION_NOT = 5,
    HTN_CONDITION_CALL = 6,
    HTN_CONDITION_CALL_BIND = 7,
    HTN_CONDITION_BUILTIN_COMPARISON = 8,
    HTN_CONDITION_BUILTIN_LIST_SPLIT = 9
} HTNGeneratedConditionKind;

typedef enum HTNGeneratedBuiltinComparisonOperator
{
    HTN_BUILTIN_COMPARE_EQUAL = 0,
    HTN_BUILTIN_COMPARE_NOT_EQUAL = 1,
    HTN_BUILTIN_COMPARE_LESS = 2,
    HTN_BUILTIN_COMPARE_LESS_EQUAL = 3,
    HTN_BUILTIN_COMPARE_GREATER = 4,
    HTN_BUILTIN_COMPARE_GREATER_EQUAL = 5
} HTNGeneratedBuiltinComparisonOperator;


typedef enum HTNGeneratedBuiltinListSplitOperation
{
    // Original split_list syntax. Uses front-split semantics.
    HTN_BUILTIN_LIST_SPLIT = 0,

    // Explicitly extracts the first element and returns the remaining suffix.
    HTN_BUILTIN_LIST_SPLIT_FRONT = 1,

    // Extracts the last element and returns the remaining prefix.
    HTN_BUILTIN_LIST_SPLIT_BACK = 2
} HTNGeneratedBuiltinListSplitOperation;

typedef enum HTNGeneratedTaskKind
{
    HTN_TASK_COMPOUND = 0,
    HTN_TASK_PRIMITIVE = 1,
    HTN_TASK_DEFERRED = 2
} HTNGeneratedTaskKind;

typedef enum HTNGeneratedDebugValueFlags
{
    HTN_GENERATED_DEBUG_VALUE_FLAG_NONE = 0u,
    HTN_GENERATED_DEBUG_VALUE_FLAG_VARIABLE = 1u << 0u,
    HTN_GENERATED_DEBUG_VALUE_FLAG_STRING_LITERAL = 1u << 1u,
    HTN_GENERATED_DEBUG_VALUE_FLAG_CALL_EXPRESSION = 1u << 2u
} HTNGeneratedDebugValueFlags;

typedef struct HTNGeneratedDebugValue
{
    uint32_t flags;

    // Original source expression used by the debugger title (for example
    // @split_list_input rather than its resolved compile-time value).
    uint32_t text;

    // Compile-time resolved value text. This is primarily used by debugger
    // views to inspect constants without requiring generated runtime storage.
    uint32_t resolved_text;

    uint32_t source_line;
    uint32_t variable_slot;
} HTNGeneratedDebugValue;

typedef struct HTNGeneratedDebugCondition
{
    HTNGeneratedConditionKind kind;
    uint32_t id;
    uint32_t first_argument;
    uint32_t argument_count;
    uint32_t first_child_ref;
    uint32_t child_count;
    uint32_t output_value;
    uint32_t resolved_index;
    uint32_t source_line;
} HTNGeneratedDebugCondition;

typedef struct HTNGeneratedDebugTask
{
    HTNGeneratedTaskKind kind;
    uint32_t id;
    uint32_t first_argument;
    uint32_t argument_count;
    uint32_t source_line;
    uint32_t plan_step_head_string_id;
} HTNGeneratedDebugTask;

typedef struct HTNGeneratedDebugBranch
{
    uint32_t id;
    uint32_t condition;
    uint32_t first_task;
    uint32_t task_count;
    uint32_t source_line;
} HTNGeneratedDebugBranch;

typedef struct HTNGeneratedDebugMethod
{
    uint32_t id;
    uint32_t first_parameter;
    uint32_t parameter_count;
    uint32_t first_branch;
    uint32_t branch_count;
    uint32_t source_line;
    uint64_t variable_slot_mask[HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS];
} HTNGeneratedDebugMethod;

typedef struct HTNGeneratedDebugAxiom
{
    uint32_t id;
    uint32_t first_parameter;
    uint32_t parameter_count;
    uint32_t condition;
    uint32_t source_line;
    uint64_t variable_slot_mask[HTN_GENERATED_VARIABLE_SLOT_MASK_WORDS];
} HTNGeneratedDebugAxiom;

typedef struct HTNGeneratedDebugConstant
{
    uint32_t group_id;
    uint32_t id;
    uint32_t value;
    uint32_t source_line;
} HTNGeneratedDebugConstant;

typedef struct HTNGeneratedDebugSourceRange
{
    uint32_t source_file_index;
    uint32_t begin_line;
    uint32_t begin_column;
    uint32_t end_line;
    uint32_t end_column;
} HTNGeneratedDebugSourceRange;

struct HTNGeneratedDebugMetadata
{
    /* Source identity belongs to debugger metadata, not the generated execution ABI. */
    const char* source_file;
    /* Debug/source text is optional and emitted only with HTN_DEBUG_DECOMPOSITION. */
    const char* const* strings;
    uint32_t string_count;
    const HTNGeneratedDebugValue* values;
    uint32_t value_count;
    const uint32_t* variable_string_ids;
    uint32_t variable_slot_count;
    const HTNGeneratedDebugCondition* conditions;
    uint32_t condition_count;
    const uint32_t* condition_child_refs;
    uint32_t condition_child_ref_count;
    const HTNGeneratedDebugTask* tasks;
    uint32_t task_count;
    const HTNGeneratedDebugBranch* branches;
    uint32_t branch_count;
    const HTNGeneratedDebugMethod* methods;
    uint32_t method_count;
    const HTNGeneratedDebugAxiom* axioms;
    uint32_t axiom_count;
    const HTNGeneratedDebugConstant* constants;
    uint32_t constant_count;
    /* Execution-layout counts are debugger/test metadata only. Generated execution
       storage is sized directly by generated initialization callbacks. */
    uint32_t callterm_slot_count;
    uint32_t fact_slot_count;
    const char* const* source_files;
    uint32_t source_file_count;
    const HTNGeneratedDebugSourceRange* value_sources;
    const HTNGeneratedDebugSourceRange* condition_sources;
    const HTNGeneratedDebugSourceRange* task_sources;
    const HTNGeneratedDebugSourceRange* branch_sources;
    const HTNGeneratedDebugSourceRange* method_sources;
    const HTNGeneratedDebugSourceRange* axiom_sources;
    const HTNGeneratedDebugSourceRange* constant_sources;
};


#ifdef HTN_DEBUG_DECOMPOSITION
/* Standalone generated event debugger. It consumes generated metadata only. */
void HTNGeneratedEventDebug_BeginPlan(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t entry_method);
void HTNGeneratedEventDebug_EndPlan(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result);
void HTNGeneratedEventDebug_BeginMethod(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t method_index);
void HTNGeneratedEventDebug_EndMethod(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result);
void HTNGeneratedEventDebug_BeginBranch(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t branch_index);
void HTNGeneratedEventDebug_EndBranch(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result);
void HTNGeneratedEventDebug_CapturePendingTask(HTNGeneratedDebugger* debugger, uint32_t task_index);
void HTNGeneratedEventDebug_BeginTask(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t task_index);
void HTNGeneratedEventDebug_EndTask(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result);
void HTNGeneratedEventDebug_BeginCondition(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t condition_index);
void HTNGeneratedEventDebug_EndCondition(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result);
void HTNGeneratedEventDebug_BeginAxiom(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t axiom_index);
void HTNGeneratedEventDebug_EndAxiom(HTNGeneratedDebugger* debugger, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result);

#endif

#ifdef HTN_DEBUG_DECOMPOSITION
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_PLAN(context, domain, entry_method) HTNGeneratedEventDebug_BeginPlan((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (entry_method))
#  define HTN_GENERATED_EVENT_DEBUG_END_PLAN(context, domain, result) HTNGeneratedEventDebug_EndPlan((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (result))
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_METHOD(context, domain, method_index) HTNGeneratedEventDebug_BeginMethod((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (method_index))
#  define HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, domain, result) HTNGeneratedEventDebug_EndMethod((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (result))
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_BRANCH(context, domain, branch_index) HTNGeneratedEventDebug_BeginBranch((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (branch_index))
#  define HTN_GENERATED_EVENT_DEBUG_END_BRANCH(context, domain, result) HTNGeneratedEventDebug_EndBranch((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (result))
#  define HTN_GENERATED_EVENT_DEBUG_CAPTURE_PENDING_TASK(context, task_index) HTNGeneratedEventDebug_CapturePendingTask((context)->debugger, (task_index))
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_TASK(context, domain, task_index) HTNGeneratedEventDebug_BeginTask((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (task_index))
#  define HTN_GENERATED_EVENT_DEBUG_END_TASK(context, domain, result) HTNGeneratedEventDebug_EndTask((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (result))
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, domain, condition_index, is_choice_point) HTNGeneratedEventDebug_BeginCondition((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (condition_index))
#  define HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, domain, condition_index, result, is_choice_point) HTNGeneratedEventDebug_EndCondition((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (result))
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_AXIOM(context, domain, axiom_index) HTNGeneratedEventDebug_BeginAxiom((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (axiom_index))
#  define HTN_GENERATED_EVENT_DEBUG_END_AXIOM(context, domain, result) HTNGeneratedEventDebug_EndAxiom((context)->debugger, HTN_GENERATED_VARIABLES(context), (domain), (result))
#else
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_PLAN(context, domain, entry_method) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_END_PLAN(context, domain, result) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_METHOD(context, domain, method_index) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_END_METHOD(context, domain, result) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_BRANCH(context, domain, branch_index) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_END_BRANCH(context, domain, result) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_CAPTURE_PENDING_TASK(context, task_index) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_TASK(context, domain, task_index) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_END_TASK(context, domain, result) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_CONDITION(context, domain, condition_index, is_choice_point) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_END_CONDITION(context, domain, condition_index, result, is_choice_point) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_BEGIN_AXIOM(context, domain, axiom_index) ((void)0)
#  define HTN_GENERATED_EVENT_DEBUG_END_AXIOM(context, domain, result) ((void)0)
#endif


#ifdef __cplusplus
}
#endif
