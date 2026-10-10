# HTN Planner 2.4.0 source export validation

Validated on 2026-10-10 in the public repository working tree. This candidate
includes boolean language changes, unregistered fact inspection, per-instance
list/safe-pool allocators and call-local backtracking allocators/statistics.

## Tests run from the public checkout

| Windows configuration | Result | Local log |
| --- | --- | --- |
| DebugInstrumented | 423 passed | `build/logs/public-allocators-export-20261010/DebugInstrumented-tests.log` |
| ReleasePlain | 410 passed | `build/logs/public-allocators-export-20261010/ReleasePlain-tests.log` |

These are complete generated framework suites. The instrumented visual HTNDemo
also compiled; no interactive GUI test was performed during this export.
The new allocator suites exercise owning-list cleanup, exhaustion, fallback,
retained plans, scratch reset by the caller, backtracking, deferred calls and
independent concurrent instances. Normal and instrumentation-free generated
domains are covered. Dynamic-module tests reject the previous generated ABI.

The exported code matches the reviewed source snapshot. Public source and project
checks contain no interpreter implementation or interpreter AST nodes.
Logs and the per-file export manifest are local artifacts under
`build/logs/public-allocators-export-20261010/`; they are not distributed.

## Remaining release steps

- This export updates sources, examples, tests, packaging scripts and documentation.
  **It does not rebuild SDK archives.** Existing 2.4.0 archives in `dist/` may
  predate the allocator changes and must not be treated as this candidate.
- Run `BuildAndValidateSDK.bat` and `bash BuildAndValidateSDK.sh` to produce
  and validate fresh Windows/Linux packages and their SHA-256 files.
- Linux was not rerun from the public checkout during this source export; the
  public Linux package and external consumers remain part of release validation.
- Confirm engine integration with the final package. Regenerate all domains and
  rebuild the host and runtime bridge because this release changes their ABI.
- Review, commit, tag and publish using [the release checklist](RELEASE_CHECKLIST.md).

No commit, tag, push or publication was performed by this export.

## Packaging replacement defaults

The build-and-validate commands now replace existing local outputs for the same
version by default. Three filesystem regression tests passed under Ubuntu from
both checkouts, covering repeated replacement/checksums, preservation of other
versions and rejection of output paths/symlinks that could escape the destination.
The Windows PowerShell script also passed syntax validation. Local logs:
`build/logs/sdk-force-default/linux-tests.log` and
`build/logs/sdk-force-default/windows-syntax.log`.
No complete SDK rebuild was run for this script-only follow-up.
