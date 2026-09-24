# HTN 2.0.2 — RuntimeBridge correction

## Critical fix and migration

Do not use HTN 2.0.1 for domain DLL integrations. Its translator can emit
`HTNAtom_SetInt` and `HTNAtom_SetFloat`, but its RuntimeBridge does not export
them. Numeric expressions and affected generated numeric values can therefore
produce LNK2019 when linking a generated domain DLL only to HTNRuntimeBridge.
Linking the framework into that DLL is not the fix.

2.0.2 adds both functions to the canonical bridge table. RuntimeBridge ABI
revision increases from 6 to **7** (plain `0x48580007`, decomposition `0x48590007`,
profiling `0x485A0007`, both `0x485B0007`). Rebuild hosts, bridge and domain modules
together; old binding tables are rejected. Planner and HTNAtom layouts are unchanged.

## Regression and release gates

- A generated domain DLL exercises numeric setters, every arithmetic operator,
  bool/symbol/string/list values, variables, comparisons, facts, axioms, callterms,
  normal and deferred methods, failed branches and fact/axiom backtracking.
- Negative values use unary `(- 3)`; signed literal tokens and literal empty lists
  are not currently accepted. An empty list is returned by a callterm instead.
  `++`/`--` are pure prefix expressions; postfix/mutating operators do not exist.
- The host executes the loaded definition and a deferred target, checks results
  and decomposition events in instrumented variants, destroys storage/results and
  unloads modules. DLL search excludes PATH; dependencies come from the package
  DLL directory or Windows system directories.
- Every canonical bridge entry is referenced by an import-library link probe and
  checked with GetProcAddress. A separate check compares undefined HTN symbols in
  compiled objects to both DLL exports and import-library members, and explicitly
  requires SetInt/SetFloat references in the generated coverage object.
- Release packaging rebuilds all eight variants and records binary hashes with
  a build ID/version. Packaging rejects changed/unrecorded binaries, including
  the translator, and verifies generated project CRT/ABI configuration.
- ValidatePackage requires a new build directory, verifies package checksums and
  runs Core, Integration, loaded DLL and object/export checks for every variant.
  Manifest ABI values are compared against the compiled host headers.

The eight SDK variants include decomposition instrumentation, not execution
profiling. Profiling is a separate development configuration; these SDK results
must not be described as execution-profiling coverage.

The previous consumer only generated simple string tasks and constant comparisons;
it did not force numeric arithmetic helpers into the DLL. Framework-linked tests
resolved the functions directly and therefore could not reveal the missing exports.
The new domain reproduces both LNK2019 errors against the original 2.0.1 package.

See [the complete function inventory](RUNTIME_BRIDGE_AUDIT.md).
