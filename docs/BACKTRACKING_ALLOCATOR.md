# Backtracking overflow allocation and memory usage

`HTNGeneratedPlannerContext::backtracking_allocator` optionally borrows an
`HTNBacktrackingAllocator`: C-compatible allocation/deallocation callbacks and user
data. With HTNIntegration, set `unit.GetExecutionContext().BacktrackingAllocator`.
Zero-initialize the context. No global or thread-local allocator is configured.

## Allocator lifetimes

| Context member | Allocations controlled | Required lifetime | Can reset at decomposition return? |
| --- | --- | --- | --- |
| `list_allocator` / `ListAllocator` | Owning atom list nodes: results, runtime lists, copies and list payloads | Allocator and backing storage must outlive every referencing value, including retained/active plans, copies and execution storage | Only after all referencing values are destroyed; a returned plan usually still owns nodes |
| `backtracking_allocator` / `BacktrackingAllocator` | Overflow container and its blocks of pending continuations and variable snapshots | Descriptor, callbacks, user data and backing storage must remain valid for the synchronous `decompose_call` | **Yes.** Every custom block is released before return on success, failure and exhaustion |

The second allocator controls container memory, not owning atom payloads inside
snapshots. Payloads keep their list/string allocation policy. Returned plans never
borrow backtracking blocks. A scratch marker can therefore be restored while the
returned plan remains alive. This does not permit using that same scratch memory
for retained atom lists.

The context borrows both services. If a context outlives a call-local backtracking
allocator, clear or replace its pointer before the next call. Deferred decomposition
uses the allocator configured for that later call. Both top-level and deferred
calls release custom overflow before returning. With the reference planning unit,
it is safe to restore a marker after `DecomposeTopLevelMethod` or
`ResolveCurrentPrimitiveTask` returns; the latter may perform multiple decompositions.

## Default behavior and scratch behavior

With a null backtracking allocator, overflow uses new/delete and retains blocks in
execution storage for reuse between decompositions. Switching to a custom allocator
releases that cache before the next decomposition. Custom allocations are never
retained for another call or released by a different allocator.

Both callbacks are required, even for an arena whose `deallocate` does nothing.
Missing callbacks return `HTN_DECOMPOSITION_INVALID_CONTEXT`. Allocation must return
the requested alignment or null, and callbacks must not throw. A null allocation
returns `HTN_DECOMPOSITION_OUT_OF_MEMORY`; there is no implicit heap fallback or
retry through a logical alternative. Partial results and overflow are cleaned up.
`execution_info.last_error` identifies backtracking allocation failure separately
from atom list allocation failure.

Deallocations need not occur in reverse allocation order. Use individual frees or
a scratch marker with no-op deallocation, rather than a stack allocator requiring
each free to pop the latest allocation. The planner calls deallocate after destroying
objects and never resets the client's arena. Independent planners can use separate
arenas concurrently; sharing one requires client coordination, including nested
calls from a callterm. Nested calls also need separate execution storage. Never
reset memory still used by an outer call.

## Statistics after execution

```cpp
const auto* info = definition->get_execution_info(context.execution_storage);
const HTNBacktrackingAllocationStats usage = info->backtracking_allocations;
// HTNIntegration: unit.GetGeneratedExecutionInfo()->backtracking_allocations
```

Statistics are available in plain and instrumented builds without profiling flags.
They reset for each `decompose_call`, including invalid calls, and survive cleanup
until the next call or execution-storage destruction. Copy the struct for history.
The planning-unit accessor reports the latest call, including deferred expansion,
rather than the sum of all decompositions in one resolve operation.

| Field | Meaning |
| --- | --- |
| `current_bytes` | Live requested bytes after return: zero for a custom allocator; retained cache size for the default allocator |
| `peak_bytes` | Maximum simultaneous live requested bytes, including previously cached default blocks |
| `requested_bytes` | Sum of this call's allocation requests, including failed requests |
| `largest_request_bytes` | Largest single request during this call |
| `max_alignment` | Largest requested alignment during this call |
| `allocation_count` | Successful allocation requests during this call |
| `failed_allocation_count` | Failed allocation requests during this call |

These are **overflow container bytes**. Inline generated arrays, atom payloads,
allocator headers and alignment padding are excluded. The first overflow request
includes the embedded first blocks. A fixed-capacity domain or a call that stays
within inline capacity makes no requests. A warmed default allocator can report
zero new allocations with nonzero cached/peak bytes.

Use successful-call peaks to size the pool, plus its padding/bookkeeping overhead.
Record an arena's own cursor high-water mark for exact physical consumption.
An exhausted call stops early: its statistics describe requests reached so far,
not the total capacity required for an eventual successful decomposition.

## Minimal scratch example

```cpp
#include "Core/HTNAtomOwner.h"
#include "Translator/HTNGeneratedPlanner.h"
#include <array>
#include <cstddef>
#include <memory>

struct BacktrackingScratch
{
    alignas(std::max_align_t) std::array<std::byte, 128 * 1024> bytes;
    size_t used = 0;
    static void* Allocate(void* user, size_t size, size_t alignment)
    {
        auto& scratch = *static_cast<BacktrackingScratch*>(user);
        void* pointer = scratch.bytes.data() + scratch.used;
        size_t remaining = scratch.bytes.size() - scratch.used;
        if (!std::align(alignment, size, pointer, remaining)) return nullptr;
        scratch.used = static_cast<std::byte*>(pointer) - scratch.bytes.data() + size;
        return pointer;
    }
    static void Deallocate(void*, void*, size_t, size_t) {} // Caller restores marker.
};

// context already has initialized prepared/execution storage and runtime services.
BacktrackingScratch scratch;
HTNBacktrackingAllocator allocator{
    &scratch, BacktrackingScratch::Allocate, BacktrackingScratch::Deallocate};
context.backtracking_allocator = &allocator;
const size_t marker = scratch.used;
HTNAtomOwner plan; // Lists use context.list_allocator or the default allocator.
const auto status = definition->decompose_call(&context, call.Get(), 1, plan.Get());
const auto usage = definition->get_execution_info(context.execution_storage)
    ->backtracking_allocations;
const size_t physicalScratchBytes = scratch.used - marker; // Includes padding.
scratch.used = marker; // Safe on success AND failure; plan remains independently owned.
context.backtracking_allocator = nullptr; // allocator is local to this scope.
```

128 KiB is an example, not a recommendation for every domain. Adapt the callbacks
to the engine allocator and record both usage and decomposition status. A callback
may implement explicit fallback; it must release each allocation through its origin.

## ABI and migration

The generated context and execution-info structs grow. Generated ABI revisions
are now **9** (plain/profiling) and **11** (debug/debug+profiling); runtime bridge
revision is **10** in every variant. Regenerate domains and rebuild the host,
SDK and runtime bridge together. Old domain/bridge ABIs are rejected before
execution. Atom representation and domain syntax are unchanged.
