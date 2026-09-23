# Axiom overloading

Axiom calls use the name and number of arguments to select exactly one declaration. Parameter names, runtime types and the `?inp_`, `?out_` and `?io_` modes do not select an overload. These modes retain their existing binding and matching semantics after selection.

```lisp
(:domain Example top_level_domain
    (:method (run) top_level_method
        (ready (and (#available) (#available 7)) ((!ready))))
    (:axiom (available) (and (enabled)))
    (:axiom (available ?inp_id) (and (enabled_for ?inp_id)))
)
```

- Declarations may appear after their callers.
- Duplicate names with the same arity are rejected, including declarations that differ only in parameter modes.
- Calls with no matching arity fail during linking, with a source location and argument count.
- Includes and qualified `#Base::available` calls resolve the exact signature. An `overrides Base` declaration replaces only that signature; other inherited overloads remain available.
- Dependency-cycle validation tracks signatures. Calling another arity of the same name is allowed if it does not create a cycle. Recursive axiom dependency cycles remain unsupported and are rejected; diagnostics include arities.
- Generated execution and compiler editor definition lookup select the matching overload.
- Axiom overloading itself preserves generated entry-point names and plan symbols. Release 2.0.0 changes the runtime ABI through its execution-context updates; regenerate domain C and rebuild the host and modules together. See [migration notes](RELEASE_2_0_0.md).

The executable fixture `Domains/Test/axiom_overloads.domain` covers overloads with input, output and IO parameters, arithmetic arguments, exact base calls, overrides, nested calls and backtracking. It uses `WorldStates/Test/axiom_overloads.worldstate`.

## Backtracking and output arguments

Generated multi-solution axioms support facts, nested axioms, nested `and`, `alt`,
`or`, `not`, comparisons, list splitting and callterm conditions/bindings. The
compiler emits C continuations and retry labels. It resumes at the latest choice
point rather than replaying an axiom body to find its next solution. No interpreter
nodes or generic runtime condition evaluator are involved.

- `and` searches combinations in source order.
- `alt` retains alternatives, including a later child after an earlier successful
  child has exhausted its solutions.
- `or` commits to the first successful child and discards its alternatives.
- `not` checks for a solution and restores its temporary bindings; it exports no
  choices. Host/WorldState side effects are not rolled back.
- A callterm before a choice point runs once for that traversal. A callterm after
  the choice point runs again for each new candidate that reaches it. There is no
  global exactly-once guarantee across separate calls or new planning executions.
- Runtime fact/axiom backtracking controls apply when generated runtime mode
  selection is enabled. Declaration order remains unrestricted; axiom dependency
  cycles remain rejected.

Both backends use the same output contract, with or without domain validation:

| Parameter | Caller argument | Behavior |
| --- | --- | --- |
| `?inp_` | Bound value | Initialize the local input. |
| `?out_` | Unbound variable | Compute the local output and bind the caller on success. |
| `?out_` | Bound variable, literal or arithmetic expression | Compute the local output independently and require atom equality with the caller's value. |
| `?io_` | Bound value | Initialize the local parameter and require a matching output. |
| `?io_` | Unbound variable | Enumerate and export outputs. |

An output mismatch rejects that candidate and tries the next available alternative.
All outputs are checked before any are published. Repeated caller variables across
output parameters must receive equal values. Failed candidates restore bindings;
owned atom values survive suspension and their temporary copies are released on commit or exhaustion.

```lisp
(:method (run) top_level_method
    (choose (and (#candidate_value (++ 1))) ((!selected 2))))
(:axiom (candidate_value ?out_value)
    (and (candidate ?out_value)))
```

With facts `(candidate 1)` and `(candidate 2)`, the first candidate is rejected and
the second matches the evaluated argument `2`.

`Domains/Test/nested_axiom_choices.domain` and `HTNGeneratedAxiomTest` cover output
and IO backtracking, bound/literal/arithmetic arguments, aliased outputs, nested
logical forms, callterm traces, owned strings, planner reuse and debugger node
completion. Regenerate domain C when adopting the correction; no runtime ABI
layout change is required.
