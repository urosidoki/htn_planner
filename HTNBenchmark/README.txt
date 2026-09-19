HTN generated planner benchmark
================================

This executable benchmarks the generated planner through the public integration API.

Commands:
  HTNBenchmark.exe [iterations] [max_threads]
  HTNBenchmark.exe [iterations] [max_threads] --lifecycle
  HTNBenchmark.exe [iterations] [max_threads] --lifecycle-heavy
  HTNBenchmark.exe --allocation-self-test

The lifecycle mode reports world-state loading, setup, first-plan and warmed-plan costs.
ProfileDetailed adds allocation attribution when allocation tracking is enabled.
