// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNPathHelpers.h"

namespace HTNPathHelpers
{
const std::string kPathSegmentSeparator = " ";

bool TryPushSegmentToPath(const std::string& inSegment, std::string& ioPath)
{
    if (inSegment.empty())
    {
        return false;
    }

    if (!ioPath.empty())
    {
        ioPath.append(kPathSegmentSeparator);
    }

    ioPath.append(inSegment);

    return true;
}

bool TryPopSegmentFromPath(std::string& ioPath)
{
    if (ioPath.empty())
    {
        return false;
    }

    const size Index = ioPath.find_last_of(kPathSegmentSeparator);
    if (Index != std::string::npos)
    {
        ioPath.erase(Index);
    }
    else
    {
        ioPath.clear();
    }

    return true;
}

bool FindLastPathSegment(const std::string& inPath, std::string& outSegment)
{
    const size Index = inPath.find_last_of(kPathSegmentSeparator);
    if (Index == std::string::npos)
    {
        return false;
    }

    outSegment = inPath.substr(Index + 1);

    return true;
}
} // namespace HTNPathHelpers
