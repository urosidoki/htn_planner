// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNGeneratedPlanner.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HTNGeneratedBacktrackingOverflow HTNGeneratedBacktrackingOverflow;

HTNGeneratedBacktrackingOverflow* HTNGeneratedBacktracking_CreateOverflow(void);
void HTNGeneratedBacktracking_ResetOverflow(HTNGeneratedBacktrackingOverflow* overflow);
void HTNGeneratedBacktracking_DestroyOverflow(HTNGeneratedBacktrackingOverflow* overflow);

/* Generated C owns continuation metadata and the normal inline push/pop path.
   This C++ bridge receives only one continuation's overflow payload at a time. */
int HTNGeneratedBacktracking_PushContinuationSnapshotOverflow(HTNGeneratedBacktrackingOverflow* overflow,
                                                              uint32_t variable_slot,
                                                              const HTNAtom* value);
int HTNGeneratedBacktracking_PushPendingContinuationOverflow(HTNGeneratedBacktrackingOverflow* overflow,
                                                             uint64_t variable_frame_id,
                                                             HTNGeneratedTaskContinuationFn continuation,
                                                             uint32_t restore_snapshot_count);
HTNGeneratedTaskContinuationFn HTNGeneratedBacktracking_PopPendingContinuationOverflow(HTNGeneratedBacktrackingOverflow* overflow,
                                                                                      uint64_t* out_variable_frame_id,
                                                                                      uint32_t* out_restore_snapshot_count);
void HTNGeneratedBacktracking_PopContinuationSnapshotOverflow(HTNGeneratedBacktrackingOverflow* overflow,
                                                              uint32_t* out_variable_slot,
                                                              HTNAtom* out_value);

#ifdef __cplusplus
}
#endif
