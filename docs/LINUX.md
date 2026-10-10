# Building and using HTN on Linux

The initial supported environment is **Ubuntu 24.04, x86_64**, including Ubuntu
under WSL2. Host code uses C++20; generated domains use C11. Validation uses GCC
14 and Clang 18 with GNU libstdc++. Other distributions, architectures, libc++,
macOS and other Unix systems need separate validation.

## 1. Install dependencies

Run these commands **inside Ubuntu**, not in Windows PowerShell:

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential gcc-14 g++-14 clang-18 \
    cmake ninja-build python3 pkg-config libsdl2-dev libgtest-dev \
    git curl ca-certificates tar binutils
```

Use Python 3.12+, CMake 3.25+ and the Linux executable from
[Premake 5.0.0-beta8](https://github.com/premake/premake-core/releases/tag/v5.0.0-beta8).
Install that pinned release in your user directory and point `PREMAKE5` at it:

```sh
premake_dir="$HOME/.local/share/premake/5.0.0-beta8"
mkdir -p "$premake_dir"
(
    set -e
    cd "$premake_dir"
    curl -fL -o premake-linux.tar.gz \
        https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-linux.tar.gz
    printf '%s  %s\n' 63edd3e7461eebdd45b500a3c7e8ad4e7a67d68f230010f9a97cbb71b4ec59c8 premake-linux.tar.gz | sha256sum -c -
    tar -xzf premake-linux.tar.gz
    chmod u+x premake5
)
export PREMAKE5="$premake_dir/premake5"
```

Repeat the `PREMAKE5` export when opening a new terminal. The bundled
`ThirdParty/premake/bin/premake5.exe` is for Windows. Do not use it
inside Ubuntu. `libsdl2-dev` and `libgtest-dev` are needed for the development
solution; building the SDK alone does not require SDL, ImGui or GoogleTest.

## 2. Get the source

For WSL2, keep the checkout in the Linux filesystem, such as `~/src/htn_planner`,
to avoid slow builds across `/mnt/c`. From Windows PowerShell, enter Ubuntu with
`wsl -d Ubuntu-24.04`, then run the remaining commands in that Linux shell.

```sh
mkdir -p ~/src
cd ~/src
git clone https://github.com/urosidoki/htn_planner.git
cd htn_planner
```

Use a revision that includes these Linux scripts. Every command below starts at
the repository root unless explicitly stated otherwise.

## 3. Build and test the development projects

```sh
# Defaults to GCC 14, Debug and Release:
bash BuildAndTestLinux.sh

# All development configurations:
bash BuildAndTestLinux.sh Debug Release Profile ProfileDetailed
```

This builds the framework, integration, RuntimeBridge, translator, debugger,
HTNDemo, HTNHotReloadDemo, language server, benchmark and tests. It runs the
regression suite, language-server protocol checks, benchmarks, allocation checks
and the compile/hot-reload pipeline. Close other hot-reload demo instances before
running it. Logs are in `build/logs/`; the script stops at the first failure.
`HTN_BUILD_JOBS=4` is the default; lower it on machines with less memory.

**HTNEditor is excluded on Linux.** Its port is not currently a priority because
the editor is still experimental on Windows too. The demo and generated debugger
remain available.

To use Clang, use a **separate source/build tree** and run:

```sh
CC=clang-18 CXX=clang++-18 bash BuildAndTestLinux.sh Debug Release Profile ProfileDetailed
```

Use another tree for additional diagnostic options:

```sh
bash BuildAndTestLinux.sh --atom-diagnostics --generated-execution-profiling \
    --runtime-backtracking-support=enabled Debug Profile ProfileDetailed
```

The script rejects compiler/options changes in an existing development build
tree to prevent stale objects. Atom counters are enabled in Profile, with detailed
counters in ProfileDetailed. Generated category timing needs both ProfileDetailed
and `--generated-execution-profiling`. These optional diagnostics are not enabled
in the packaged SDK variants.

### Run the visual tools

A graphical Linux session or WSLg is required. Software-rendered testing does not
certify every GPU/driver combination.

```sh
(cd HTNDemo && ../bin/Debug-linux-x86_64/HTNDemo/HTNDemo)
./bin/Debug-linux-x86_64/HTNHotReloadDemo/HTNHotReloadDemo
```

The first command preserves the demo's domain/world-state search paths. Hot reload
compiles a candidate `.so`; the demo manages loading and adopting it. An integrating
engine must provide that orchestration. The translator and language server also
run without a desktop:

```sh
./bin/Debug-linux-x86_64/HTNTranslator/HTNTranslator --check Domains/Wanderer.domain
./bin/Debug-linux-x86_64/HTNLanguageServer/HTNLanguageServer
```

The language server expects LSP messages on stdin/stdout; it is not an interactive
shell. Packaging a Linux editor extension is a separate deliverable.

## 4. Build, package and validate the SDK

```sh
bash BuildAndValidateSDK.sh
# Optional candidate version; neither command edits VERSION or publishes anything:
# bash BuildAndValidateSDK.sh --version 2.2.0-rc.1 --jobs 4
```

The version defaults to the repository's `VERSION` file. The script regenerates the
isolated SDK makefiles under `build/sdk/`, rebuilds all four variants, checks the
interpreter boundary, packages the result, extracts the archive to a fresh temporary
directory, then builds and runs external consumers with **both GCC 14 and Clang 18**.
These compilers must therefore both be installed even when the SDK is built with GCC.
It only places a package in `dist/` after validation succeeds.

Output:

```text
dist/HTNSDK-<version>-linux-x86_64/
dist/HTNSDK-<version>-linux-x86_64.tar.gz
dist/HTNSDK-<version>-linux-x86_64.tar.gz.sha256
```

Running the command again replaces this version's local directory, tarball and
SHA-256 file automatically, after both consumer validations pass. Other versions
are unchanged; the command does not publish a release. Build/package logs are in
`build/sdk-linux/<build-id>/`, including copies
of the detailed consumer logs under `external/`; the final output also prints the
temporary external consumer directory. All paths are printed
on failure so a terminal window closing does not lose the evidence.

### Linux variants and binary compatibility

| Variant | Optimization | Decomposition/debug logging/validation |
| --- | --- | --- |
| DebugPlain | Off | Off |
| DebugInstrumented | Off | On |
| ReleasePlain | Full | Off |
| ReleaseInstrumented | Full | On |

All variants contain debug symbols. HTNFramework and HTNIntegration are static
`.a` libraries built with PIC; the optional RuntimeBridge is a `.so`. The translator
is one ReleasePlain executable. The eight Windows names also encode MSVC CRT
linkage; Linux has no `/MT` versus `/MD` axis.

The package records compiler, glibc, libstdc++ ABI, defines and planner/bridge ABI
versions in `manifest.json`. It uses dynamically linked system glibc/libstdc++,
not a universal static Linux runtime. Use the validated Ubuntu environment or a
compatible runtime, or rebuild from source for your target distribution.
Clients must use libstdc++ with `_GLIBCXX_USE_CXX11_ABI=1` and without
`_GLIBCXX_DEBUG`. Clang consumers use libstdc++, not libc++. Never mix variant
headers/defines/libraries, or Windows and Linux binaries.

## 5. Consume an extracted SDK

Run this from the directory containing the downloaded archive and its SHA-256
sidecar. Replace `<version>` below with the version in those filenames.

```sh
sha256sum -c HTNSDK-<version>-linux-x86_64.tar.gz.sha256
tar -xzf HTNSDK-<version>-linux-x86_64.tar.gz
sdk="$PWD/HTNSDK-<version>-linux-x86_64"

# Validate all four variants, with fresh consumer builds outside the package:
bash "$sdk/ValidatePackage.sh" --cc gcc-14 --cxx g++-14
bash "$sdk/ValidatePackage.sh" --cc clang-18 --cxx clang++-18

# Or build and run the examples directly:
cmake -S "$sdk/examples" -B ./htn-consumers -G Ninja \
    -DHTN_DIR="$sdk/cmake" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14
cmake --build ./htn-consumers --parallel 4
ctest --test-dir ./htn-consumers --output-on-failure
```

The SDK contains Core, Integration and dynamically loaded domain examples, an ELF
symbol audit, and an incompatible-domain ABI check. Its validator also checks
checksums/provenance and rejects unknown variants and incompatible libstdc++ ABIs.

In your CMake project, before `find_package`:

```cmake
set(HTN_VARIANT_DEBUG DebugInstrumented)
set(HTN_VARIANT_RELEASE ReleasePlain)
find_package(HTN CONFIG REQUIRED)
target_link_libraries(MyAgent PRIVATE HTN::HTNIntegration) # or HTN::HTNFramework
htn_configure_target(MyAgent)
```

Defaults are DebugInstrumented for Debug and ReleasePlain for Release,
RelWithDebInfo and MinSizeRel. Custom configuration names require an explicit
`HTN_VARIANT_<UPPERCASE_CONFIGURATION>`. `HTN::HTNTranslator` is an imported tool
target that can be used in custom commands. Generated C receives the same defines
as its consumer through the imported library target.

For a domain shared library, link only `HTN::HTNRuntimeBridge`, define
`HTN_GENERATED_MODULE_EXPORTS`, and bind the bridge's host API before executing
the domain. Deploy the matching bridge `.so` and configure the runtime loader's
search path (for example an `$ORIGIN` RUNPATH). The packaged examples show the
build-tree setup; production deployment and unloading remain the client's responsibility.
