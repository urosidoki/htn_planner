# Per-instance generated list allocation

`HTNGeneratedPlannerContext::list_allocator` optionally borrows a C++
`HTNAtomListAllocator`, exposed as `void*` in the generated C ABI. Zero-initialize
the context. `nullptr` preserves the previous allocation policy; it does not
select or modify a process-wide or thread-local allocator.

With HTNIntegration, configure the same service through
`unit.GetExecutionContext().ListAllocator`. The hook forwards it to generated
execution. The interpreter does not use this field.

## Which allocations use it?

| Storage or operation | Allocation policy |
| --- | --- |
| Returned plan, task argument lists and deferred argument lists | Configured allocator, recursively |
| Runtime list expressions, including nested lists and evaluated call results | Configured allocator, recursively |
| Copies into variables, snapshots, axiom/compound inputs and outputs | Configured allocator for owning list payloads |
| Owning results of `split_list_front` / `split_list_back` | Configured allocator, including nested elements |
| Fixed variable, snapshot and call-frame arrays | Existing generated execution storage |
| Prepared literal lists | Existing prepared storage; owning copies use the configured allocator |
| Overflow continuation containers | Optional separate `backtracking_allocator`; owning list payloads still use `list_allocator` |
| Debugger history with an instance allocator configured | Independent default new/delete copies, so history does not retain or exhaust the instance pool |
| Symbols, strings, STL containers and storage blocks themselves | Existing allocation policy; this is a list-node allocator |

At the end of a decomposition using an instance allocator, generated variable
and snapshot payloads are released, including on failure. The returned plan owns
its nodes independently. Deferred expansion uses the allocator configured on that
expansion's context; it does not implicitly inherit one from its input value.

Client inputs, world-state values and callterm results keep their original
owners. Generated **copies** allocate new nodes through the configured allocator;
no existing node is reparented. A callterm binding's own allocations are the
binding's responsibility. A returned value moved directly into a variable keeps
its originating allocator until destroyed; copies into plans/snapshots and lists
embedded in runtime expressions use the instance allocator. Moves always transfer
ownership without allocation. Outside generated execution, ordinary atom copies
continue to preserve the source's allocator; use `HTNAtom_CopyWithAllocator` or
`HTNAtom_AssignCopyWithAllocator` to request an explicit deep copy elsewhere.

## Fixed pool and safe fallback

`HTNPooledAtomListAllocator(capacity)` has a fixed number of nodes. When exhausted,
`Allocate()` returns `nullptr`. `HTNSafePooledAtomListAllocator(capacity, fallback)`
first tries its pool, then the borrowed fallback. The default fallback is
`HTNNewDeleteAtomListAllocator::Get()` (one non-throwing new/delete per node).
Pool slots that become free are reused even while fallback nodes remain alive.
Deallocation always goes to the origin of the node, including nested lists.

```cpp
#include "Core/HTNAtomListAllocator.h"
#include "Core/HTNAtomOwner.h"
#include "Translator/HTNGeneratedPlanner.h"

// definition was validated; prepared/execution were initialized with its callbacks.
HTNDecompositionStatus Run(const HTNGeneratedPlannerDefinition* definition,
    HTNWorldState& world, HTNCallTermBindingContext& bindings,
    void* prepared, void* execution, const HTNAtom& call)
{
    HTNPooledAtomListAllocator pool(4096);
    // Or: HTNSafePooledAtomListAllocator pool(4096,
    //         HTNNewDeleteAtomListAllocator::Get());

    HTNGeneratedPlannerContext context{};
    context.world_state = &world;
    context.callterm_binding_context = &bindings;
    context.callterm_error_policy = HTNCallTermErrorPolicy::FailSilently;
    context.backtracking_mode = HTN_BACKTRACKING_ALL;
    context.prepared_storage = prepared;
    context.execution_storage = execution;
    context.list_allocator = &pool;

    HTNAtomOwner plan; // Destroyed before pool. May be inspected after decompose_call.
    const auto status = definition->decompose_call(&context, &call, 1, plan.Get());
    // Consume plan here. On OOM, get_execution_info(execution)->last_error explains why.
    return status;
}
```

For retained plans, make the pool an instance member that outlives those plans,
their copies and the planning unit. Never return a pooled plan from the example
above: its local pool would be destroyed. The allocator and its fallback are
borrowed, not owned by the context. Do not reset or destroy either while any
referencing list remains alive. Custom allocators must destroy `node->data` in
`Deallocate`, as the built-in allocators do, before releasing the node's storage.

The fixed and safe pools are not synchronized. Independent planners can run
concurrently with separate pools, contexts and execution storage. Sharing one
allocator requires external synchronization (or a thread-safe implementation),
including when retained values are destroyed on another thread. The stateless
new/delete fallback can be shared.

## Exhaustion and diagnostics

Failure to allocate a generated owning list returns
`HTN_DECOMPOSITION_OUT_OF_MEMORY`, sets `execution_info.last_error`, and releases
partial values. It is fatal to that attempt, not a logical failure that selects
another branch. With a configured allocator, a failed decomposition returns an
unbound output. A safe pool fails only if its fallback also returns `nullptr`.
The pool object and execution storage can be reused after failure.

The reference planning unit also checks copies into its active plan: exhaustion
returns `OUT_OF_MEMORY` from top-level decomposition, or `Failed` from deferred
task resolution, and clears the active plan. These integration copy failures use
its existing log diagnostics; generated execution diagnostics describe the
decomposition itself.

## ABI and migration (unreleased)

The context gains an opaque pointer. `HTNAtom`, `HTNAtomList` and their node layout
are unchanged. Including the subsequent backtracking allocator, generated planner
ABI revisions become **9** (plain/profiling) and **11** (debug/debug+profiling);
runtime bridge revision becomes **10** in every
variant. Rebuild the SDK, host and runtime bridge, and **regenerate and recompile
all domains**. Previously generated source does not implement this allocation
policy. Old domain descriptors and old runtime bridge tables are rejected before
execution. Do not combine binaries from different SDK revisions.

For the separate call-local allocator, per-call memory statistics and a lifetime
table, see [Backtracking allocation](BACKTRACKING_ALLOCATOR.md).
