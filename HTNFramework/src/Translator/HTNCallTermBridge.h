// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtomC.h"

#include <stdint.h>

#ifdef __cplusplus
class HTNCallTermRegistry;
class HTNCallTermBindingContext;
extern "C" {
#else
typedef struct HTNCallTermRegistry HTNCallTermRegistry;
typedef struct HTNCallTermBindingContext HTNCallTermBindingContext;
#endif

typedef struct HTNGeneratedCallTerm
{
    const void* registry_entry;
    const char* name;
} HTNGeneratedCallTerm;

/* Narrow C bridge used by generated planners after callterms have been configured. */
HTNGeneratedCallTerm HTNCallTermRegistry_ResolveGeneratedCallTerm(const HTNCallTermBindingContext* callterm_context,
                                                                  const char* name);
int HTNCallTermRegistry_InvokeGeneratedCallTerm(const HTNCallTermBindingContext* callterm_context,
                                                const HTNGeneratedCallTerm* callterm,
                                                const HTNAtom* const* arguments,
                                                uint32_t argument_count,
                                                HTNAtom* out_result);

#ifdef __cplusplus
}
#endif
