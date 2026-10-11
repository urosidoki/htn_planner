# Generated fact retries and debugger capture

## Correction (development main, 2026-10-10)

Generated `FACT_CHOICE(context, target_solution)` restarted at row zero on every
retry, counted matching rows and discarded previously returned solutions. An
ascending nested latest-event query therefore did cubic row work, on top of the
quadratic search expressed by the domain.

The helper now accepts a next-row offset owned by its generated continuation.
Each invocation starts at zero; retries keep the offset while variable
checkpoints restore the bindings. Nested queries and separate axiom invocations
have independent locals. No cursor state is shared between planners.

The helper reopens the table and refreshes its row count on every attempt, then
seeks directly to the saved offset. It retains no pointers to individual rows
across calls. Facts appended by callterms remain visible, including when a vector
reallocates. The existing contract prohibits deletion, replacement and reordering
during decomposition; external world-state effects are not rolled back.

Debugger changes preserve the full event tree:

- Search only the fixed prefix of pending metadata children, advancing past
  already started children. Completed retry history is never searched again.
- Tokenize each condition's presentation once per decomposition, copying owning
  titles, tokens and source locations into every event.
- Walk bound/scope bits and reserve only the variable bindings actually captured,
  instead of reserving all domain variables twice per event.
- Add opt-in counters/timers under the existing execution-profiling flag. Normal
  builds contain no additional timers. A disabled debugger captures nothing.

## Measurement method

The versioned fixture is `Domains/Test/fact_retry.domain`. Its
`most_important_event` axiom uses `(event ...)` plus a negated nested event query
with a greater-timestamp comparison. `select_event` returns `(!selected id)`.
The benchmark executes no primitive tasks or pathfinding.

Windows 11 Pro 10.0.26200, Intel Core i7-9850H (6 cores/12 threads), 16 GiB RAM,
MSVC 14.39.33519. Both binaries use ReleaseInstrumented, full generated
instrumentation, runtime backtracking support, `HTN_BACKTRACKING_ALL`, and
`HTN_GENERATED_EXECUTION_PROFILING`, without `HTN_PROFILE_DETAILED`. Debugger off/on
uses the same executable and generated domain. Measurements ran sequentially,
without concurrent builds, with two warmups and seven samples per case.
The baseline is `c4e33e8` plus the new fixture/measurement probes, before either
optimization; its executable is retained locally as `baseline-benchmark.exe`.

See [reproduction commands](../HTNBenchmark/README.md#fact-retries-and-generated-debugger-opt-in).
Fixture creation, translation, old-plan/history destruction and primitive
execution are excluded. The generated entry's preparation time is included in
total and also reported separately (under 0.002 ms in these runs). Allocation and
event-timing diagnostics use extra passes, outside latency measurements. A client
history copy is approximated by copying the complete SDK node vector separately;
this does not measure the engine's own additional history machinery.

These are local medians, not CI thresholds or guarantees. The large complete
histories put pressure on this machine's memory; timing variation is expected.
Row counts, node counts and allocation requests are the deterministic evidence.

## Before / after

All times are milliseconds. Debugger-on times include capture but exclude the
separate snapshot copy. Nodes are identical before and after.

| Events | Insertion order | Off before | Off after | On before | On after | Rows before | Rows after | Debug nodes |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | Ascending | 0.0219 | 0.0147 | 0.7292 | 0.2741 | 1,103 | 167 | 359 |
| 16 | Descending | 0.0032 | 0.0022 | 0.0938 | 0.0368 | 153 | 17 | 44 |
| 16 | Shuffled | 0.0081 | 0.0058 | 0.3083 | 0.1209 | 319 | 58 | 135 |
| 64 | Ascending | 0.5616 | 0.1789 | 9.1829 | 4.1086 | 49,983 | 2,207 | 4,487 |
| 64 | Descending | 0.0184 | 0.0062 | 0.2492 | 0.1138 | 2,145 | 65 | 140 |
| 64 | Shuffled | 0.0188 | 0.0064 | 0.2779 | 0.1210 | 2,150 | 68 | 147 |
| 256 | Ascending | 20.0054 | 2.7400 | 168.3059 | 67.3028 | 2,895,103 | 33,407 | 67,079 |
| 256 | Descending | 0.2070 | 0.0214 | 1.3210 | 0.3959 | 33,153 | 257 | 524 |
| 256 | Shuffled | 0.2534 | 0.0456 | 2.3914 | 0.8453 | 36,763 | 536 | 1,131 |
| 1024 | Ascending | 1,130.7057 | 43.0331 | 5,994.1796 | 1,227.4029 | 180,532,223 | 526,847 | 1,054,727 |
| 1024 | Descending | 4.4247 | 0.0831 | 12.0349 | 1.5890 | 525,825 | 1,025 | 2,060 |
| 1024 | Shuffled | 7.7767 | 0.5145 | 35.5129 | 7.6100 | 806,255 | 4,272 | 8,979 |

With the cursor fix alone, ascending 256/1024 off timings were 2.7302/55.2397 ms.
For 256 with capture, cursor-only time was 176.9006 ms: removing row scans does
not remove condition attempts or history, so a separate debugger optimization
was necessary. Capture allocations and history digests were unchanged at that
intermediate stage.

### Debugger work and memory (ascending)

Allocation counts use the benchmark's replacement `new` probes in a separate
pass. Bytes are cumulative requested bytes, not peak/live heap memory. They
include plan creation; debugger-off runs consistently allocate only 3 times / 168
bytes for the result, and record zero nodes/captures.

| Events | Allocations before | Allocations after | Bytes before | Bytes after | Children examined before | Children examined after | Copy before (ms) | Copy after (ms) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 4,276 | 2,620 | 1,132,347 | 432,571 | 3,930 | 51 | 0.2384 | 0.1489 |
| 64 | 53,959 | 31,855 | 14,326,091 | 5,609,163 | 193,378 | 195 | 4.2624 | 2.6383 |
| 256 | 806,293 | 471,997 | 214,877,815 | 84,881,399 | 11,480,450 | 771 | 51.4179 | 48.8135 |
| 1024 | 12,665,604 | 7,396,140 | 3,381,582,188 | 1,339,093,740 | 720,549,378 | 3,075 | 1,313.1869 | 970.5394 |

For 256 events, diagnostic event time changed from 178.55 to 72.95 ms, including
variable capture 68.82 to 35.37 ms and child lookup 27.28 to 1.17 ms. For 1024,
event time changed from 6,731.06 to 1,391.17 ms, capture from 2,335.34 to 657.36 ms,
and lookup from 2,013.29 to 18.62 ms. These timer-enabled passes have extra
overhead and must not be substituted for the latency medians above.

All 700,809 atom snapshots at 256 events and 11,060,745 at 1024 are still captured.
This fixture contains scalar values, so captured list counts are zero. Snapshot
copy allocation counts/bytes remain exactly unchanged: 468,514 / 84,150,152 at
256, and 7,378,978 / 1,327,519,229 at 1024. Timing differences in that unchanged
copy operation reflect allocator/cache/memory effects, not a copy-algorithm fix.

## Correctness and compatibility

- The initial structural regression run had 8 expected failures (4 pass): the
  selected plans were correct, but repeated row scans exceeded the exact bounds.
- New regressions cover exhaustion, bound filters, nested queries, binding
  restoration, callterm appends/reallocation, planner reuse, all four runtime
  backtracking modes, and both fixed-capacity and fixed-with-overflow policies.
- Debugger regressions retain failed comparisons and every candidate binding,
  handle pending nodes entered out of order, preserve separate retries, keep a
  copied snapshot after reset, and verify that disabled capture stays empty.
- All 24 benchmark histories retain the same node/retry/atom-copy counts and the
  same content digest (source ranges, titles/tokens/constants, before/after
  bindings, outcomes, scope masks and parent/child links). Every invocation also
  checks the selected event ID.
- No changes to the C runtime ABI, atom layout or generated descriptor/context.
  Regenerate domains to obtain the cursor optimization. Rebuild the C++ framework
  and debugger consumers together: `HTNGeneratedDebugger` gains private caches
  and its C++ object layout changes. Do not mix older libraries with new headers.
  The public `Node` fields and their owning representation are unchanged.
  Existing published SDK archives and the engine have not been modified.

### Development main validation logs

All paths below are relative to the repository root, under `build/logs/fact-retry/`:

- `baseline-tests.log`: failing structural tests before the cursor fix.
- `baseline-benchmark-1024.log`: complete before measurements.
- `cursor-benchmark.log`: intermediate cursor-only measurements.
- `optimized-benchmark.log`: complete after measurements.
- `comparison.csv`: all 24 before/after cases with history checks.
- `environment.json`: hardware/compiler/system information.
- `optimized-focused-tests.log`: 16 new regression cases passed.
- `release-instrumented-tests.log`: 523 tests passed, including counters.
- `DebugInstrumented-tests.log`: 523 tests passed without optional counters.
- `ReleasePlain-tests.log`: 506 tests passed without debugger/profiling counters.
- `DebugPlain-tests.log`: 506 tests passed without debugger/profiling counters.
- `benchmark-no-counters-build.log` and `benchmark-no-counters-cli.log`: the
  normal benchmark build succeeds and the focused mode explains how to enable
  its optional counters.

### Generated public release merge validation

The merged generated-only branch was rebuilt and validated on Windows/MSVC:

- ReleaseInstrumented with execution profiling: all 439 tests passed, including
  the fact-row counter and complete debugger-history regressions.
- ReleasePlain without debugger or execution profiling: all 422 tests passed.
- The merged benchmark ran three samples after two warmups, with 16, 64, 256
  and 1024 events without capture and 16/64 with capture. All 18 cases preserved
  the row/retry counts, node counts and complete snapshot digests measured on
  development main. This was an integration check, not a new timing baseline.
- The runtime-bridge audit passed (101 declarations, 64 bridge entries), and
  the tracked-source audit found no interpreted planner or node source paths.

Logs are under `build/logs/public-fact-retry-merge/`:

- `release-instrumented-build.log` and `release-instrumented-tests.log`.
- `release-plain-build.log` and `release-plain-tests.log`.
- `benchmark-smoke.log` and `benchmark-history-comparison.log`.
- `runtime-bridge-audit.log` and `source-path-audit.log`.

These checks do not package the SDK or validate the public export on Linux.

## Remaining costs

For ascending input, the corrected query examines exactly
`N * (N + 1) / 2 + 2 * N - 1` rows. This is the quadratic work expressed by the
domain. Descending input examines `N + 1`. The fix removes redundant prefixes;
it does not introduce a timestamp index or special-case event selection.

Capturing and copying over a million nodes remains expensive. No history has
been truncated, and the owning public `Node` representation is unchanged.
Further work could reduce repeated immutable data/binding snapshots, with an
explicit compatibility and ownership design. Likewise, entry/condition
checkpoints still execute once per real attempt. The debugger remains intended
for inspection; its ratio to the much faster non-debug path is not a useful
standalone performance target.
