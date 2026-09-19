# SDK distribution

## Build and validate

Run the complete Windows x64 release gate from the repository root:

```bat
BuildAndValidateSDK.bat
```

The command reads `VERSION`, generates `HTNSDK.sln`, rebuilds all eight SDK
variants, creates the package, extracts it into a temporary directory and builds
and runs its external consumers. It stops on the first failure.

The output is written to:

```text
dist/HTNSDK-<version>-windows-x86_64/
dist/HTNSDK-<version>-windows-x86_64.zip
```

Existing versioned output is preserved. During local iteration, pass `-Force` to
`PackageSDK.cmd` only when replacing that exact local package is intentional.

## Package contents

The package contains:

- Public headers for `HTNFramework` and `HTNIntegration`.
- Libraries and symbols for all supported variants.
- `HTNRuntimeBridge` import libraries, DLLs and symbols.
- A standalone Release `HTNTranslator`.
- CMake package configuration and variant metadata.
- Core, integration and dynamic-domain consumer examples.
- License, attribution, third-party notices and SHA-256 checksums.

Build trees, tests, demos, editors, source-control metadata and development project
files are excluded.

## Components

| Component | Purpose | Required |
| --- | --- | --- |
| `HTNFramework` | Atoms, world state, compiler frontend, generated runtime and public C ABI. | Yes |
| `HTNTranslator` | Domain validation and C source generation. | Yes |
| `HTNIntegration` | Reference planner hook, planning unit and active-plan handling. | No |
| `HTNRuntimeBridge` | Calls from dynamically loaded domain modules into the host runtime. | No |

Core consumers can use `HTNPlanner.h` and manage generated storage directly.
Engine integrations can use `HTNIntegration.h`. Dynamic domain modules additionally
use the bridge matching the selected SDK variant.

## External validation

The packaged `ValidatePackage.cmd` builds consumers using only extracted package
files. Validation covers:

- Direct use of the generated runtime.
- The reference C++ integration.
- Translation and compilation of domain source.
- Dynamic domain loading through `HTNRuntimeBridge`.
- Instrumented debugger event capture.
- Rejection of incompatible CRT and unknown variant selections.

A successful release gate reports all eight variants and 24 consumer executions.
