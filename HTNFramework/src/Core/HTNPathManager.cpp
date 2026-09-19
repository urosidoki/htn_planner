// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNPathManager.h"

#include "HTNPathHelpers.h"

HTNPathManager::HTNPathManager(const std::string& inPath) : mPath(inPath)
{
}

bool HTNPathManager::TryPushSegmentToPath(const std::string& inSegment)
{
    return HTNPathHelpers::TryPushSegmentToPath(inSegment, mPath);
}

bool HTNPathManager::TryPopSegmentFromPath()
{
    return HTNPathHelpers::TryPopSegmentFromPath(mPath);
}

bool HTNPathManager::FindLastPathSegment(std::string& outSegment) const
{
    return HTNPathHelpers::FindLastPathSegment(mPath, outSegment);
}
