// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"
#include "HTNUncopyable.h"

#include <string>

class HTNPathManager;

/**
 * Helper that uses RAII to automatically push/pop a segment to/from a path
 */
class HTNScope : private HTNUncopyable
{
public:
    explicit HTNScope(const std::string& inSegment, HTNPathManager& ioPathManager);
    virtual ~HTNScope();

protected:
    HTNPathManager& mPathManager;
    bool            mResult = false;
};
