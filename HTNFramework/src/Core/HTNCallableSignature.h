// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <cstddef>
#include <string>

// Internal lookup key. Domain identifiers and generated symbols retain their original name.
inline std::string HTNCallableSignature(const std::string& inName, std::size_t inArgumentCount)
{
    return inName + "/" + std::to_string(inArgumentCount);
}
