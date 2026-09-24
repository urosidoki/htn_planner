# Client context in type conversions

Consumers specialize `HTNTypeTraits<T>` and `HTNTypeConverter<T>` for engine types.
Converters now receive the borrowed client context as their first argument:

```cpp
static bool FromAtom(void* inClientContext, const HTNAtom& inAtom, EntityRef& outValue);
static bool ToAtom(void* inClientContext, const EntityRef& inValue, HTNAtom& outAtom);
```

For example, cast `inClientContext` to your own services structure, read an integer
EntityId from the atom, and resolve it through your entity manager. Return `false`
if the manager is unavailable, the ID is stale, or the entity is not owned by it.
The framework does not know your services type and performs no cast for you.

## Supplying the context

```cpp
struct ClientServices
{
    EntityManager* Entities;
};
ClientServices Services{&Entities};
auto& Execution = Unit.GetExecutionContext();
Execution.ClientContext = &Services;
Execution.MissingCallTermPolicy = HTNMissingCallTermPolicy::FailSilently;
```

`Unit` is the planning unit associated with `Hook`. It copies the configured
pointer into `HTNPlannerExecutionContext::ClientContext` for each execution,
including deferred calls and top-level call construction. Consumers calling
`HTNPlannerHook::Decompose` directly set `ExecutionContext.ClientContext`.
Core-only consumers set `HTNGeneratedPlannerContext::client_context` directly.
Client pointer, missing-callterm policy and callback are direct execution fields.
They are not stored in `HTNCallTermBindingContext`.
Typed static and daemon bindings receive that execution's context automatically
for both argument conversion and return conversion. Generated C calls use this dispatch. Raw bindings can access the same pointer
via `HTNCallTermArguments::GetClientContext()`.

Explicit conversions outside planner execution accept the same first argument:

```cpp
HTNTryParseType(&Services, Atom, Entity);
HTNTryToAtom(&Services, Entity, Atom);
HTNAtom Call = HTNAtom::sCreateCallWithContext(&Services, Method, Entity);
// Use Call, then release its owned storage.
HTNAtom::sDestroy(Call);
```

Composite converters must forward `inClientContext` to each nested conversion.
The two-argument helpers and existing `sCreateCall` supply `nullptr`. Builtin
converters ignore the context. Custom converters decide whether null is valid;
context-dependent converters should return `false` when required services are
unavailable. There is no fallback to global state.

Conversion failure preserves normal callterm failure behavior. Failed arguments
prevent the callable from running. A failed return conversion produces an unbound
result; effects already performed by the callable are not rolled back. Conversion
failures are not missing-callterm reports.

## Ownership and concurrency

The context is a borrowed `void*`, also used by the missing-callterm report callback.
HTN never allocates, deletes, or retains ownership of the payload or its managers.
Keep them alive throughout planning and callbacks. Returned native references must
remain valid for the duration of the call; converters must not dereference stale
pointers to validate them.

A const execution descriptor does not imply a const client payload. Use a client-defined
structure containing pointers to const services where read-only access is needed.
Set the pointer before execution, or change it between executions after they have
finished. Keep planning-unit payloads alive while deferred calls can still execute;
never mutate a planning unit or execution descriptor concurrently with execution.

Multiple contexts may share one registry. Invocation carries the current pointer
in a local argument view, not in the registry or a global/thread-local variable.
Clients must synchronize access to shared managers and their own mutable state.
The framework does not add locks around converters or client callbacks.

## Migration and ABI

Update every custom `FromAtom` and `ToAtom` specialization with the leading
`void*` parameter, even when unused. Forward it in composite converters. Update
any direct calls to a specialization. Existing context-free helper calls remain
valid; use the explicit overload when a conversion needs services.

Rebuild C++ consumers and host libraries together: `HTNCallTermArguments` has
changed layout. Its callable signature and daemon-instance argument are unchanged.
The generated execution descriptor now includes `client_context`. Planner ABI
versions advance to `0x48540004` (plain), `0x48550005` (debug), `0x48560004`
(profiling) and `0x48570005` (debug plus profiling). Runtime bridge revision 6
passes the execution descriptor to both callterm invocation exports. Policy,
callback and client pointer are read afresh for every invocation. Atom layouts are unchanged.
Regenerate domains and rebuild host, bridge and modules together; older definitions
and bridge tables are rejected. Remove binding-context `SetClientContext` calls
and configure the execution descriptor or planning unit instead.

## Writing world-state facts

`World.WriteFact(Fact, values...)` delegates to
`World.WriteFactWithContext(nullptr, Fact, values...)`. To resolve engine values
through client services, use `World.WriteFactWithContext(&Services, Fact, Entity)`.
Both paths convert each argument through `HTNTryToAtom(context, value, atom)`.
The symbol must belong to the world's fact registry, as before.

Arguments are owned temporaries until all conversions succeed and produce bound
atoms. Failure returns `false` without inserting a row or creating fact storage;
previous rows and other arities remain unchanged. Conversion short-circuits on
failure and releases temporary owned values. Converter side effects on client
services are outside this world-state transaction and are not rolled back.

Native HTN values, custom registered types, string literals and mutable/const
C strings are supported. Null C strings retain the existing empty-string behavior.
C-string conversion is input-only; use `std::string` for conversion from atoms.
Zero-argument facts and duplicate-row append behavior are preserved. No client
context is stored in the world state. Native atoms and lists are copied, even
when passed with `std::move`; inputs remain usable after successful or failed
writes. This intentionally replaces the previous rvalue-consuming behavior.
Keep ownership of raw C atoms/lists and destroy them explicitly; wrap owning
raw temporaries in `HTNAtomOwner`/`HTNAtomListOwner` before passing them to avoid
leaks. RAII-owned temporaries clean themselves up normally. Custom converters
retain their existing `const T&` contract.

This is a C++ header API change only: no C ABI, atom layout or planner ABI change.
Rebuild consumers to use it. Unbound arguments are now rejected rather than stored.

See the [fact-writing release notes](RELEASE_NOTES_WRITE_FACT.md) for ownership
migration requirements and the recorded development validation.
