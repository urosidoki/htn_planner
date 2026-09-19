// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNGuidGenerator.h"

#include <atomic>

uint32 HTNGuidGenerator::GenerateGUID()
{
    // GUID uniqueness is all we need here; no ordering/synchronization relationship
    // with any other memory is required. relaxed therefore avoids an unnecessary
    // cross-thread ordering cost while removing the previous data race.
    static std::atomic<uint32> GUID{0};
    return GUID.fetch_add(1u, std::memory_order_relaxed) + 1u;
}
