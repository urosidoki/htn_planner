# HTN generated planner benchmark

This target measures the generated planner through the same public hooks used by a host application.

## Run

```text
HTNBenchmark.exe [iterations] [max_threads]
HTNBenchmark.exe [iterations] [max_threads] --lifecycle
HTNBenchmark.exe [iterations] [max_threads] --lifecycle-heavy
HTNBenchmark.exe --allocation-self-test
```

The regular suite reports throughput, latency, scaling and generated execution counters. The lifecycle suite measures world state loading, planner setup, the first plan and warmed plans separately. `--lifecycle-heavy` also runs the recursive `FactHeavy100` fixture.

Allocation columns are available when `HTN_BENCHMARK_ALLOCATIONS` is enabled. The `ProfileDetailed` configuration adds attribution by execution phase and allocation source.
