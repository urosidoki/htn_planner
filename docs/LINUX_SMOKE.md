# Linux generated-path experiment

The isolated CMake project targets an experimental Ubuntu 24.04 x86_64
build using GCC 14 in Debug and Release. This is an initial portability gate,
not a distributable Linux SDK or a claim that all tools support Linux.

```sh
CC=gcc-14 CXX=g++-14 cmake -S cmake/LinuxSmoke -B build/linux-smoke -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/linux-smoke --parallel 2
ctest --test-dir build/linux-smoke --output-on-failure
```

The isolated CMake project builds framework sources with the generated-only SDK
exclusions, the translator and runtime bridge. It translates a domain with the
freshly built native translator and compiles the output as C11. The consumer
checks the exact task and argument after arithmetic evaluation and axiom
backtracking, checks failure and reuses its storage. Additional checks validate
the domain frontend and included domains in AAACombatNPC.

The normal Windows Premake build remains the production entry point. This
experiment uses CMake to avoid bootstrapping a Linux Premake binary and to keep
the initial dependency set small. It disables Optick capture (`USE_OPTICK=0`).
SDL/ImGui, Editor, Language Server, GoogleTest, dynamic bridge loading, hot reload
and SDK packaging are follow-up gates, not covered by this experiment. Existing
Windows SDK variant names encode CRT choices and are not applied to Linux.

## Linux validation status

Native GCC/Linux compilation and execution remain unverified.
The isolated CMake project is experimental; no Linux SDK is distributed.

## Local ClangCL validation

ClangCL 17.0.3 (Visual Studio 2022, Windows x64) successfully built the generated-only
framework, translator, runtime bridge and generated C consumer in Debug and Release.
All three CTest checks passed in each configuration (six successful executions).
The generated C still produces warnings about unused labels and functions.
This validates the Clang frontend with the Windows SDK and MSVC standard library;
GCC, Linux APIs and the Linux ABI remain untested.

```powershell
cmake -S cmake/LinuxSmoke -B build/clang-cl-smoke -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build/clang-cl-smoke --config Debug --parallel 2
ctest --test-dir build/clang-cl-smoke -C Debug --output-on-failure
cmake --build build/clang-cl-smoke --config Release --parallel 2
ctest --test-dir build/clang-cl-smoke -C Release --output-on-failure
```
