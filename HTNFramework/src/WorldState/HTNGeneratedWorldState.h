// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtomC.h"

#include <stdint.h>

#ifdef __cplusplus
class HTNWorldState;
class HtnSymbol;
extern "C" {
#else
typedef struct HTNWorldState HTNWorldState;
typedef struct HtnSymbol HtnSymbol;
#endif

/* C-compatible cursor owned by generated code while HTNWorldState owns the
   pointed-to fact table storage for the duration of the query. */
typedef struct HTNGeneratedFactRowCursor
{
    const void* table;
    uint64_t next;
    uint64_t row_count;
    uint32_t argument_count;
} HTNGeneratedFactRowCursor;

const void* HTNWorldState_ResolveGeneratedFactTables(HTNWorldState* world_state,
                                                     const HtnSymbol* fact_symbol);
uint64_t HTNWorldState_GetFactStorageGeneration(const HTNWorldState* world_state);
void HTNWorldState_BeginGeneratedFactRowCursor(const void* fact_tables,
                                               uint32_t argument_count,
                                               HTNGeneratedFactRowCursor* out_cursor);
int HTNWorldState_NextGeneratedFactRow(HTNGeneratedFactRowCursor* cursor,
                                       const HTNAtom** out_arguments);

#ifdef __cplusplus
}
#endif
