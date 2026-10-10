# HTN SDK for Linux x86_64

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
See [2.4.0 compatibility and release notes](docs/RELEASE_2_4_0.md), including the
current validation status. Upgrades from before 2.3.0 must also follow its
[compiler-tool migration notes](docs/RELEASE_2_3_0.md).

The exact version, compiler and ABI contract are in `manifest.json`.
Validated on Ubuntu 24.04 with GCC 14 / Clang 18 and libstdc++, C++20 hosts and
C11 generated domains. System glibc/libstdc++ must be compatible with the build.

Start with [the Linux build and consumption guide](docs/LINUX.md), section 5,
and [the domain language reference](docs/DOMAIN_LANGUAGE.md).

Four variants: DebugPlain, DebugInstrumented, ReleasePlain, ReleaseInstrumented.
Framework and optional Integration are static libraries; RuntimeBridge is an
optional shared library. All variants contain debug symbols. The interpreter,
its AST nodes and its integration are excluded. SDL/ImGui and GoogleTest are not
SDK dependencies; graphical demos/tools are available from the source repository.

Use `find_package(HTN CONFIG REQUIRED)` with `HTN_DIR` pointing to `cmake/`, link
`HTN::HTNFramework` or `HTN::HTNIntegration`, and call `htn_configure_target` on your
target. Defaults: Debug -> DebugInstrumented; Release -> ReleasePlain.
The SDK uses libstdc++ ABI 1 without `_GLIBCXX_DEBUG`. Advanced profiling and atom
diagnostic flags are not enabled in these binary variants. Rebuild the SDK and
domains together if you change ABI-affecting flags.

Validate without the source repository:

```sh
bash ValidatePackage.sh --cc gcc-14 --cxx g++-14
bash ValidatePackage.sh --cc clang-18 --cxx clang++-18
```

Requires Python 3.12+, CMake 3.25+, Ninja and binutils, plus the chosen compiler.
The validator verifies every packaged file, then translates domains, compiles their
C, and runs Core, Integration and shared-domain consumers for each variant. It
also checks ELF bridge symbols, incompatible domain ABI rejection and negative
configuration cases. Results/logs go to a fresh temporary directory printed at
the end. An optional `--build-root` must name a directory that does not exist.

`CHECKSUMS.sha256` covers the package contents; the archive has a separate SHA-256
sidecar. These are integrity hashes, not cryptographic publisher signatures.
