# HTN SDK

Windows x64, MSVC v143, C++20 clients and C11 generated domains.

The eight variant IDs combine `Static` or `Dynamic` CRT linkage, `Debug` or
`Release` CRT, and `Plain` or `Instrumented` HTN execution. For example,
`StaticReleaseInstrumented` uses /MT and retains decomposition debugging,
logging and domain validation. Instrumentation does not imply a Debug CRT.
Debug CRT variants use no optimization; Release CRT variants are optimized.
Every variant includes PDBs. The framework itself is a static library in all
variants; RuntimeBridge is a separate optional DLL.

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
