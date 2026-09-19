# Release checklist

## Source and public contract

- [ ] `VERSION` contains the intended semantic version.
- [ ] Public API and ABI changes are documented.
- [ ] Incompatible generated planner or bridge changes increment their ABI identifiers.
- [ ] `README.md`, SDK documentation and examples match the packaged API.
- [ ] Copyright, license and third-party notices are current.
- [ ] The repository contains no generated binaries, build trees or local settings.
- [ ] `git diff --check` passes and the release commit has a clean working tree.

## Build and tests

- [ ] Generate and rebuild `HTN.sln` in Debug, Profile and Release.
- [ ] Run the complete `HTNTest` suite.
- [ ] Run the benchmark allocation self-test.
- [ ] Run the hot reload pipeline self-test.
- [ ] Run `BuildAndValidateSDK.bat` successfully.
- [ ] Confirm all eight SDK variants and all external consumer executions pass.
- [ ] Perform the visual HTNDemo smoke test.

## Package inspection

- [ ] `manifest.json` reports the intended version, variants and ABI identifiers.
- [ ] `CHECKSUMS.sha256` covers every packaged file except itself.
- [ ] Headers, libraries, DLLs, tools, symbols, examples and documentation are present.
- [ ] Tests, demos, editors, build output and repository-only files are absent.
- [ ] Extract the ZIP outside the repository and run `ValidatePackage.cmd`.

## Publication

- [ ] Create release notes with features, supported platform and known limitations.
- [ ] Tag the exact public repository commit as `v<VERSION>`.
- [ ] Attach the validated ZIP and its SHA-256 checksum to the release.
- [ ] Download the published artifact and compare its hash with the validated local ZIP.
