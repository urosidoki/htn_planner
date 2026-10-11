# Generated execution debugger

`HTNGeneratedDebugger` records the execution of native generated planners as a tree
of structured events. It consumes source metadata emitted by the compiler.

## Data flow

```mermaid
flowchart LR
    IR["Compiler IR with source ranges"] --> Generated["Generated C and debug metadata"]
    Generated --> Events["Generated execution events"]
    Events --> Capture["HTNGeneratedDebugger"]
    Capture --> View["Application or ImGui view"]
```

The generated metadata maps runtime indexes back to methods, branches, conditions,
axioms, tasks, constants and source ranges. During planning, generated code reports
begin and end events. `HTNGeneratedDebugger` turns those events into nodes containing:

- Parent and child relationships.
- Started, completed and succeeded state.
- Domain file, line and column ranges.
- Display tokens for the executed expression.
- Constants referenced by the event.
- Bound variables before and after the event.
- Historical alternatives created by backtracking and retries.

### Source expressions and compiler temporaries

Condition titles preserve the original domain expression, including nested calls,
arithmetic and parentheses. For example:

```lisp
(= ?new_entity_position (call get_entity_position ?entity_id))
(< (call get_distance_from_to ?old_entity_position ?new_entity_position) 0.2)
```

The compiler can lower one expression into several internal assignments. These
steps still execute, but their metadata explicitly marks them as internal: they
do not create separate debugger rows or expose temporary variables in the watch.
The visible row reports the result of the whole expression. Backtracking can
create additional attempts of that same source expression.

Composite conditions and declarations use `...` for their child rows; leaf
conditions and tasks display complete parenthesized expressions. This is a
structured source view, not a reproduction of comments or original indentation.
The visible assignment source range covers the assignment, while a callterm error
inside its initializer continues to report the location of `(call ...)`.

This correction changes debug metadata layout. Regenerate instrumented domains
and rebuild their hosts together (debug planner descriptor revision 9). Plain
and profiling-only planner layouts, RuntimeBridge ABI and `HTNAtom` are unchanged.

## Build configuration

The runtime, generated domain and consuming application must be built with
`HTN_DEBUG_DECOMPOSITION` to expose debug metadata and event capture. This changes the
generated planner ABI, so modules and their host must use matching configurations.
Regenerate and rebuild domain code after changing this setting.

The repository's `Debug` configuration enables the feature. Instrumented SDK variants
also enable it. Release and ordinary Profile configurations omit the metadata and
capture overhead.

## Attaching a debugger

The application owns the debugger and must keep it alive while the planning unit can
use it:

```cpp
#ifdef HTN_DEBUG_DECOMPOSITION
HTNGeneratedDebugger GeneratedDebugger;
GeneratedDebugger.SetEnabled(true);
PlanningUnit.SetGeneratedDebugger(&GeneratedDebugger);
#endif
```

`HTNPlanningUnit` passes the pointer through the generated execution context. Before
each generated decomposition, `HTNPlannerHook` resets the capture and supplies the
source file from the active planner definition. Planning then populates the event tree
synchronously. The application can read `GetNodes()`, `FindNode()` and `GetRevision()`
after the call completes.

The pointer is non-owning. Detach it or ensure the planning unit is destroyed before
the debugger object goes out of scope.

## Visual presentation

`HTNDemo/src/UI/HTNGeneratedDebuggerView.h` is the ImGui reference renderer. It shows
the captured hierarchy, successful and failed alternatives, retry counts, source
locations, constants and bound variables. Normal mode projects the event stream into
a concise planning view; verbose mode exposes the underlying event hierarchy.

The view is deliberately separate from capture. An integration can render the same
`HTNGeneratedDebugger::Node` data in an engine editor, telemetry tool or custom UI
without linking its planner execution to ImGui.

## Runtime boundary

The debugger observes generated execution through the generated runtime bridge. It
does not evaluate conditions, choose branches or affect backtracking. Disabling the
debugger removes captured state but does not change planning results.

## Capture cost and diagnostics

Capture retains every visible attempt, including unsuccessful retries, skipped
conditions and the before/after bindings. Pending-condition lookup examines the
metadata placeholders rather than repeatedly scanning completed attempts.
Condition titles are tokenized once per decomposition and copied into owning
nodes; variable snapshots reserve only the bindings they actually contain.
Snapshots still own their strings/atoms and remain usable after reset or module
unloading. There is no automatic history truncation.

Build with the existing `--generated-execution-profiling` Premake option to enable
optional debugger performance counters (no `HTN_PROFILE_DETAILED` required):

```cpp
debugger.SetPerformanceCountersEnabled(true);
// Reset the debugger and execute a decomposition as usual.
const auto& counters = debugger.GetPerformanceCounters();
```

Counters report event/capture/search nanoseconds, nodes created, pending children
examined, capture calls, variable slots examined and copied atom/list values.
List counts refer to top-level captured list values, not their individual nodes.
Capture and search times are included in event time. The timers themselves have
overhead: use a separate diagnostic pass, not representative latency samples.
`Reset()` clears counters. A disabled debugger performs no captures or node
allocations even when performance counters are enabled.

For the opt-in 16–1024 event benchmark and a separately measured deep snapshot
copy, see [HTNBenchmark](../HTNBenchmark/README.md#fact-retries-and-generated-debugger-opt-in)
and the [fact retry performance report](FACT_RETRY_PERFORMANCE.md). Complete
histories of large nested searches still consume significant time and memory.
