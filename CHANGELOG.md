# Changelog

All notable changes to HTN Planner are documented in this file. Releases follow
[Semantic Versioning](https://semver.org/).

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
