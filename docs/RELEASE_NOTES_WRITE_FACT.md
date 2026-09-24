# World-state fact conversion — pending release

## Changes

`WriteFact(fact, arguments...)` delegates to
`WriteFactWithContext(nullptr, fact, arguments...)`. Both APIs convert arguments
through `HTNTryToAtom(clientContext, value, atom)`, supporting native HTN values
and custom types registered with `HTNTypeTraits` and `HTNTypeConverter`.

Use `WriteFactWithContext(clientContext, fact, arguments...)` when a converter
needs client services. The context is borrowed for the call and is not stored.

Arguments are converted into owned temporaries before fact storage is modified.
If a conversion fails or produces an unbound atom, the call returns `false`,
releases the temporaries and leaves the world state unchanged. Conversion stops
at the first failure. Side effects performed by custom converters on client
services are outside this transaction.

## Compatibility and migration

- Existing `WriteFact` call syntax, native value types, zero-argument facts and
  duplicate-row behavior remain supported.
- Native atoms and lists are copied even when passed with `std::move`. Their
  original values remain usable after successful and failed writes. This
  replaces the previous ownership-consuming behavior for rvalues.
- Clients retain ownership of raw C atoms and lists and must destroy them.
  Wrap owning raw temporaries in `HTNAtomOwner` or `HTNAtomListOwner` before
  passing them; do not rely on `WriteFact` to take ownership.
- Unbound arguments are rejected rather than stored. Check the returned `bool`.
- No C ABI, planner ABI or `HTNAtom` layout changes are introduced by this update.
  Rebuild C++ consumers to use the new header implementation.

These behavior changes must be considered when choosing the release version;
unchanged ABI alone does not guarantee drop-in compatibility.
See [type conversion documentation](TYPE_CONVERSION.md#writing-world-state-facts)
for the complete contract.

## Development validation

Before porting to the release branch, validation on `main` passed:

- 277 Debug tests, excluding the slow `Recursive100Entities` case.
- Six fact-writing tests in Profile with atom diagnostics enabled, covering
  native and custom types, client context, failed conversions, failure in a
  middle argument and preservation of input atoms.
- All eight Windows SDK variants and 24 external consumer executions, including
  rejection of incompatible CRT settings and unknown variants.

This records development validation; the release snapshot must still pass its
own release checklist before publication.
