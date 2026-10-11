# HTN generated planner benchmark

This target measures the generated planner through the same public hooks used by a host application.

## Fact retries and generated debugger (opt-in)

`Domains/Test/fact_retry.domain` selects the latest event using a nested negated
query. No primitive tasks are executed. Build and run from the repository root:

```powershell
New-Item -ItemType Directory -Force build/logs/fact-retry | Out-Null
./ThirdParty/premake/bin/premake5.exe --test-matrix --generated-execution-profiling --runtime-backtracking-support=enabled vs2022
MSBuild HTN.sln /t:Benchmarks\HTNBenchmark /p:Configuration=ReleaseInstrumented /p:Platform=x64 /m:3
./bin/ReleaseInstrumented-windows-x86_64/HTNBenchmark/HTNBenchmark.exe --fact-retries 7 256 > build/logs/fact-retry/benchmark.log
```

The first number is the sample count (minimum three), the second is the maximum
event count with debugger capture/copy enabled. All runs include 16, 64, 256 and
1024 events without capture. Use `--fact-retries 7 1024` to include 1024-event
histories too; the complete history and its deep copy can require substantial
memory. This is a benchmark-only opt-in, not a debugger history limit.

Ascending, descending and shuffled insertion use identical generated C, compiler
flags and runtime backtracking mode. The shuffle seed is 1729. Two warmups precede
the measured samples; output reports medians. Every run checks the selected ID.
Domain translation, fixture construction, old-plan/history destruction and
primitive execution are outside the measured interval. Preparation is measured
inside the generated entry and included in total decomposition time.

Allocation probes and debugger event timers run in separate diagnostic passes:
they do not distort the reported decomposition/copy timing samples. `new_bytes`
is cumulative requested bytes, not peak/live memory. Debugger event time includes
capture and lookup time; do not add those nested columns together. Copy time is a
deep copy of the SDK node vector, not the engine's complete history machinery.
The snapshot digest includes unsuccessful and unstarted nodes, source locations,
titles, bindings and child links, to check that optimizations preserve history.

`--generated-execution-profiling` is needed for deterministic row/retry counters.
It does not require `HTN_PROFILE_DETAILED`. Build `Tests\HTNTest` with the same
options and run `--gtest_filter=*HTNFactRetryTest*` for structural regressions.

## Run

```text
HTNBenchmark.exe [iterations] [max_threads]
HTNBenchmark.exe [iterations] [max_threads] --lifecycle
HTNBenchmark.exe [iterations] [max_threads] --lifecycle-heavy
HTNBenchmark.exe --allocation-self-test
```

The regular suite reports throughput, latency, scaling and generated execution counters. The lifecycle suite measures world state loading, planner setup, the first plan and warmed plans separately. `--lifecycle-heavy` also runs the recursive `FactHeavy100` fixture.

Allocation columns are available when `HTN_BENCHMARK_ALLOCATIONS` is enabled. The `ProfileDetailed` configuration adds attribution by execution phase and allocation source.
