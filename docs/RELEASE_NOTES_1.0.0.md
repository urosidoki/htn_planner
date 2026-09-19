# HTN Planner 1.0.0

HTN Planner 1.0.0 is the first stable public release of the generated planner,
compiler toolchain and Windows x64 SDK.

## Highlights

- Ahead-of-time compilation from domain source to native C.
- Compiler-owned lexer, AST, validation, linking and IR pipeline.
- Generated runtime with configurable fact, axiom and hierarchical backtracking.
- World state, lists, callterms, includes, overrides and deferred domain calls.
- Generated execution debugger with source locations and bound values.
- Optional C++ integration with planner hooks and active-plan handling.
- Runtime bridge and example pipeline for dynamically loaded domains and hot reload.
- SDL and ImGui visual demo with complex example domains.
- Editor, language server and VS Code language support.

## SDK

The Windows x64 package supports MSVC v143, C++20 clients and C11 generated
domains. It contains eight variants covering `/MT`, `/MTd`, `/MD` and `/MDd`,
with plain and instrumented execution contracts.

The release gate builds every variant and runs 24 isolated consumer executions,
including direct runtime use, the reference integration and dynamic domain modules.

## Engine integration validation

The SDK has been integrated into an external game engine with customized hooks,
planning units, resource conversion, engine-specific callterms, visual debugging
and dynamic domain hot reload. This integration exercises the public ABI and package
outside the planner repository.

## Supported platform

- Windows x64.
- Visual Studio 2022 and MSVC v143.
- CMake 3.25 or newer for packaged SDK consumption.

See the [SDK distribution guide](DISTRIBUTION.md), [variant matrix](SDK_VARIANTS.md)
and [release checklist](RELEASE_CHECKLIST.md) for the complete contract.
