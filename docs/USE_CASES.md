# Planner use cases

The planner selects a sequence of tasks. Your application executes those tasks,
updates facts and decides when to plan again. Primitive tasks (`!move`, `!attack`)
are output data: producing them does not perform an action. Callterms run during
decomposition, so their side effects require care when trying alternatives.

The following are integration patterns, not built-in scheduling modes. Names such
as `reasoning`, `behave` and `active_plan` are ordinary top-level method names.

## 1. NPC with separate reasoning and plan validation

Use this when refreshing perception and validating an existing action should
not replace the behavior plan on every update.

```text
Refresh world state -> reasoning
                         |
                         +-- failure -> client handles failed update
                         |
                         +-- success -> active plan exists?
                                           | no: behave -> execute new plan
                                           | yes: active_plan
                                                    | success: continue
                                                    | failure: cancel -> behave
```

- `reasoning` performs administrative work such as selecting a threat or updating
  memory through registered callterms. If it emits primitive tasks instead, execute
  them before relying on their effects.
- `behave` chooses the action sequence.
- `active_plan` checks facts describing the current target, order and validity.
  It succeeds without producing actions when continuation is allowed. An
  unconditional fallback would incorrectly accept an invalid plan.

Use three independent planning units sharing the NPC's world state and bindings,
and invoke them sequentially. Their output plans and execution storage remain
independent. A validation decomposition must never overwrite the behavior unit.
The host publishes active-plan metadata as facts; the domain cannot inspect an
engine action object automatically.

**Expected flow:** plan an attack on A, preserve progress while A remains valid,
then lose A, fail validation, cancel the attack and plan a search.

## 2. One behavior entry point with plan comparison

Use this when the client can cheaply compare a new candidate plan with its active
plan. Invoke `behave` using separate candidate storage or a candidate planning unit.

| Result | Client action |
| --- | --- |
| Equivalent candidate | Keep the active action, task index and progress. |
| Different candidate | Request cancellation, then adopt the candidate safely. |
| No plan | Apply an explicit policy: retain the current action or cancel it. |
| Successful empty plan | Handle deliberate completion; do not treat it as failure. |

Define equivalence in terms of relevant task arguments, targets and action identity.
Do not call decomposition on the active reference planning unit and expect its old
plan to remain untouched: decomposition replaces that unit's result.

**Expected flow:** repeated patrol decisions preserve movement progress; a new
threat produces an attack candidate and triggers a controlled transition.

## 3. Prioritized combat NPC

Start with [AAACombatNPC](https://github.com/urosidoki/htn_planner/blob/main/Domains/AAACombatNPC.domain) and the
[scenario guide](AAA_COMBAT_NPC_DEMO.md). Its included domains demonstrate reusable
behavior, threat selection, melee/ranged choices and orders. Run it in HTNDemo's
Domain Runner with the supplied world states and inspect the generated debugger.

Separate pre-combat investigation from searching for a previously engaged enemy.
The host executes movement, weapon changes and attacks, and updates facts after
actions complete. Branch order expresses priority; it does not schedule actions.

## 4. Squad coordination

Use one squad planner for role assignment and independent NPC planners for local
execution. Publish a shared observation snapshot to the squad planner, obtain
orders such as defend, flank or suppress, then apply them through host execution.

Each NPC receives its own order facts and chooses its detailed behavior using its
own world state, execution storage and client services. Definitions can be shared;
mutable planner storage must not be shared between concurrent jobs.

**Expected flow:** the squad assigns a flanker, the NPC chooses a local route, and
the next squad decision reassigns the role if that NPC becomes unavailable. The
host resolves conflicting reservations and defines whether orders can interrupt
combat. This is an integration design, not a bundled runnable squad demo.

## 5. AI Director

Use a world-level domain to choose encounter pacing, reinforcements, objectives
or mission progression from facts such as intensity, available spawn locations,
player progress and cooldowns.

```text
World observations -> director plan -> client applies world actions
                                           |
                                           v
                              NPCs receive updated facts/orders
```

For example, high intensity can choose a recovery phase, while low intensity and
an expired cooldown can choose an encounter. Spawning and resource consumption
should be executed as client actions after plan selection, avoiding irreversible
work during speculative conditions. The host owns update cadence and budgets.
This is an integration design, not a bundled runnable Director demo.

## 6. Deferred planning and domain iteration

A task `(&method arguments...)` defers expansion until the host reaches it and
requests resolution. The latest world state can then select a different branch.
See [Wanderer](https://github.com/urosidoki/htn_planner/blob/main/Domains/Wanderer.domain) for deferred behavior.

For runtime domain iteration, the hot reload example demonstrates client-owned
compilation, loading, ABI validation and replacement. An engine must provide its
own watching, build invocation and synchronization, and define what happens to
active plans and pending deferred tasks. The SDK does not supply automatic hot
reload for an arbitrary engine.

## Runtime configuration

Configure each execution's [missing-callterm policy](MISSING_CALLTERMS.md) and
optional [client services](TYPE_CONVERSION.md). Keep borrowed client data alive
through all synchronous callbacks. Treat conversion failure, a missing callterm,
no valid plan and successful empty output as distinct situations in diagnostics.
