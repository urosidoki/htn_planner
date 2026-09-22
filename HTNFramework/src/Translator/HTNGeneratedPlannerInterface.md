# Generated planner selection

Generated domains export an immutable `HTNGeneratedPlannerDefinition` accessor next to their
entry point. The host explicitly owns/chooses the definition it wants to execute; there is no
process-global generated-domain registry.

## Dynamic generated modules

`HTN_GENERATED_MODULE_EXPORTS` exports the generated `<EntryPoint>_GetDefinition` accessor from a
dynamic library. Dynamic modules link only to the small `HTNRuntimeBridge` forwarding library;
they must not statically link `HTNFramework`, because doing so would duplicate process-wide runtime
state such as interned symbols.
The test module contains one domain only to keep the integration test focused; an editor module may
export several domain accessors and be grouped by game, level, or domain family.

Before loading a generated module, the host loads `HTNRuntimeBridge` and calls
`HTNRuntimeBridge_Bind()` with `HTNCreateHostRuntimeAPI()`. Binding is an initialization-time
operation: the first compatible table wins and later calls are accepted only when the table is
identical. Shipping builds continue to compile generated domains directly into the executable.

The host table is `HTNHostRuntimeAPI`, declared in `Translator/HTNRuntimeBridge.h`.
Its `HTN_RUNTIME_BRIDGE_ABI_VERSION` has revision 6: callterm invocation receives the execution descriptor, including
client context, missing-callterm policy and report callback. Rebuild the host, bridge and imported domain modules together; old bridge
tables are rejected. This is separate from the planner descriptor ABI below.
Callterm C declarations live in `Translator/HTNCallTermBridge.h`; the
`HTNGeneratedCallTerm` representation and resolve export are unchanged; both
invoke exports now receive `HTNGeneratedPlannerContext` instead of bindings.
New generated code uses `HTNCallTermRegistry_InvokeGeneratedCallTermWithSource`.
See [Missing callterm policy](../../../docs/MISSING_CALLTERMS.md) for client configuration.

The module must outlive its definition and every prepared/execution storage object created from it.
For a custom core host, destroy all execution/prepared storage and stop using the
definition before unloading the domain module, then the runtime bridge.
With the optional HTNIntegration implementation, destroy planning units and clear
hook definitions first. DLL loading/reload/rollback remains the consumer's responsibility.

## ABI version

The status enum is `HTNDecompositionStatus`, declared in
`Core/HTNDecompositionStatus.h`. Its `HTN_DECOMPOSITION_*` values are unchanged.
The optional HTNIntegration entry point is `HTNPlanningUnit::DecomposeTopLevelMethod()` and its
existing overloads. It installs a plan and may invoke planning callterms, but
does not execute the resulting primitive tasks. Consumers continue to resolve
and complete primitives through the existing planning-unit API.

These are source API renames without aliases. Rebuild C++ consumers and regenerate
domain sources; the C descriptor ABI, callbacks' binary representation and
generated entry point/accessor names are unchanged.

`HTNGeneratedPlannerDefinition::abi_version` is the first field of every exported descriptor.
The host compares it with `HTN_GENERATED_PLANNER_ABI_VERSION` before reading storage sizes,
callbacks or optional metadata. Definitions with a different value are rejected by
the core function `HTNGeneratedPlanner_ValidateDefinition()`. It requires no hook,
storage allocation or DLL loader. HTNIntegration's hook calls the same function
and preserves the selected definition when a replacement is rejected.

The value contains the `0x4854` HTN ABI marker, a schema revision in its low 16 bits (currently
`1`), and two build-option bits that alter the descriptor layout:

| Bit | Required ABI option |
| --- | --- |
| 16 | `HTN_DEBUG_DECOMPOSITION` |
| 17 | `HTN_GENERATED_EXECUTION_PROFILING` |

This makes debug/release or profiling/non-profiling mismatches fail explicitly instead of letting
the host interpret callbacks at the wrong offsets. `HTN_PROFILE_DETAILED` does not alter the ABI.

Compatibility is deliberately exact: changing the descriptor layout or a callback signature
requires incrementing the schema and regenerating domains. No compatibility wrappers or alternate
descriptor forms are maintained.

The generated entry point is a direct input/output operation:

```cpp
HTNAtom Call = HTNAtom::sCreateCall(
    HtnSymbol::sGetSymbol("get_priority_for_role"),
    Candidate,
    RoleId,
    RoleContext);

HTNAtom Plan{};
const HTNDecompositionStatus Status = Definition->decompose_call(
    &Context,
    &Call,
    1,
    &Plan);

HTNAtom::sDestroy(Plan);
HTNAtom::sDestroy(Call);
```

`Call` is borrowed by generated execution and remains caller-owned. Its first list element is the
interned top-level method symbol and the remaining elements are its arguments. Generated code
validates the selected method and arity, copies the input arguments into its variable storage, and
never mutates the call atom.

The third argument controls entry visibility. Public calls pass `1` and may select only explicit
`top_level_method` entries. `HTNPlanningUnit` passes `0` internally when it resolves a deferred
plan step; this exposes generated `#` targets without publishing them as top-level methods.

`Plan` must not contain a live atom on entry. The generated entry point initializes it; on success
it owns a list of primitive-task atoms, and on failure it is left empty/unbound and safe to destroy.
There is no primitive-emission callback in the generated ABI.

Every root domain produces one independent `*.generated.c` source file and can be compiled,
linked, packaged, or loaded without unrelated planners. Demo/test harnesses may keep local explicit
lists solely for UI/test iteration; those lists are application-owned and are not part of
HTNFramework's runtime interface.

## Generated execution context

`HTNGeneratedPlannerContext` contains only services and storage required by generated execution:
world state, the per-entity callterm binding context, prepared storage, execution storage, and the
optional debugger. The binding context references an application-level `HTNCallTermRegistry` and
stores only that entity's daemon pointers. All daemon slots start as `nullptr`; a missing instance
is reported only if execution reaches a CallTerm that requires it.
CallTerms receive only the arguments declared in the domain. A stateful CallTerm obtains entity
state, including the permanent WorldState, from its daemon's `HTNPlannerHook`.
The requested call and decomposition output are explicit entry-point parameters rather than hidden context
state. Generated execution does not depend on `HTNPlannerExecutionContext`.

`HTNPlannerHook` adapts the planning-unit API to this generated ABI. A generated planner
definition is mandatory; planning has no fallback backend.

## Generated execution storage ownership

`HTNPlanningUnit` owns one reusable opaque execution-storage block for its generated backend.
Each entity owns an `HTNPlannerHook`, including its permanent WorldState reference and
`HTNCallTermBindingContext`; hooks are not shared between entities. The selected generated planner definition and the application-level callterm
registry are immutable runtime data and may be shared. The definition describes the storage layout
plus its initialize/destroy callbacks, and the planning unit reuses that storage across planning calls.

This preserves the parallel model: one shared generated definition and callterm registry, plus an
independent PlannerHook + PlanningUnit + WorldState + execution storage per agent/job.


## Hierarchical branch backtracking

A method branch is speculative until both its preconditions and the complete decomposition subtree
scheduled by that branch succeed. Passing the branch preconditions is therefore **not** a commit.

This distinction is important:

- If a branch precondition fails, the method immediately tests its next branch. This is ordinary
  local branch selection.
- If a branch precondition succeeds but a compound task inside that branch later cannot decompose,
  the planner restores the state from before the parent branch and tests the parent's next branch.
- Primitive/deferred plan steps, pending tasks and variable bindings produced by the failed subtree
  must not leak into the alternative branch.
- Runtime errors such as out-of-memory or generated fixed-backtracking-capacity exhaustion are real
  errors, not semantic decomposition failures, and are propagated instead of selecting another
  branch.

For example, assume `(in_combat_state)` is absent:

```lisp
(:method (do_behavior ?threat)
    (branch_combat () ((do_combat ?threat)))
    (branch_search () ((do_search ?threat))))

(:method (do_combat ?threat)
    (branch_fire (and (in_combat_state)) ((!fire ?threat)))
    (branch_wait (and (in_combat_state)) ((!log "combat wait"))))
```

`do_behavior.branch_combat` is initially applicable, but `do_combat` cannot select a valid branch.
The failed `branch_combat` subtree is rolled back and `do_behavior.branch_search` is then tested.
This is the same semantic outcome as placing `(in_combat_state)` directly on
`do_behavior.branch_combat`: in both cases the planner can continue with `branch_search`.

Generated execution implements this behavior synchronously inside the method invocation;
decomposition remains atomic and does not require a
cross-frame continuation or an `IN_PROGRESS` result.

## Runtime backtracking mode

`HTNBacktrackingMode` is a runtime policy, but generated code supports it only when
`HTNGeneratedRuntimeBacktrackingSupport::Enabled` is selected at translation time. This is
independent from `HTNGeneratedBacktrackingPolicy`, which controls backtracking storage.

The default generation mode is `Disabled`. In that mode generated code keeps full backtracking
semantics (equivalent to `HTN_BACKTRACKING_ALL`) and emits no runtime-mode checks. Enabling
runtime support adds the `HTN_GENERATED_FEATURE_RUNTIME_BACKTRACKING` capability to the generated
planner definition and makes the same generated planner honor per-planning-unit runtime modes.

When runtime support is enabled, changing `HTNBacktrackingMode` does not require regenerating the planner: the same generated
planner can be executed with a different mode for each `HTNPlanningUnit` or planning call.

`HTNPlanningUnit` defaults to `HTN_BACKTRACKING_ALL` and exposes `SetBacktrackingMode()` /
`GetBacktrackingMode()` for per-agent configuration. Generated execution receives that mode
through `HTNGeneratedPlannerContext`.

The mode is a bitmask:

| Value | Runtime behavior |
| --- | --- |
| `HTN_BACKTRACKING_NONE` | Disables optional fact/axiom alternatives and hierarchical branch retry. |
| `HTN_BACKTRACKING_FACTS_AND_AXIOMS` | Enables retrying alternative solutions produced by facts and axioms. |
| `HTN_BACKTRACKING_BRANCHES` | Enables restoring a selected branch after its child subtree fails and trying the method's next branch. |
| `HTN_BACKTRACKING_ALL` | Enables both forms of backtracking and is the default `HTNPlanningUnit` mode. |

The branch flag does not control ordinary branch selection. If a branch's own preconditions fail,
the method can always test its next branch. `HTN_BACKTRACKING_BRANCHES` controls the additional
hierarchical behavior required when a branch passed its preconditions, selected a subtree, and that
subtree later failed to decompose.

For facts and axioms, disabling `HTN_BACKTRACKING_FACTS_AND_AXIOMS` commits the first solution
selected by a choice-producing condition. If a later condition invalidates that binding, the
planner does not restore the earlier fact/axiom choice point to request another solution.

The storage and runtime-support generation policies are orthogonal. For example, a planner may use
`HTNGeneratedBacktrackingPolicy::FixedCapacity` with runtime backtracking support either enabled or
disabled. Once runtime support is enabled for that generated planner, the same generated C may be
shared by planning units using different `HTNBacktrackingMode` values.

The CLI option is `--runtime-backtracking-support=disabled|enabled` and defaults to `disabled`.

## Execution client context

Set `HTNGeneratedPlannerContext::client_context` for core execution, or
`HTNPlannerExecutionContext::ClientContext` through the integration hook.
The integration planning units expose `GetExecutionContext()`; set ClientContext,
MissingCallTermPolicy, MissingCallTermCallback and BacktrackingMode directly.
They copy runtime options into each execution, including deferred calls. The borrowed pointer is not kept in bindings
or cached generated storage. Converters and missing-callterm callbacks receive it.

This changes the generated execution ABI: plain `0x48540004`, debug `0x48550005`,
profiling `0x48560004`, debug/profiling `0x48570005`. Regenerate and rebuild domains,
host and runtime bridge together. See [conversion migration](../../../docs/TYPE_CONVERSION.md).
