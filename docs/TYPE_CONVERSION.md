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
