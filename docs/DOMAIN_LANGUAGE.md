# Domain language guide

This guide describes the domain language supported by HTN Planner **2.4.0**.
It starts with a complete example, then explains the syntax and execution rules.
The linked feature documents provide more detail and migration notes.

Native negative numeric literals were introduced in 2.3.0; see the
[syntax and compatibility notes](RELEASE_NOTES_NEGATIVE_LITERALS.md).

The language is a small, declarative **domain-specific language (DSL)** for
Hierarchical Task Network planning. It uses **S-expressions**: parenthesized forms
whose first element identifies an operation or a piece of data. It is not a
general-purpose Lisp implementation.

## Contents

- [A first domain](#a-first-domain)
- [Reading the syntax](#reading-the-syntax)
- [Facts and variables](#facts-and-variables)
- [Methods, branches and tasks](#methods-branches-and-tasks)
- [Axioms and parameter modes](#axioms-and-parameter-modes)
- [Conditions and backtracking](#conditions-and-backtracking)
- [Assignment](#assignment)
- [Comparisons and arithmetic](#comparisons-and-arithmetic)
- [Callterms](#callterms)
- [Lists and list splitting](#lists-and-list-splitting)
- [Constants, includes and overrides](#constants-includes-and-overrides)
- [Deferred decomposition](#deferred-decomposition)
- [Validation and common mistakes](#validation-and-common-mistakes)
- [Where to go next](#where-to-go-next)

## A first domain

Save this complete example as `Tutorial.domain`:

<!-- example: Tutorial.domain; complete -->
```lisp
(:domain Tutorial top_level_domain
    (:method (behave) top_level_method
        (approach_visible_enemy
            (and
                (visible_enemy ?enemy ?position)
                (not (disabled ?enemy))
            )
            (
                (!move_to ?position)
                (!look_at ?enemy)
            )
        )
        (idle
            ()
            (
                (!wait 1.0)
            )
        )
    )
)
```

The host supplies a **world state** containing facts. For illustration, suppose
it contains this row, and no matching `disabled` row:

```text
visible_enemy(enemy_7, (10.0 0.0 4.0))
```

When the host asks the planner to decompose `behave`:

1. It tries the first branch, `approach_visible_enemy`.
2. The fact query binds `?enemy` to the symbol `enemy_7` and `?position` to the list
   `(10.0 0.0 4.0)`.
3. `not` succeeds because there is no matching `disabled` fact.
4. The branch produces this plan:

   ```text
   (!move_to (10.0 0.0 4.0))
   (!look_at enemy_7)
   ```

5. The **host executes** these actions. Producing the plan does not move an entity.

If the first branch cannot produce a plan, the planner tries `idle`. Its empty
condition body `()` is unconditional and produces `(!wait 1.0)`.

`visible_enemy`, `disabled`, `move_to`, `look_at` and `wait` are application-defined
names. The planner knows how to query facts and build tasks; the host defines
what those names mean in its game or application.

Check and translate the domain from a shell where `HTNTranslator` is available:

```powershell
HTNTranslator --check Tutorial.domain
HTNTranslator Tutorial.domain CreateTutorialHTN Generated
```

`CreateTutorialHTN` names the generated C entry point; it is not the domain method
name. The host selects `behave` when requesting a decomposition. Translation
produces C source to compile and link or load with the matching HTN SDK.

## Reading the syntax

Whitespace and line breaks separate elements; parentheses define nesting.
Indentation is for people, not part of the grammar. Use `//` for comments through
the end of the line. C-style block comments and Lisp `;` comments are unsupported.

Use letters, digits and underscores for names, starting with a letter or
underscore. Names are case-sensitive. Language keywords such as `and`, `call`
and `method` are reserved.

| Form | Meaning |
| --- | --- |
| `(:domain Name top_level_domain ...)` | Root domain declaration. |
| `(:method (name ...) ...)` | A way to decompose a compound task. |
| `(:axiom (name ...) ...)` | A reusable condition, optionally producing outputs. |
| `(visible ?entity)` | A fact query in a condition. |
| `(#select ?entity ?result)` | An axiom call in a condition. |
| `(call get_time)` | A host callterm, executed during decomposition. |
| `(!move_to ?position)` | A primitive task to put in the plan. |
| `(move_to ?position)` | A compound task to decompose now. |
| `(&move_to ?position)` | A compound task to decompose later. |
| `?name` | A variable. |
| `@name` | A domain constant. |
| `Base::move_to` | A method qualified by its declaring domain. |

### Values

| Value | Example | Notes |
| --- | --- | --- |
| Integer | `42`, `-1` | Signed 32-bit runtime value. |
| Float | `0.2`, `10.0`, `-0.5` | Single-precision runtime value. |
| Boolean (runtime) | Returned by a host callterm or bound from a fact | Matches integer `1`/`0`; no boolean literal syntax. |
| Symbol | `moving_to_enemy`, `true`, `false` | An interned identifier, distinct from a string or boolean. |
| String | `"Moving to enemy"` | Quoted text. |
| List | `(patrol 3 true (1.0 2.0 3.0))` | Ordered, heterogeneous, nestable values. |
| Variable read | `?position` | Uses the current binding. |
| Constant read | `@search_radius` | Uses a declared literal constant. |
| Expression | `(+ ?time 2.0)` | Evaluated during decomposition. |

Use decimal notation; scientific notation and hexadecimal literals are not
supported. A minus sign immediately followed by a digit is part of the number:
`-1`, `-1.0`, `-0.5` and `-123.456` are single literal values. They work wherever
a positive number works, including constants, lists, facts, assignments and task
or callterm arguments. Integers must fit in `[-2147483648, 2147483647]`; floats
must fit in the supported single-precision representation.

Arithmetic still uses an operator in parentheses: `(- 5 2)` subtracts, `(- 1.0)`
negates, and `(-- 1.5)` decrements. Separate an operator from a numeric operand:
`(- -2 3)` evaluates to `-5`, whereas `(-2 3)` is a list containing two numbers.
In a value position, `- 1` is invalid: write the literal `-1` or the expression
`(- 1)`. `-.`, `-abc` and `--1` are invalid numeric spellings; use `(-- 1)` for
decrement. Negative literals use prepared values rather than runtime negation;
source locations include the minus sign. The world-state file reader also accepts
negative numbers. Generated debugger labels preserve the written numeric spelling,
including trailing zeros and all decimal digits in scalar and list literals.

Strings do not interpret C-style backslash escapes; an embedded double quote
cannot be escaped with `\"`.

A position represented as `(1.0 2.0 3.0)` is a list, not a special vector literal.
The host can convert its own vector/entity types through
[type traits and converters](TYPE_CONVERSION.md).

## Facts and variables

A fact query matches the fact name and argument count. Literals and already-bound
arguments constrain the match; unbound variables receive values from a matching
row. Reusing a normal variable refers to the same binding within that scope.
Each method branch has its own local scope, and each axiom invocation has its own
parameter/local environment.

For example, `(visible_enemy ?enemy ?position)` can select an enemy, and a later
`(armed ?enemy)` restricts the result to that same enemy. Fact and axiom queries
can introduce bindings without an explicit assignment.

Use `?any_description` as a **discarded fact argument**. It matches a value but
does not bind a usable variable. Each such name may appear only once in its scope;
it cannot be consumed by a task, arithmetic expression or callterm. For example,
`(entity_presence ?entity ?any_factor ?any_friend)` keeps only `?entity`.

A variable being known to the compiler does not guarantee that every runtime
path binds it. Reads in value expressions require a bound value; a failed read
cannot be used as a successful result. A typo in a consumed variable name is a
validation error, not an implicitly created input.

**Do not delete facts while a decomposition is in progress.** The planner assumes
fact rows remain valid while enumerating alternatives. World-state changes or
external side effects made by callterms are not automatically undone by
backtracking. The host owns synchronization and the lifetime of the world state.

Generated fact queries enumerate rows in insertion order. Each query invocation
keeps its own next-row position across retries; nested queries and fresh axiom
invocations start independently. On a retry, the table size is refreshed, so rows
appended by a callterm remain visible. Do not remove, replace or reorder rows (or
change the world's fact registry) during decomposition. Appends are not rolled
back when a later condition fails.

## Methods, branches and tasks

A method signature contains its name and zero or more **input** parameters, all
named `?inp_...`. Mark every entry that client code must call with
`top_level_method`. These entries remain available even if no other method calls
them; unused internal methods may be removed from generated output.

A branch has exactly three parts: a name, a condition body, and a task list.
The condition body is `()` or a form headed by `and`, `or` or `alt`. Wrap even a
single fact or comparison in `(and ...)` at this level.

Branches are tried in source order, giving higher branches higher priority.
Tasks within a selected branch are processed in order. A compound task can expand
into more tasks; a primitive task becomes a plan entry. If decomposition fails,
the planner can try alternatives according to the configured backtracking mode.

Methods are overloaded by **name and argument count**, not by runtime types or
parameter names. `(move_to ?position)` and `(move_to ?from ?to)` can name two
different overloads. Duplicate declarations with the same signature are errors.
Definitions may appear below their callers. Recursive methods are supported, with
bounded generated call-frame storage; see [recursion capacity](GENERATED_RECURSION.md).

Application conventions such as `!begin_plan`, `!end_plan`, `!continue` and `!say`
are ordinary primitive task names. The planner does not assign them built-in
execution behavior. An empty task list can also be a successful plan.

## Axioms and parameter modes

An axiom is a named condition. It does not emit primitive tasks. Call it with
`#` inside another condition, including from another axiom.

<!-- example: Axioms.domain; complete -->
```lisp
(:domain Axioms top_level_domain
    (:method (behave ?inp_entity) top_level_method
        (select
            (and
                (#select_position ?inp_entity ?selected_position)
            )
            (
                (!move_to ?selected_position)
            )
        )
    )
    (:axiom (select_position ?inp_entity ?out_position)
        (and
            (= ?out_position (call get_entity_position ?inp_entity))
        )
    )
)
```

The host must register `get_entity_position`. Its returned value becomes
`?out_position`, then `?selected_position` in the caller when the axiom succeeds.
Notice that the axiom is declared **after** its caller.

| Parameter | Required on entry | Behavior |
| --- | --- | --- |
| `?inp_` | Bound input value | Read-only input; cannot be assigned with `=`. |
| `?out_` | Unbound caller variable | Body must produce an output; bound inputs fail before entering the body. |
| `?io_` | Bound value or unbound variable | Match a bound value, or produce values for an unbound caller. |

For IO matching, `(:axiom (candidate ?io_entity) (and (enemy ?io_entity)))`
can test a known entity or enumerate enemies for an unbound caller variable.
Ordinary matching does not overwrite a bound value.

An unused `?out_` or `?io_` parameter can be initialized with `=`. For `?io_`,
an already-bound slot makes the assignment fail **before its initializer runs**.
Use a fact/axiom match or comparison when you intend to test an existing IO value.
A second assignment on the same condition path is invalid.

All outputs must be available and consistent before success is published to the
caller. Failed candidates and backtracking restore earlier bindings. Axioms can
have multiple solutions. They also support name/arity overloads and qualified
calls such as `(#Base::candidate ?entity)`. Recursive axiom dependency cycles
are rejected. See [axiom contracts and backtracking](AXIOM_OVERLOADS.md).

## Conditions and backtracking

| Condition | Behavior |
| --- | --- |
| `(and a b ...)` | Require all children, evaluating in source order and searching combinations. |
| `(or a b ...)` | Commit to the first successful child; discard its remaining alternatives. |
| `(alt a b ...)` | Retain alternatives, including later children after earlier solutions are exhausted. |
| `(not a)` | Succeed if `a` has no solution; restore temporary bindings. |

Here `a` and `b` stand for complete conditions, not literal symbols. In particular,
`or` and `alt` are not interchangeable. Consider this branch condition fragment:

<!-- example: Alternatives.domain; condition -->
```lisp
(and
    (alt (preferred_target ?target) (other_target ?target))
    (allowed ?target)
)
```

If a preferred target fails `allowed`, alternatives can include other preferred
targets and then `other_target`. Replacing `alt` with `or` commits to the first
successful child's result; a later failure does not reopen that choice.

Bindings and tentative plan entries participate in rollback. Host effects do not:
`not` and backtracking can execute callterms whose effects remain even when their
logical result is discarded. A callterm may run again when another candidate
reaches it. Avoid relying on exactly one invocation per decomposition.

The translator's `--runtime-backtracking-support=enabled` option permits runtime
selection of supported fact/axiom backtracking modes. Without it, runtime mode
selection is disabled. This is separate from the capacity policy for storing
backtracking data and from `--call-frame-capacity`.

## Assignment

`(= ?destination expression)` declares and initializes a fresh local variable.
The destination must not have been declared or used earlier on that condition
path, except for an unused axiom output/IO parameter as described above.

This condition fragment reads an entity fact and declares two locals; `?inp_age`
is a method input parameter:

<!-- example: Assignment.domain; condition; parameters: ?inp_age -->
```lisp
(and
    (entity ?entity)
    (= ?position (call get_entity_position ?entity))
    (= ?next_age (++ ?inp_age))
    (== ?next_age (+ ?inp_age 1))
)
```

Initializers can be literals, constants, variables, arithmetic, callterms or
lists. Assigning a callterm's BOOL false result is valid; a failed or unbound
initializer is not. Bare `false` in source is a SYMBOL.
The previous implicit form `(?value (call get_value))` is invalid.

This example is **intentionally invalid**: assignment is not mutation.

<!-- example: Reassignment.domain; reject-condition -->
```lisp
(and
    (= ?value 1)
    (= ?value 2)
)
```

Independent method branches and independent `or`/`alt` arms may declare the same
local name. A later assignment cannot redeclare a name mentioned by any preceding
alternative. Self-reference in the initializer is also invalid.
See [assignment rules](ASSIGNMENT.md).

## Comparisons and arithmetic

Comparisons are conditions with **exactly two** value operands:

| Operator | Meaning |
| --- | --- |
| `==`, `!=` | Equal / not equal; never bind variables. |
| `<`, `<=`, `>`, `>=` | Numeric ordering. |

Integer/float comparisons allow mixed numeric types. Other equality comparisons
use atom equality; symbols and strings are distinct. Ordering requires numeric
operands. Unbound values cannot make a comparison succeed. Float equality is
exact: there is no built-in epsilon or `near` operator in this version.

Runtime BOOL true matches integer `1`, and BOOL false matches integer `0`, in either
direction. This applies to `==`/`!=`, fact arguments, axiom output matching and
list elements. No other integer, float, string or symbol is equivalent to a
boolean. Numeric int/float comparisons retain their existing behavior.

```lisp
(and
    (is_visible ?entity 1) // Also matches a fact written with C++ bool true.
    (= ?alive (call is_entity_alive ?entity))
    (== ?alive 1)
    (!= 0 false) // false is a SYMBOL, not a BOOL or INT.
    (call set_enabled 1) // Accepted when the binding parameter is C++ bool.
)
```

Atoms keep their original type: C++ `bool` writes and returns remain BOOL,
while `0`/`1` remain INT and bare `true`/`false` are SYMBOL. Binding an unbound variable copies that original type;
matching an already bound variable does not replace its value. The debugger
displays BOOL as `0`/`1`, including inside lists; symbols named `true`/`false`
keep their names. Visual Studio's natvis also displays `Bool: 0`/`Bool: 1`.
This formatting does not change the stored atom type. Integers still work
in arithmetic; BOOL operands do not gain arithmetic or ordering conversions.
An independent `(call predicate ...)` condition still requires a BOOL result:
returning INT `0` or `1` reports `NonBooleanConditionResult`. Compare such a result
explicitly, for example `(== (call integer_flag) 1)`.

Arithmetic forms produce values and can nest inside comparisons or other value
positions, including callterm, fact, axiom and task arguments:

| Expression | Operands | Meaning |
| --- | --- | --- |
| `(+ a b ...)` | At least two | Addition. |
| `(- a)` / `(- a b ...)` | At least one | Negation / subtraction from left to right. |
| `(* a b ...)` | At least two | Multiplication. |
| `(/ a b ...)` | At least two | Division from left to right. |
| `(% a b)` | Exactly two | Integer remainder. |
| `(++ a)` / `(-- a)` | Exactly one | Produce `a + 1` / `a - 1`; do not mutate `a`. |

All-integer arithmetic produces an integer; integer division truncates toward
zero. A float operand promotes the result to float. `%` accepts integers only.
Invalid operand types, division/remainder by zero and checked overflow fail
evaluation. Argument counts are checked during translation; value types are
checked at runtime.

<!-- example: Arithmetic.domain; condition -->
```lisp
(and
    (plan_duration ?plan_start_time ?plan_duration)
    (agent_time ?time)
    (<= ?time (+ ?plan_start_time ?plan_duration))
    (== (- 10 3 2) 5)
    (== (* 2 3) 6)
    (== (/ 7 2) 3)
    (== (% 7 2) 1)
    (== (-- (++ 10)) 10)
    (> 4 3)
    (>= 4 4)
    (!= patrol idle)
)
```

`=` means assignment, **not** equality. Logical forms and comparisons are
conditions; arithmetic and `(call ...)` can provide nested value expressions.
There is no general boolean-expression-to-value coercion.

## Callterms

`(call name arguments...)` invokes a binding registered by the host. It runs
**during decomposition**, unlike a primitive action emitted into the plan.
Arguments and nested value expressions are evaluated before the enclosing call.
Example condition fragment, with a position-producing host function and a
boolean visibility predicate:

<!-- example: Calls.domain; condition -->
```lisp
(and
    (agent ?entity ?from)
    (call is_visible ?entity)
    (= ?to (call get_entity_position ?entity))
    (< (call get_distance_from_to ?from ?to) 0.2)
    (< 0.0 (call get_distance_from_to ?from ?to))
)
```

A standalone callterm condition must return a **boolean** atom. A BOOL true result
succeeds; BOOL false is an ordinary condition failure. A valid non-boolean result reports
`NonBooleanConditionResult` through the configured error policy, then fails.
Bind or compare a position, integer, symbol or other non-boolean result explicitly.

```lisp
(call is_entity_alive ?entity)             // BOOL false fails this condition.
(= ?result (call is_entity_alive ?entity)) // BOOL false binds successfully.
```

These are alternative uses of the callterm, not successive conditions: the
second form continues when a bound result is returned, even BOOL false. A missing,
failed or unbound result fails the assignment. Returning INT `0`/`1`, or the
SYMBOL `false`/`true`, does not satisfy the standalone call's BOOL contract.

The host must configure `HTNCallTermErrorPolicy`. Missing registrations, bindings
or instances and incompatible arguments use the existing callback/reporting
mechanism. `FailSilently` suppresses reporting; it does not turn failure into
success. `Unset` is not a configured policy and retains the SDK's assertion
behavior. See [callterm diagnostics](MISSING_CALLTERMS.md).

Once the registry and instances are ready, the host can validate a generated
definition's callterm requirements before planning. That checks availability,
without executing the callterms or proving their future argument/return types.
Functions such as `get_entity_position` and `get_distance_from_to` are examples
of host bindings, not planner built-ins.

## Lists and list splitting

In a **value position**, an ordinary parenthesized sequence constructs a list.
Fully literal lists can use prepared storage. Lists with variables, arithmetic
or callterms are built during decomposition using current values.

This fragment assumes `?inp_entity` is a method input:

<!-- example: Lists.domain; condition; parameters: ?inp_entity -->
```lisp
(and
    (= ?static_query (query_extent_y (1.0 1.0 1.0)))
    (call ppr_start (find_closest_location_to_entity ?inp_entity))
    (= ?target (target ?inp_entity (call get_entity_position ?inp_entity)))
    (= ?request (action ?target (segment (++ 2)) (metadata current_time (call get_time))))
    (request_allowed ?request)
)
```

Here `ppr_start` must return a boolean because it is a standalone condition.
`(target ...)` is data inside a list value; it does not call a method. Context
matters: `(request_allowed ?request)` at the condition level is a fact query.

Elements evaluate left to right; failure stops construction. Every variable read
must already be bound. The resulting atom owns copies of its elements, including
nested lists. Backtracking reevaluates expressions when another candidate needs
them; it does not keep references into previous variable slots.

Lists work wherever an input value is accepted. A list containing variables is
**not a destructuring pattern** for binding fact or axiom outputs. Use the list
splitting conditions instead:

<!-- example: Split.domain; condition -->
```lisp
(and
    (= ?route (north east west))
    (split_list_front ?route ?first ?rest)
    (split_list_back ?route ?prefix ?last)
    (split_list ?route north (east west))
    (== ?first north)
    (== ?last west)
)
```

`split_list` aliases `split_list_front`. Front splitting returns **element, rest**;
back splitting returns **rest, element**. These take exactly three arguments,
bind fresh outputs or match existing values, and leave the original list intact.
A non-list or empty input fails. Splitting a single-element list produces an
empty remainder.

An empty list can exist at runtime, but the source literal `()` is currently
rejected in a value position. The empty **condition body** `()` remains valid.
The names `split_list`, `split_list_front` and `split_list_back` are reserved
built-ins in condition heads; they do not query same-named facts.
See [runtime list ownership and diagnostics](RUNTIME_LISTS.md).

## Constants, includes and overrides

Declare literal constants inside a domain and reference them with `@`:

<!-- example: Constants.domain; declarations -->
```lisp
(:constants MovementSettings
    (search_radius 10.0)
    (directions (north east south west))
)
(:method (search) top_level_method
    (ready
        (and (search_enabled))
        ((!search_area @search_radius @directions))
    )
)
```

The block name `MovementSettings` groups declarations; the value reference is
`@search_radius`, not `@MovementSettings::search_radius`. Initializers must be
literals, including fully literal lists, not runtime expressions.

`true` and `false` are ordinary names and can be declared as numeric constants:

```lisp
(:constants
    (true 1)
    (false 0)
)
```

`(== 0 @false)` succeeds, while `(== 0 false)` fails as a condition because
`false` is a symbol. `@true`/`@false` are user-defined constants, not built-ins;
they must be declared or included. A definition `(false false)` is also legal,
but its value is the symbol `false`, not a boolean or zero.

Migration from SDK 2.3.0: replace former boolean literals with `1`/`0` or these
explicit constants when boolean matching/conversion is intended. Apply the
same migration to `.worldstate` files. Regenerate and rebuild domain C. The
runtime BOOL atom type and native C++ `bool` bindings remain supported.

Includes are resolved relative to the including file. They link declarations
from other domains. Put `(:include "BaseMovement.domain")` before the root domain
declaration, as in this two-file example.

**`BaseMovement.domain`:**

<!-- example: BaseMovement.domain; include -->
```lisp
(:domain BaseMovement base
    (:constants MovementSettings base
        (move_speed 1.0)
    )
    (:method (move_to ?inp_position) base
        (ready () ((!walk ?inp_position @move_speed)))
    )
    (:method (move_to ?inp_from ?inp_to) base
        (ready () ((!walk_from_to ?inp_from ?inp_to)))
    )
)
```

**`SpecialMovement.domain`:**

<!-- example: SpecialMovement.domain; complete -->
```lisp
(:include "BaseMovement.domain")
(:domain SpecialMovement top_level_domain
    (:constants MovementSettings overrides BaseMovement
        (move_speed 2.0)
    )
    (:method (move_to ?inp_position) overrides BaseMovement
        (ready () ((!run_to ?inp_position @move_speed)))
    )
    (:method (behave) top_level_method
        (ready ()
            (
                (move_to (1.0 0.0 2.0))
                (BaseMovement::move_to (1.0 0.0 2.0))
                (move_to (0.0 0.0 0.0) (1.0 0.0 2.0))
            )
        )
    )
)
```

The unqualified one-argument call selects the override. The qualified call selects
the original method body. The two-argument overload remains inherited. Constants
use their effective linked value: selecting a base method body does not restore
the base constant value. Here both one-argument bodies see `@move_speed` as `2.0`.

An override must name an existing matching declaration marked `base` in a base
domain. Axioms use the same signature rules and `overrides BaseMovement` marker.
`::` qualifies methods and axioms by **domain name**, not filename. It is not a
general namespace for facts, callterms or built-ins.
See [method overloads](METHOD_OVERLOADS.md) and [axiom overloads](AXIOM_OVERLOADS.md).

## Deferred decomposition

Use `&` on a compound task to emit a deferred call instead of expanding it now:

<!-- example: Deferred.domain; complete -->
```lisp
(:domain Deferred top_level_domain
    (:method (behave ?inp_entity) top_level_method
        (ready
            (and (= ?position (call get_entity_position ?inp_entity)))
            (
                (!move_to ?position)
                (&after_arrival ?inp_entity)
            )
        )
    )
    (:method (after_arrival ?inp_entity)
        (still_visible
            (and (visible ?inp_entity))
            ((!say "I can still see the target."))
        )
        (lost_target () ((!search_for ?inp_entity)))
    )
)
```

The host executes `!move_to`, then asks the planner to decompose the deferred
`after_arrival` step. That body can inspect the world state as it exists **then**.

Deferred arguments are evaluated and captured when the original plan step is
created. For example, a `(call get_time)` inside a deferred argument captures the
time now; a call inside the deferred method body runs during the later
decomposition. The compiler preserves deferred targets and their dependencies
even when they are not public top-level entries.

The host owns scheduling, cancellation, plan lifetimes and the later decomposition
request. `&` does not provide a timer or run a background job. `#` is exclusively
for axiom calls in conditions and is invalid in a task list.

## Validation and common mistakes

`HTNTranslator --check` loads, links and validates the domain and its includes.
It checks syntax, signatures, references, assignment rules and parameter modes.
It cannot prove the runtime types of world-state or callterm values. Normal
translation additionally lowers the domain to IR and generates C.

| Symptom | Check |
| --- | --- |
| Assignment destination was already used | Use a fresh local, or a valid unused axiom output. Use `==` to compare. |
| Unknown variable | Check spelling and whether a parameter or condition introduces it. |
| `?out_` call fails before entering its body | Pass an unbound caller variable; use `?io_` for matching bound values. |
| Call returns a position but its condition fails | Assign/compare the result; standalone calls require boolean results. |
| Compound/axiom call cannot resolve | Check name, arity and include declarations; declaration order is unrestricted. |
| Axiom prefix in task list | Use `#` in conditions, a plain method name for immediate tasks, `&` for deferred tasks. |
| A list element is unbound | Runtime lists consume current bindings; they do not introduce them. |
| A failed alternative left an external effect | Callterm/host effects are not rolled back with planner bindings. |
| No debugger tree after generation | Keep the default `--instrumentation=full`; `none` omits debugger/profiling output. |

Diagnostics retain domain file, line and column where available. Runtime registry
errors use the host's callterm error policy. List-evaluation failures also provide
the generated execution diagnostic described in [runtime lists](RUNTIME_LISTS.md).
The host is responsible for displaying these reports.

The positive Lisp examples in this guide were checked and translated with the
2.1.0 candidate translator in both `full` and `none` instrumentation modes.
Fragments were placed in suitable method/domain wrappers; the deliberate
reassignment example was checked for rejection. This verifies compilation, not
the behavior of example host bindings such as `get_entity_position`.

## Where to go next

- [Integration use cases](USE_CASES.md): separate reasoning/behavior/active-plan
  validation, single-entry replanning, squad behavior and AI Director patterns.
- [Type conversion](TYPE_CONVERSION.md): connect application types to atoms and
  write world-state facts.
- [Callterm configuration and errors](MISSING_CALLTERMS.md): registration,
  initialization validation and runtime reporting.
- [Generated instrumentation and size](GENERATED_INSTRUMENTATION.md): choose
  debugger/profiling emission and inspect source-size statistics.
- [SDK variants](SDK_VARIANTS.md): choose the CRT, configuration and instrumentation.
- [2.4.0 release notes](RELEASE_2_4_0.md): compatibility and migration.
