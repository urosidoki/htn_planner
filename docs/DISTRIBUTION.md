# SDK distribution

## Build and validate

Run the appropriate command from the repository root. Both read `VERSION` unless
an explicit candidate override is supplied; neither command publishes a release.

Windows PowerShell:

```powershell
.\BuildAndValidateSDK.bat
# Candidate only: .\BuildAndValidateSDK.bat -Version 2.2.0-rc.1
```

Inside Ubuntu, after completing [Linux setup](LINUX.md), including `PREMAKE5`:

```sh
bash BuildAndValidateSDK.sh
# Candidate only: bash BuildAndValidateSDK.sh --version 2.2.0-rc.1
```

Each script rebuilds the SDK variants, packages them, extracts the archive to a
fresh external directory, and builds/runs consumers. Windows produces eight CRT,
configuration and instrumentation combinations. Linux produces four variants and
validates consumers with both GCC 14 and Clang 18/libstdc++.

Outputs:

```text
dist/HTNSDK-<version>-windows-x86_64/
dist/HTNSDK-<version>-windows-x86_64.zip
dist/HTNSDK-<version>-windows-x86_64.zip.sha256
dist/HTNSDK-<version>-linux-x86_64/
dist/HTNSDK-<version>-linux-x86_64.tar.gz
dist/HTNSDK-<version>-linux-x86_64.tar.gz.sha256
```

Both build-and-validate commands replace this version's local package directory,
archive and SHA-256 file by default. Windows passes `-Force` to the packager;
Linux replaces its outputs after both consumer validations pass. Other versions
are unchanged. Calling `PackageSDK.cmd` directly still requires `-Force`.
These commands do not publish a release. Build final packages with the final
version instead of renaming candidate files, so manifests, provenance and
archive filenames agree.

## Package contents

- Public headers for HTNFramework and HTNIntegration.
- Libraries and debug symbols for all supported platform variants.
- Optional HTNRuntimeBridge DLL/import libraries on Windows, or `.so` on Linux.
- A standalone Release HTNTranslator and CMake package configuration.
- Core, integration and dynamically loaded domain examples.
- `debug/HTN.natvis`; Windows CMake targets attach it to consuming projects.
- Release notes, language/integration guides, license and attribution notices.
- Manifest, build provenance, payload checksums and archive SHA-256 sidecar.

SDL/ImGui, tests, graphical demos, HTNEditor, development build trees and repository
metadata are excluded from SDK packages. Demos/tools remain available from source;
HTNEditor is excluded from Linux builds.

## Components

| Component | Purpose | Required |
| --- | --- | --- |
| HTNFramework | Atoms, world state, compiler frontend, generated runtime and public C ABI. | Yes |
| HTNTranslator | Domain validation and C source generation. | Yes |
| HTNIntegration | Reference planner hook, planning unit and active-plan handling. | No |
| HTNRuntimeBridge | Calls from dynamically loaded domain modules into the host runtime. | No |

The client owns action execution and hot reload orchestration, including module
loading, synchronization and lifetime. Use the bridge matching the host and domain
variant; SDK installation does not implement engine hot reload automatically.

## External validation

Windows `ValidatePackage.cmd` covers eight variants: 24 consumer executions and
eight object/export checks. It rejects incompatible CRT/unknown variants and
requires the Natvis definitions, checksum and generated project attachments.

Linux `ValidatePackage.sh` covers four variants per compiler: 12 consumer
executions, four ELF export audits and four incompatible-domain ABI checks.
Running it with both GCC and Clang gives 40 checks and six negative configuration
checks (unknown variant, old libstdc++ ABI and libstdc++ debug mode per compiler).

Both validate artifact provenance and checksums using the extracted SDK. Record
the script's printed log locations; Linux packaging also preserves detailed
consumer logs under `build/sdk-linux/<build-id>/external/`.

## Compatibility

Version 2.2.0 retains the C API, generated planner ABI, RuntimeBridge ABI and atom
layout from 2.1.0. Use the manifest and matching headers as the authoritative ABI
values for the selected variant. Windows and Linux require separate native builds;
an ABI identifier does not make binaries portable between operating systems.

Follow [2.2.0 compatibility notes](RELEASE_2_2_0.md),
[2.1.0 migration](RELEASE_2_1_0.md), and the earlier
[2.0.4 migration requirements](RELEASE_2_0_4.md) when upgrading older integrations.
