# SDK variants

The Windows x64 SDK ships eight variants. A variant selects the MSVC runtime,
optimization level and generated execution instrumentation independently.

| Variant | CRT | Instrumentation | Optimization |
| --- | --- | --- | --- |
| `StaticDebugPlain` | `/MTd` | Off | Off |
| `StaticDebugInstrumented` | `/MTd` | On | Off |
| `StaticReleasePlain` | `/MT` | Off | Full |
| `StaticReleaseInstrumented` | `/MT` | On | Full |
| `DynamicDebugPlain` | `/MDd` | Off | Off |
| `DynamicDebugInstrumented` | `/MDd` | On | Off |
| `DynamicReleasePlain` | `/MD` | Off | Full |
| `DynamicReleaseInstrumented` | `/MD` | On | Full |

Every variant includes symbols. Instrumented variants define
`HTN_DEBUG_DECOMPOSITION` and include generated event metadata. Instrumentation
does not select a Debug CRT or disable optimization.

## CMake selection

Select a variant for every configuration before calling `find_package`:

```cmake
set(HTN_VARIANT_DEBUG StaticDebugInstrumented)
set(HTN_VARIANT_RELEASENOOPTIMIZATIONS StaticReleaseInstrumented)
set(HTN_VARIANT_PROFILE StaticReleasePlain)
set(HTN_VARIANT_RELEASE StaticReleasePlain)
set(HTN_VARIANT_SUBMISSION StaticReleasePlain)

find_package(HTN CONFIG REQUIRED)
target_link_libraries(MyConsumer PRIVATE HTN::HTNIntegration)
htn_configure_target(MyConsumer)
```

`htn_configure_target` applies the selected runtime library, language level and
required definitions to that target. Every library linked into the same final binary
must use a compatible CRT and iterator-debug setting.

The manifest also propagates `_DEBUG` for Debug CRT variants and `NDEBUG` for
Release CRT variants through the imported targets, including custom configuration
names such as `Validation`. Consequently, assertions in consumer translation
units follow the selected SDK runtime configuration.

Generated source, the host runtime and `HTNRuntimeBridge` must use the same
instrumentation contract. `HTNGeneratedPlanner_ValidateDefinition` and the bridge
binding API reject incompatible modules at runtime.

The SDK manifest records each variant's CRT linkage, configuration,
instrumentation, optimization, iterator-debug level, definitions and ABI identifiers.
