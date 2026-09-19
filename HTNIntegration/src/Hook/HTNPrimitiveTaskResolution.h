// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <cstdint>

enum class HTNPrimitiveTaskResolution : std::uint8_t
{
    TaskReady,
    PlanCompleted,
    Failed
};
