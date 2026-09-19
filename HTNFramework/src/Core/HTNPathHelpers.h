// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

#include <string>

namespace HTNPathHelpers
{
// Pushes the given segment to the given path
HTN_NODISCARD bool TryPushSegmentToPath(const std::string& inSegment, std::string& ioPath);

// Pops a segment from the given path
HTN_NODISCARD bool TryPopSegmentFromPath(std::string& ioPath);

// Returns the last segment of the given path
HTN_NODISCARD bool FindLastPathSegment(const std::string& inPath, std::string& outSegment);
} // namespace HTNPathHelpers
