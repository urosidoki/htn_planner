# HTN Planner 2.4.0

Status: **Release preparation; not published.**
Distribution targets: Windows x86_64 and Ubuntu 24.04 x86_64.

## Per-instance list and backtracking allocators

`HTNGeneratedPlannerContext` now accepts two optional borrowed services:

| Member | Purpose | Required lifetime |
| --- | --- | --- |
| `list_allocator` | Owning list nodes, runtime list expressions, copies, list-operation results and returned plans | Until all referencing values and execution storage are destroyed, including plans retained after decomposition |
| `backtracking_allocator` | Overflow containers for pending continuations and variable snapshots | The synchronous decomposition call; all custom blocks are released before return |

HTNIntegration exposes the same services as `ListAllocator` and
`BacktrackingAllocator` in `HTNPlannerExecutionContext`. Different planners may
use independent allocators concurrently; shared allocators need client coordination.
Null retains the existing policies, including reusable default backtracking blocks.
Prepared data and fixed execution scratch remain in their existing storage.

- `HTNSafePooledAtomListAllocator` falls back to a borrowed allocator when its fixed
  pool is exhausted, defaulting to `HTNNewDeleteAtomListAllocator`. Nodes always
  return to their allocator of origin; moves preserve ownership.
- A backtracking arena may use no-op deallocation. **Only the client resets scratch
  or restores its marker**, after decomposition returns. Returned plans remain
  independently owned. Never reset a list allocator while a referencing value lives.
- Allocation failures return `HTN_DECOMPOSITION_OUT_OF_MEMORY` and clean up partial
  results. Backtracking allocation has no implicit heap fallback; callbacks may
  implement one explicitly. Callbacks must not throw.
- `execution_info.backtracking_allocations` reports current/peak/requested bytes,
  largest request, alignment and successful/failed allocation counts. Statistics
  reset per call and remain available afterwards, without profiling flags.
  `HTNPlanningUnit::GetGeneratedExecutionInfo()` exposes them to integration clients.
  Counts describe requested container bytes; the arena's own high-water mark also
  includes allocator padding and bookkeeping.

See [list allocation and ownership](INSTANCE_LIST_ALLOCATOR.md) and
[backtracking scratch allocation](BACKTRACKING_ALLOCATOR.md) for examples, failure
behavior, deferred calls and detailed lifetime contracts.

## Release-wide ABI migration

**This release changes the generated C ABI and runtime bridge ABI.** Regenerate
every domain and rebuild the host, libraries, bridge and domain modules together.
Do not mix headers/libraries from an earlier 2.4.0 candidate. Previous generated
definitions and bridge tables are rejected before execution.

- Planner ABI revision: **9** for plain/profiling, **11** with decomposition debugging.
- Runtime bridge ABI revision: **10** in all variants.
- `HTNAtom`/list representation and existing status values are unchanged.
- Zero-initialize contexts; leave both allocator pointers null for default behavior.

The feature-specific compatibility notes below describe the boolean and
unregistered-fact changes alone; they do not supersede this release-wide migration.

## Unregistered world-state facts in instrumented builds

When `HTN_DEBUG_DECOMPOSITION` is defined, `WriteFact` and
`WriteFactWithContext` retain valid writes whose symbols are absent from the
world state's fact registry. This helps clients inspect daemon output that a
compiled domain does not reference, such as `(health 60 60)` or `(hit_reaction)`.

```cpp
#ifdef HTN_DEBUG_DECOMPOSITION
// Both calls return false when these symbols are not registered.
worldState.WriteFact(HtnSymbol::sGetSymbol("health"), 60, 60);
worldState.WriteFact(HtnSymbol::sGetSymbol("hit_reaction"));
const HTNFacts& unusedFacts = worldState.GetUnregisteredFacts();
// Inspect each symbol's tables, indexed by argument count, just like GetFacts().
#endif
```

- These writes still return `false`. `GetFacts()`, fact queries and generated
  planner cursors never expose the inspection rows. Registering a symbol later
  does not promote previously captured rows into planner facts.
- Arguments use the existing `HTNTryToAtom(clientContext, value, atom)` path.
  All conversions must succeed and produce bound atoms before a row is stored.
  Failed conversions leave both stores unchanged. Values are owned copies;
  the client context is borrowed only for conversion.
- Unregistered writes now execute custom converters in instrumented builds.
  Any converter side effects on client services remain the client's responsibility.
- Zero-argument facts and duplicate rows are retained. A null fact symbol is
  rejected without storage. Without a registry, all non-null fact symbols are
  unregistered.
- `ClearFact(symbol, arity)` clears matching inspection rows too, retaining its
  existing return contract (`false` for an unregistered symbol). `RemoveAllFacts()`
  clears all rows in both stores. Empty table containers are retained for reuse.
- Clients can use the new getter in their debugger UI. This change provides the
  inspection data; it does not add a panel to the existing visual debugger.

## Boolean and binary integer compatibility

**Domain language change:** bare `true` and `false` are now ordinary SYMBOL
values in domains and `.worldstate` files. They are not reserved
words or boolean literals. `(== 0 false)` is valid but fails as a condition.
Replace old boolean literals with `0`/`1`, or declare
`(:constants (true 1) (false 0))` and use `@true`/`@false`. The constants are
ordinary user declarations, not predefined aliases. Regenerate existing domains
after migrating their source; previously generated C retains the old literal
types until rebuilt.

- Domain values `0`/`1` can match BOOL `false`/`true` in facts, equality,
  axiom outputs and lists. Matching is symmetric and never changes stored types.
  This behavior is shared by plain and instrumented builds.
- C++ `bool` callterm parameters and `HTNTryParseType` accept BOOL or INT `0`/`1`.
  Other integers and all floats remain invalid boolean arguments. Raw bindings
  receive original atoms and should use the converter to read boolean values.
- Standalone callterm conditions still require a BOOL return value. An INT
  result, including `0` or `1`, continues to report `NonBooleanConditionResult`.
  A callterm's BOOL false result fails a standalone condition but is a valid
  assigned value: `(= ?result (call is_entity_alive ?entity))` binds and continues.
  Missing, failed or unbound results still fail assignment.
- Arithmetic and numeric ordering retain their existing INT/FLOAT behavior.
  Atom text formatting, the planner debugger and Visual Studio natvis display
  BOOL as `0`/`1`, also inside lists, without changing the stored type. Symbols
  named `true`/`false` keep their names.
- The boolean changes alone preserve atom layout and the C ABI. Regenerate
  domains for the new literal semantics and constant comparison rules. The
  allocator changes in this release additionally require the ABI migration
  above; older modules cannot be reused with this SDK.
- This expands matching: facts previously distinguished only by BOOL versus
  INT `0`/`1` may now both match a query. Duplicate rows remain independent
  alternatives during backtracking. Code that needs the actual type can use
  `HTNAtom_GetType`/`HTNAtomIsType`.

## Unregistered-fact compatibility

- The feature and getter exist only under `HTN_DEBUG_DECOMPOSITION`, including
  Release configurations that select an instrumented SDK variant.
- Plain variants keep their previous world-state layout, early rejection of
  unregistered writes and planner behavior. No build variants are added.
- The instrumented C++ `HTNWorldState` layout changes. Rebuild consumers and
  use matching 2.4.0 headers and libraries throughout the client. Do not mix an
  older instrumented library with the new headers.
- The generated domain format, C runtime ABI and `HTNAtom` layout are unchanged.
  This feature alone does not require domain regeneration.

The Windows SDK is built and validated with `BuildAndValidateSDK.bat`, which
reads `VERSION` and packages `dist/HTNSDK-2.4.0-windows-x86_64.zip` and its SHA-256
file. Its extracted SDK directory is suitable for an engine's `thirdparty/HTN/2.4.0`.

## Package validation and release preparation

Build the Windows archive with `BuildAndValidateSDK.bat` and the Linux archive
with `bash BuildAndValidateSDK.sh`, following the Linux dependencies guide.
Both commands read `VERSION` (2.4.0), rebuild SDK variants and validate external
consumers against the extracted archive. Windows validates eight variants;
Linux validates four variants with GCC 14 and Clang 18 consumers.

The source repository records this candidate's completed checks and local log
paths in `docs/VALIDATION_2_4_0.md`. Those logs are local build artifacts and are
not shipped in the SDK. A previous 2.4.0 candidate from before the allocator
changes must be replaced with a rebuilt package. Existing archives in `dist/`
are not updated by copying source files.

Engine integration with the final candidate, review, tagging and publication
remain maintainer release steps. Follow `docs/RELEASE_CHECKLIST.md` in the source
repository. No tag or publication is performed by the SDK build commands.
