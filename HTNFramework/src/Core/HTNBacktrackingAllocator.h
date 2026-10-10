// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Optional borrowed allocator for generated backtracking overflow containers.
 * Both callbacks are required. allocate returns aligned storage or NULL; no
 * implicit heap fallback occurs. deallocate receives the original size/alignment.
 * All custom allocations are released before decompose_call returns, including
 * failure. Deallocation order is not necessarily LIFO: a scratch arena may use a
 * no-op deallocate and restore its marker after the call. The planner never resets
 * the client's arena. Owning atom payloads and returned plans use separate storage.
 * Callbacks must not throw. Concurrent calls require separate arenas or client
 * synchronization. The descriptor and user_data must remain valid during the call. */
typedef struct HTNBacktrackingAllocator
{
    void* user_data;
    void* (*allocate)(void* user_data, size_t size, size_t alignment);
    void (*deallocate)(void* user_data, void* memory, size_t size, size_t alignment);
} HTNBacktrackingAllocator;

/* Last decomposition, reset on entry and retained in execution info after return.
 * Bytes describe overflow container requests, excluding inline generated storage,
 * atom payloads, allocator bookkeeping and alignment padding. requested_bytes
 * includes failed requests; allocation_count counts successful requests only.
 * current_bytes is zero after a custom-allocator call. With the default allocator
 * it includes cached blocks, also included in peak_bytes on the next call. */
typedef struct HTNBacktrackingAllocationStats
{
    size_t current_bytes;
    size_t peak_bytes;
    size_t requested_bytes;
    size_t largest_request_bytes;
    size_t max_alignment;
    uint64_t allocation_count;
    uint64_t failed_allocation_count;
} HTNBacktrackingAllocationStats;
