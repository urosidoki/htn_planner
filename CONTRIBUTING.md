# Contributing

## Development environment

The supported development environment is Windows x64 with Visual Studio 2022,
MSVC v143 and a Windows SDK. Generate the solution with:

```bat
GenerateProjectFiles.bat
```

Build `HTN.sln` and run `HTNTest` from the repository root. Changes to domain
translation should include focused compiler diagnostics or generated execution
coverage.

## Code and documentation

- Follow the existing C++ and C formatting in the surrounding files.
- Keep runtime code free of C++ exceptions.
- Preserve the boundary between compiler representations and generated execution.
- Document public API, ABI, domain language or SDK packaging changes.
- Avoid committing generated source, binaries, build directories or local IDE files.

## Validation

Before opening a change:

1. Run `git diff --check`.
2. Build the affected configurations.
3. Run the complete `HTNTest` suite.
4. For SDK changes, run `BuildAndValidateSDK.bat`.
5. For dynamic modules, run the hot reload pipeline self-test.

See [CI validation](CI.md) and the [release checklist](docs/RELEASE_CHECKLIST.md)
for the complete project gates.
