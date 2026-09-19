// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

// Storage policy encoded into a generated planner.
enum class HTNGeneratedBacktrackingPolicy
{
    FixedWithOverflow,
    FixedCapacity
};

// Whether generated code includes runtime HTNBacktrackingMode checks.
enum class HTNGeneratedRuntimeBacktrackingSupport
{
    Disabled,
    Enabled
};
