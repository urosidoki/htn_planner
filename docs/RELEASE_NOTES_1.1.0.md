# HTN Planner 1.1.0

HTN Planner 1.1.0 adds builtin numeric expressions to generated domains.

## Highlights

- Integer and floating-point arithmetic with `+`, `-`, `*`, `/` and `%`.
- Unary increment and decrement expressions with `++` and `--`.
- Nested arithmetic expressions as operands in builtin comparisons.
- Runtime type validation for numeric operands.
- Runtime failure for division or modulo by zero.
- Numeric expression domain available in HTNDemo.

## Validation

- Nested and mixed arithmetic is covered through the generated execution path.
- Failure cases cover division by zero and invalid operand types.
- The packaged Windows x64 SDK is validated across all eight build variants and
  24 isolated consumer executions.

## Compatibility

This release retains the existing generated planner and runtime bridge ABI
identifiers. Existing 1.0.0 integrations remain source and binary compatible.

## Supported platform

- Windows x64.
- Visual Studio 2022 and MSVC v143.
- CMake 3.25 or newer for packaged SDK consumption.

See the [SDK distribution guide](DISTRIBUTION.md), [variant matrix](SDK_VARIANTS.md)
and [release checklist](RELEASE_CHECKLIST.md) for the complete contract.
