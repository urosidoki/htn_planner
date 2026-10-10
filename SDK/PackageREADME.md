# HTN SDK

Optional [per-instance list allocators](docs/INSTANCE_LIST_ALLOCATOR.md) control
owning lists and returned plans, including a safe pool with a new/delete fallback.
[Backtracking allocators](docs/BACKTRACKING_ALLOCATOR.md) support call-local scratch
storage and per-call usage statistics. The client exclusively resets its scratch
after decomposition; list allocators must outlive every referencing value.

**ABI migration:** regenerate all domains and rebuild the host and runtime bridge
together. Planner ABI revisions are 9 (plain/profiling) and 11 (debug); the runtime
bridge revision is 10. Previous domains are rejected. Atom/list layouts are unchanged.

Domains use `0`/`1` for boolean matching and typed C++ `bool` arguments. Bare
`true`/`false` are ordinary symbols; migrate old boolean literals and regenerate
domains. Standalone callterm conditions still require BOOL; assigning a BOOL
false result succeeds. Debugger and natvis display BOOL as `0`/`1` without
changing its stored type. These boolean changes alone preserve the atom layout;
the allocator additions above require an ABI migration for this release.

Version 2.4.0 retains unregistered world-state writes for inspection when
`HTN_DEBUG_DECOMPOSITION` is enabled. Access them through `GetUnregisteredFacts()`;
writes still return `false` and the planner cannot query these rows. Plain variants
retain their previous behavior. Rebuild instrumented C++ clients with matching
2.4.0 headers/libraries: the `HTNWorldState` layout changes. Fact inspection alone
preserves the C ABI; the allocator additions require regeneration and rebuilding.
See [2.4.0 compatibility and release notes](docs/RELEASE_2_4_0.md).
This package targets Windows; Linux has a separate archive. The exact version,
including any candidate suffix, is recorded in `manifest.json`.
For upgrades from before 2.3.0, also follow its
[compiler-tool migration notes](docs/RELEASE_2_3_0.md).
Windows Natvis distribution and CMake integration are retained from
[2.2.0](docs/RELEASE_2_2_0.md).
When upgrading from before 2.1.0, retain its
[feature migration requirements](docs/RELEASE_2_1_0.md).
For upgrades from older releases, also follow [2.0.4 migration](docs/RELEASE_2_0_4.md)
and, when starting from 2.0.2 or earlier, [2.0.3 migration](docs/RELEASE_2_0_3.md).

Windows x64, MSVC v143, C++20 clients and C11 generated domains.

New to authoring domains? Start with the [domain language guide](docs/DOMAIN_LANGUAGE.md),
which includes a complete example and the syntax/execution reference.

The eight variant IDs combine `Static` or `Dynamic` CRT linkage, `Debug` or
`Release` CRT, and `Plain` or `Instrumented` HTN execution. For example,
`StaticReleaseInstrumented` uses /MT and retains decomposition debugging,
logging and domain validation. Instrumentation does not imply a Debug CRT.
Debug CRT variants use no optimization; Release CRT variants are optimized.
Every variant includes PDBs. The framework itself is a static library in all
variants; RuntimeBridge is a separate optional DLL.

**Visual Studio type visualizers**

The package includes `debug/HTN.natvis` for `HtnSymbol`, `HTNAtom`,
`HTNAtomOwner` and `HTNAtomList`, in every variant. The Windows CMake targets
attach it automatically to consumers linking `HTN::HTNFramework`,
`HTN::HTNIntegration` or `HTN::HTNRuntimeBridge`.

For an engine with its own project generator, include this file in the generated
Visual Studio project. For a manually maintained project, use **Add > Existing
Item** and select `debug/HTN.natvis` from the extracted SDK. Keep the file from
the same SDK version as your headers and libraries. No global Visual Studio
installation or HTN instrumentation flag is required.

Domain source is consumed by the standalone translator and the resulting C source
uses the generated runtime API. Runtime packages also include the compiler frontend
used by the translator and authoring tools.

Use `find_package(HTN CONFIG REQUIRED)` with `HTN_DIR` pointing to `cmake/`.
Set `HTN_VARIANT_<UPPERCASE_CONFIGURATION>` before finding the package.
Defaults: Debug -> DynamicDebugInstrumented; Release, RelWithDebInfo and
MinSizeRel -> DynamicReleasePlain. Custom configurations require explicit IDs.
Link `HTN::HTNFramework` or `HTN::HTNIntegration` and call
`htn_configure_target(your_target)` to apply the selected CRT and language levels.
That helper changes the target's CRT: all other libraries in the same link must
be compatible. Do not call it to hide an incompatible engine configuration.

All source files using these C++ interfaces and generated domains must receive
the selected defines and runtime. Default MSVC iterator debugging is required:
2 for Debug CRT, 0 for Release CRT. HTN_DEBUG/HTN_RELEASE describe the CRT build;
HTN_DEBUG_DECOMPOSITION controls the debugger ABI independently. Advanced
profiling and atom diagnostics are not enabled in these packaged variants.

`HTN::HTNTranslator` is one self-contained static-CRT Release tool, shared by
all variants. Generated source is compiled with the consumer's selected ABI.
For domain DLLs use `HTN::HTNRuntimeBridge` and deploy its matching DLL beside
the host. The host owns loading, binding and unloading; modules must be unloaded
before their bridge and before host-owned objects are destroyed.

Run `ValidatePackage.cmd` to build and run Core, Integration and domain-DLL
consumers for all eight variants using only the package. Outputs go to a unique
temporary directory. CMake 3.25+ and Visual Studio 2022 C++ tools are required.

CMake and manifest metadata are the supported selection interfaces. Do not infer
ABI compatibility from a folder name or mix headers and binaries from separate
releases.

See [2.0.2 RuntimeBridge correction](docs/RELEASE_2_0_2.md), [2.0.0 migration](docs/RELEASE_2_0_0.md), [use cases](docs/USE_CASES.md),
[client services](docs/TYPE_CONVERSION.md) and [missing-callterm handling](docs/MISSING_CALLTERMS.md).
