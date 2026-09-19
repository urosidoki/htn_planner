// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include "Core/HTNMemoryDebug.h"

class HTNMemoryDebugPanel
{
public:
    void Render();

private:
    HTNMemoryDebugStats mBaseline{};
    bool mHasBaseline = false;
};
