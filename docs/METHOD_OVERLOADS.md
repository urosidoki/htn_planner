# Method overloading

A method is identified by its name and parameter count. Parameter names and runtime value types do not select an overload.

```lisp
(:domain Example top_level_domain
    (:method (run) top_level_method
        (main () ((act) (act 42))))
    (:method (act)
        (idle () ((!idle))))
    (:method (act ?inp_target)
        (target () ((!act_on ?inp_target))))
)
```

- All method parameters still require the `?inp_` prefix.
- Two declarations with the same name and arity are rejected, even if their parameter names differ.
- Declaration order does not affect call resolution.
- Compound calls, recursive calls, deferred `&` calls and host entry calls select the exact arity. Missing arities fail; there is no fallback to another overload.
- An `overrides Base` declaration replaces only the matching signature. Other inherited overloads remain available. A qualified `Base::act` call selects the exact base implementation with the supplied arity.
- `top_level_method` and deferred reachability apply to each overload independently.
- Axioms also support overloading by name and arity; see [AXIOM_OVERLOADS.md](AXIOM_OVERLOADS.md).
- Overload resolution itself preserves generated entry-point names and plan symbols. Release 2.0.0 changes the runtime ABI through its execution-context updates; regenerate domain C and rebuild the host and modules together. See [migration notes](RELEASE_2_0_0.md).

`Domains/Test/method_overloads.domain` exercises includes, overrides, qualified calls, recursion, overloaded entry points and deferred resolution. Negative tests cover duplicate signatures and missing arities. Editor definition lookup resolves calls using their argument count.
