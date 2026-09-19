// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNScope.h"

#include "HTNPathManager.h"

#include <cassert>

HTNScope::HTNScope(const std::string& inSegment, HTNPathManager& ioPathManager) : mPathManager(ioPathManager)
{
    mResult = ioPathManager.TryPushSegmentToPath(inSegment);
}

HTNScope::~HTNScope()
{
    if (!mResult)
    {
        return;
    }

    [[maybe_unused]] const bool Result = mPathManager.TryPopSegmentFromPath();
    assert(Result);
}
