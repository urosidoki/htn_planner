// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

/**
 * Base class to prevent copying
 */
class HTNUncopyable
{
public:
    HTNUncopyable() = default;

    HTNUncopyable(const HTNUncopyable& inOther)            = delete;
    HTNUncopyable& operator=(const HTNUncopyable& inOther) = delete;
};
