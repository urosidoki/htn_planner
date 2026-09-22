# HTN Planner 2.0.0 — release candidate

Publication is pending the release checklist. Windows x64 / MSVC v143 remains
the supported SDK target. Linux support is not included in this release.

## Features and fixes

- Method and axiom overloads resolve by name and argument count, including
  includes, qualified calls, recursion and deferred method calls.
- Arithmetic arguments are evaluated before compound tasks, primitive tasks,
  callterms and axioms. Backend diagnostics retain source provenance.
- Nested axiom alternatives preserve output and IO bindings across backtracking.
  Already-bound outputs unify with candidates instead of being overwritten.
  Resuming alternatives does not replay earlier host effects.
- Deferred methods use `&`; `#` is reserved for axiom conditions. Incorrect
  prefixes in task lists produce a located diagnostic.
- Client services and missing-callterm handling are configured on the execution
  context. Reports distinguish unregistered names, missing bindings and missing
  daemon instances.
- Lexer/parser/include errors carry typed diagnostics and source locations.
- [Use cases](USE_CASES.md) explain integration patterns and client responsibilities.
- An isolated ClangCL smoke build prepares future portability work. It is not
  certification of the complete toolset or Linux/GCC compatibility.

## Migration from 1.1.0

This update contains source, domain syntax and binary incompatibilities. It is
not a drop-in SDK replacement. The major version increment reflects these
incompatibilities. Review this migration before upgrading.

1. Change deferred calls in task lists from `#method` to `&method`. Keep `#axiom`
   in conditions. Do not perform a global replacement of `#`.
2. Add `void* inClientContext` as the first parameter of custom converter
   `FromAtom` and `ToAtom` functions. Forward it to nested conversions. See
   [type conversions](TYPE_CONVERSION.md).
3. Configure `Unit.GetExecutionContext().ClientContext`, `MissingCallTermPolicy`
   and, for `Report`, `MissingCallTermCallback`. Core clients use the corresponding
   fields on `HTNGeneratedPlannerContext`. `Unset` asserts if a missing callterm
   is reached; it is not a configured fallback. See [missing callterms](MISSING_CALLTERMS.md).
4. Adapt direct registry calls to supply an execution context. Bindings retain
   registry/daemon information; runtime options belong to each execution.
5. Revalidate domains that relied on overwriting a bound `?out_` value. A different
   candidate now fails/unifies normally and may cause backtracking.
6. Regenerate all domain C and rebuild the host, SDK libraries and domain modules
   together. Do not mix old generated objects or bridge DLLs with this SDK.

Generated planner ABI identifiers are `0x48540004` (plain), `0x48550005`
(decomposition), `0x48560004` (profiling) and `0x48570005` (both).
The runtime bridge revision is 6; use the ABI identifiers from the selected
package variant's manifest and validate definitions before adoption.

## Boundaries

Primitive actions, cancellation, file watching, compilation, module loading and
safe hot reload are client responsibilities. The repository provides integration
examples. Callterm side effects are not rolled back when planning fails.

The public source and SDK contain only the generated planner path. Development
history from the private repository must not be pushed into the public repository;
export the reviewed source snapshot into its existing independent history.
