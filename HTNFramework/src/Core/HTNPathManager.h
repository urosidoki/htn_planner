// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

#include <string>

/**
 * Manages a path
 */
class HTNPathManager
{
public:
    HTNPathManager() = default;
    explicit HTNPathManager(const std::string& inPath);

    // Pushes the given segment to the path
    HTN_NODISCARD bool TryPushSegmentToPath(const std::string& inSegment);

    // Pops a segment from the path
    HTN_NODISCARD bool TryPopSegmentFromPath();

    // Returns the last segment of the path
    HTN_NODISCARD bool FindLastPathSegment(std::string& outSegment) const;

    // Returns the path
    HTN_NODISCARD const std::string& GetPath() const;

private:
    std::string mPath;
};

inline const std::string& HTNPathManager::GetPath() const
{
    return mPath;
}
