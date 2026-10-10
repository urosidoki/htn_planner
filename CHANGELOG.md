# Changelog

All notable changes to HTN Planner are documented in this file. Compatibility
exceptions are called out explicitly in each release.

## 2.4.0 - Unreleased

- Add optional per-instance allocators for owning atom lists and backtracking
  overflow, with no global allocator selection. Null preserves default policies.
- Add `HTNSafePooledAtomListAllocator`: a fixed pool with a borrowed fallback,
  defaulting to new/delete. Every node is released through its originating allocator.
- Release custom backtracking containers before every decomposition returns,
  including failures. The client exclusively owns scratch reset/marker restoration;
  retained plan/list values have a separate allocator lifetime.
- Expose backtracking bytes, peak usage, allocation counts and failures through
  execution info, without requiring profiling or debugger instrumentation.
- Cover allocation failure, cleanup, backtracking, deferred calls, retained plans,
  independent concurrent instances and previous-domain ABI rejection.

- Bare `true` and `false` are ordinary symbols in domains and `.worldstate` files.
  Use `0`/`1` or explicitly declared `@true`/`@false` constants for boolean data.
  Migrate old boolean literals and regenerate domains before upgrading.
- BOOL values match INT `0`/`1` symmetrically in facts, equality, lists and axiom
  bindings. Typed `bool` arguments accept these two integers; other integers,
  floats and symbols are rejected. Stored atom types do not change.
- Standalone callterm conditions still require BOOL. A BOOL false result fails
  the condition; assigning it binds the result and continues. `(== 0 false)` fails.
- Debugger, atom text and Visual Studio natvis display BOOL values as `0`/`1`,
  including nested lists. Symbols named `true`/`false` retain their spelling.

- Retain unregistered `WriteFact` / `WriteFactWithContext` rows for inspection
  through `GetUnregisteredFacts()` when `HTN_DEBUG_DECOMPOSITION` is enabled.
  Writes still return `false`; planner queries and `GetFacts()` never see these rows.
- Reuse transactional argument conversion, including client-context converters.
  `ClearFact` and `RemoveAllFacts` clear the captured rows alongside normal facts.
- Cover zero/nonzero arities, query isolation, failed conversion, registry changes,
  cleanup and instrumented SDK consumers. Plain builds retain their existing behavior.
- **Compatibility:** the generated context/execution-info and runtime bridge ABIs
  change; regenerate domains and rebuild all hosts and modules together. Older
  domains are rejected. The instrumented C++ `HTNWorldState` layout also changes.
  Atom/list layouts are unchanged; no new SDK variants are introduced.
- See [2.4.0 release notes and validation status](docs/RELEASE_2_4_0.md).

## 2.3.0 - 2026-10-04

- Accept native negative integer and float literals in every numeric value context,
  including static/runtime lists, tasks, callterms, axioms and world-state files.
- Preserve subtraction, unary negation and decrement syntax; generate negative atoms
  directly and emit `INT32_MIN` portably for the minimum signed 32-bit integer.
- Preserve numeric spelling and source locations in generated debugger labels,
  including full decimal precision and trailing zeros inside static lists.
- Add located diagnostics for malformed signed values and overflow, plus lexer,
  frontend, AST/IR, generated C, runtime, backtracking and deferred regressions.
- **Compatibility:** C runtime API/ABI, RuntimeBridge ABI and atom layout are unchanged.
  Rebuild tools using the C++ compiler AST; translate domains that adopt the new syntax.
- See [2.3.0 release notes and validation](docs/RELEASE_2_3_0.md).

## 2.2.0 - 2026-10-03

- Include `debug/HTN.natvis` in the Windows SDK, propagate it to CMake consumers,
  and validate its type definitions, checksum and Visual Studio project integration.
  No C API, ABI or atom-layout change.

- Add native Ubuntu 24.04 x86_64 builds with GCC 14 and Clang 18/libstdc++ for
  the generated runtime, translator, integration, visual demos/debugger, language
  server, benchmarks and tests. HTNEditor remains Windows-only and experimental.
- Add Linux hot reload compilation/loading through the demo's existing client
  orchestration, plus automated pipeline and language-server protocol checks.
- Add four Linux SDK variants, CMake integration, archive/SHA-256 packaging and
  external consumer validation with both compilers, including incompatible-ABI checks.
- Document [Linux setup, builds and SDK consumption](docs/LINUX.md).
- **Compatibility:** no C runtime ABI, RuntimeBridge ABI or atom-layout change.
  Other distributions, architectures, libc++ and other Unix platforms remain unverified.
- See [2.2.0 release notes and validation status](docs/RELEASE_2_2_0.md).

## 2.1.0 - 2026-10-01

- Add a [domain language tutorial and reference](docs/DOMAIN_LANGUAGE.md),
  also included in the SDK, with translator-checked examples.
- Build owned runtime lists from variables, arithmetic, callterms and nested
  lists wherever a value is accepted. Preserve static literal storage,
  evaluation order, backtracking, deferred capture and failure cleanup.
- Report `NonBooleanConditionResult` through the existing callterm error policy
  when an independent callterm condition returns a non-boolean value. No truthiness
  conversions are added; boolean false remains an ordinary condition failure.
- Add `--instrumentation=full|none` (default full) and `--code-stats` to the
  translator. None omits optional generated debugger/profiling code while
  preserving validation, ownership, error reporting and the selected C ABI.
- Share implementations of qualified method/axiom aliases and omit unreachable
  generated implementations. Preserve every top-level method and deferred entry,
  exact name/arity dispatch, overrides and debugger identities.
- **Compatibility:** no C runtime ABI, RuntimeBridge ABI or atom-layout change
  from 2.0.4. Rebuild clients using the extended C++ compiler interfaces;
  regenerate/recompile domains to gain the new behavior and size reductions.
  See [2.1.0 release notes](docs/RELEASE_2_1_0.md).

## 2.0.4 - 2026-09-30

**Breaking compatibility despite the patch version:** migrate the callterm error
API, regenerate domain C sources and rebuild the host, libraries and domain DLLs.
This replaces the earlier local 2.0.4 candidate; do not reuse its binaries.

- Replace native method/task recursion with a generated iterative dispatcher and
  fixed call-frame storage. `--call-frame-capacity=N` selects the capacity
  (default 8192); exhaustion reports the configured limit and how to increase it.
- Expose `get_execution_info` for configured capacity, peak frames, frame size
  and the last error. Preserve rollback and storage reuse after exhaustion.
- Unify missing-callterm and argument/return conversion failures under
  `HTNCallTermErrorPolicy`, with one client report per failed invocation.
- Support explicit assignment to unused axiom `?out_` and `?io_` parameters,
  including literal, arithmetic and callterm initializers. Preserve output
  propagation, multiple solutions and rollback during backtracking.
- Require pure `?out_` arguments to arrive unbound. Assignment to a bound IO
  slot fails before its initializer runs. Reject unknown variables in axiom
  expressions with file, line and column diagnostics.
- Update generated debugger/demo reporting and add recursion, capacity recovery,
  nested-callterm and axiom/IO regression coverage.
- Preserve original expressions and parentheses in the generated debugger; hide
  compiler temporaries from the tree and watch while retaining outcomes and retries.
  Instrumented domains and their hosts must be regenerated/rebuilt together.
- **Compatibility:** planner descriptor and RuntimeBridge ABI revisions change;
  existing status values and `HTNAtom` layout remain unchanged. See
  [2.0.4 migration notes](docs/RELEASE_2_0_4.md).

## 2.0.3 - 2026-09-29

**Breaking compatibility despite the patch version:** migrate assignments and
regenerate/rebuild all domain modules and the host.

### Callterm evaluation and initialization validation

- Evaluate nested callterms in comparisons and arithmetic instead of treating
  their names as literal values. Preserve missing-call policies and exact source
  locations. See [nested-call regression notes](docs/RELEASE_NOTES_NESTED_CALLS.md).
- Add `HTNCallTermRegistry::ValidateGeneratedCallTerms` for explicit initialization
  checks of missing registrations, bindings and instances without invoking calls.
- Generated definitions expose call-site requirements in plain and instrumented
  builds. Each original call site is reported once, including linked sources.
- **Compatibility:** the planner descriptor ABI changes. Rebuild the host and
  regenerate/recompile domain modules with matching headers; atom layout and
  RuntimeBridge function signatures are unchanged. See
  [initialization validation](docs/MISSING_CALLTERMS.md#initialization-validation-unreleased).

### Explicit variable declarations (breaking domain syntax change)

- Declare and initialize fresh local variables with `(= ?value expression)`.
- Reject implicit call-result binding and destinations already declared or used.
- Support literal, variable, arithmetic and callterm initializers with backtracking.
- Update generated debugger assignment display and compiler regression tests.
- Migrate domains and regenerate their C source; C function signatures and atom
  layout are unchanged. See [migration instructions](docs/ASSIGNMENT.md).

## 2.0.2 - 2026-09-25

- Fix missing `HTNAtom_SetInt` and `HTNAtom_SetFloat` exports in RuntimeBridge.
- **Rebuild required:** RuntimeBridge ABI revision is now 7. Rebuild the host,
  bridge and generated domain modules together. Planner and atom layouts are unchanged.
- Add generated DLL arithmetic/backtracking coverage and object/export checks
  across all eight SDK variants; verify build provenance and complete package checksums.
- Validate hot reload definitions before callbacks and test malformed fact-name rollback.
- Extend missing-callterm policy checks to Release consumers and enforce SDK source isolation.
- See [release notes and migration](docs/RELEASE_2_0_2.md).

## 2.0.1 - 2026-09-24

### World-state fact conversion

- `WriteFact` now converts arguments through `HTNTryToAtom`, supporting custom
  types registered with `HTNTypeTraits` and `HTNTypeConverter`.
- Added `WriteFactWithContext(clientContext, fact, arguments...)`; the context
  is borrowed for conversion and is not stored in the world state.
- Failed conversions or unbound results return `false` without modifying fact
  storage or inserting partial rows.
- Native atoms and lists are copied, even when passed with `std::move`.
- **Compatibility:** rvalues are no longer consumed and unbound arguments are
  rejected. The C ABI and atom layout are unchanged. Rebuild C++ consumers and
  review the [ownership migration notes](docs/RELEASE_NOTES_WRITE_FACT.md).

## 2.0.0 - 2026-09-23

- Method and axiom overloads by arity.
- Arithmetic arguments across task, callterm and axiom calls.
- Nested axiom backtracking and bound-output unification fixes.
- Deferred calls use `&`; `#` is reserved for axioms.
- Execution-owned client context, context-aware converters and missing-callterm policies.
- Located frontend diagnostics and public integration use cases.
- **Migration required:** domain syntax, custom converters and runtime ABI changed.
  See [release notes](docs/RELEASE_2_0_0.md).

## 1.1.0 - 2026-09-20

### Added

- Builtin integer and floating-point arithmetic expressions using `+`, `-`, `*`,
  `/` and `%`.
- Unary `++` and `--` arithmetic expressions.
- Numeric expressions can be nested and used as operands in builtin comparisons.
- Numeric expressions demo domain and generated planner tests.

### Validated

- Verified nested and mixed arithmetic through the generated execution path.
- Verified runtime failure for division by zero and invalid operand types.

## 1.0.0 - 2026-09-19

### Added

- Ahead-of-time domain translation through a compiler-owned lexer, AST and IR.
- Native generated planner runtime with configurable backtracking.
- Generated execution debugger with source ranges and bound values.
- Optional C++ integration layer and runtime bridge for dynamic domain modules.
- Visual SDL and ImGui demo, editor, language server and VS Code extension.
- Eight Windows x64 SDK variants covering static and dynamic CRT linkage and
  optional execution instrumentation.
- External SDK consumer validation and hot reload examples.

### Changed

- Public runtime and SDK now use the generated execution architecture throughout.
- SDK documentation and release validation describe the complete variant matrix.

### Validated

- Integrated the packaged SDK into an external game engine using engine-owned
  planner hooks, planning units, world-state updates and callterm daemons.
- Validated dynamic domain compilation, runtime bridge loading and repeated hot
  reload while the engine was running under Visual Studio.
- Validated all eight SDK variants through isolated external consumers.
