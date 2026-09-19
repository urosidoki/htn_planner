// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

/**
 * Generator of globally unique identifiers
 */
class HTNGuidGenerator
{
public:
    // Generates a new globally unique ID
    HTN_NODISCARD static uint32 GenerateGUID();
};
