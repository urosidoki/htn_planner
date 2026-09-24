# Missing callterm policy

Runtime options are direct fields of the execution descriptor. Configure them
through `HTNPlannerExecutionContext` (reference integration) or
`HTNGeneratedPlannerContext` (core generated execution). Callterm bindings contain
only registry/daemon configuration and do not own policies or callbacks.

```cpp
void ReportMissingCallTerm(void* clientContext, const HTNMissingCallTermInfo* info)
{
    auto& engine = *static_cast<EngineHTNContext*>(clientContext);
    // Use engine.Diagnostics and info->Reason / Name / DaemonID / Source.
}

auto& execution = planningUnit.GetExecutionContext();
execution.ClientContext = &engineContext;
execution.MissingCallTermPolicy = HTNMissingCallTermPolicy::Report;
execution.MissingCallTermCallback = ReportMissingCallTerm;
execution.BacktrackingMode = HTN_BACKTRACKING_ALL;
```

Planning units copy these options for every decomposition, including deferred
calls. They supply their own world state, bindings, call and storage pointers in
the per-invocation copy. Configure options only while idle. If calling the hook
directly, populate an `HTNPlannerExecutionContext` and pass it to `Decompose`.
No separate runtime-options substructure is required.

Core-only generated hosts use the equivalent direct fields:

```cpp
HTNGeneratedPlannerContext execution{};
execution.callterm_binding_context = &bindings;
execution.client_context = &engineContext;
execution.missing_callterm_policy = HTNMissingCallTermPolicy::Report;
execution.missing_callterm_callback = ReportMissingCallTerm;
// Also provide world_state, backtracking_mode, prepared_storage and execution_storage.
```

C clients use `HTN_MISSING_CALLTERM_UNSET`, `HTN_MISSING_CALLTERM_FAIL_SILENTLY`
and `HTN_MISSING_CALLTERM_REPORT`. Policy/reason enums use 32-bit representations
and the callback takes an info pointer in both C and C++.

## Policy and failure behavior

- `Unset` is zero, the initial value of a zero-initialized descriptor. A missing
  callterm triggers an SDK assert with an explicit configuration message.
- `FailSilently` returns failure without logging or reporting.
- `Report` invokes the callback with the current execution's client pointer,
  then returns failure if the callback returns. The client decides whether to
  log, assert or terminate. A null callback triggers an SDK assert.
- Unknown enum values also assert on a missing invocation.

With assertions disabled, invalid/missing configuration fails safely without
calling a null callback. Valid callterms execute normally even with `Unset`;
configuration checks occur when a missing invocation must be handled.
Argument/return conversion failures and a bound callable returning false/unbound
are ordinary callterm failures, not missing-callterm reports.

## Reasons and provenance

- `NotRegistered`: the name is absent from the registry.
- `MissingBinding`: the registry entry has no callable.
- `MissingInstance`: a member callable exists but its daemon instance is absent;
  `DaemonID` identifies the required daemon type.

Reports include the name, reason, optional daemon ID and source provenance.
Generated calls supply domain, file, line and column even without debugging.
Calls without provenance use null strings/zero coordinates. Preparing a cached
slot does not report: only actually attempting the missing call does.

All report data is borrowed during the callback; copy strings to retain it.
Callbacks are synchronous, and the client owns its payload/services. Keep them
alive throughout execution and while planning units may expand deferred calls.
Do not reconfigure an active execution or throw across the runtime boundary.

Multiple executions can share bindings while selecting different policies,
callbacks and client pointers. Changes between executions take effect without
rebuilding or invalidating cached callterm slots. Clients synchronize any shared
mutable services. Registry mutation during execution remains unsupported.

## Migration and ABI

Remove `SetMissingCallTermPolicy` calls on bindings. Assign the execution's policy
and callback directly. Change report callbacks from an info reference to a pointer.
Both C invocation exports now take an `HTNGeneratedPlannerContext*` as their first
argument; resolving a slot still takes the binding context. The C++ registry
`Execute` takes `HTNPlannerExecutionContext`.

Planner ABI versions: plain `0x48540004`, debug `0x48550005`, profiling
`0x48560004`, debug/profiling `0x48570005`. Runtime bridge revision: **6**.
Regenerate domains and rebuild hosts, runtime bridge and modules together.
Old definitions/tables are rejected; the atom and cached-callterm layouts remain
unchanged. See [type conversions](TYPE_CONVERSION.md) for converter migration.

## Production fallback validation

The SDK's external CoreConsumer checks both the C++ registry and the generated
invocation API for unregistered names, absent bindings and missing daemon instances.
It checks FailSilently and Report in all variants. Release variants additionally
check Unset, an invalid enum value and Report without a callback: each returns an
unbound failure without invoking a callback. A registered working call remains
callable under each policy. Checks use return codes, not assertions.

The SDK manifest propagates NDEBUG for Release and _DEBUG for Debug, matching
the compiled library. Development Release does not currently disable assertions;
its death tests do not replace these packaged production checks. No API or ABI
change is required for this coverage.
