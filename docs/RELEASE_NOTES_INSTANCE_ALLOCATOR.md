# HTN Planner 2.4.0: per-instance list and backtracking allocation

- Add optional borrowed `list_allocator` to `HTNGeneratedPlannerContext` and
  `ListAllocator` to `HTNPlannerExecutionContext` for HTNIntegration clients.
- Route generated owning list copies, runtime expressions, list operations,
  plan arguments and deferred values through the configured allocator, retaining
  fixed execution/prepared storage and original owners on moves.
- Report list allocation failure as `HTN_DECOMPOSITION_OUT_OF_MEMORY`, clean up
  partial results, and preserve the default allocation policy when unset.
- Add `HTNSafePooledAtomListAllocator`: fixed pool with a borrowed fallback,
  defaulting to `HTNNewDeleteAtomListAllocator`. Each node is released through its
  allocator of origin. The fallback is also allowed to fail.
- Isolate debugger history from a configured bounded pool. Check active/deferred
  plan copies in the reference integration as well as generated execution.
- Add allocator tracking, per-allocation failure injection, retained-plan,
  backtracking, deferred, concurrent-instance and incompatible-domain tests.
- Add optional `backtracking_allocator` / `BacktrackingAllocator` for overflow
  containers. Custom blocks are released before each decomposition returns, so
  callers can restore a scratch marker while retaining the independent plan.
- Expose per-call overflow usage in `execution_info.backtracking_allocations` and
  `HTNPlanningUnit::GetGeneratedExecutionInfo()` without profiling flags: peak,
  current and requested bytes, request size/alignment, allocations and failures.
- Document allocator lifetimes and test scratch reset, exhaustion, default cache
  reuse, deferred calls and concurrent instances.

**Binary compatibility:** generated context and runtime bridge ABI revisions
increase. Regenerate domains and rebuild all consumers and modules together.
Atom/list representation and domain syntax do not change. Published SDK packages
are not modified by this change. See [2.4.0 release notes](RELEASE_2_4_0.md)
for release status and migration.

See [allocation policy, lifetime rules and C++ example](INSTANCE_LIST_ALLOCATOR.md).
See also [backtracking scratch allocation and statistics](BACKTRACKING_ALLOCATOR.md).
