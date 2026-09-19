// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

// Core C++ surface. This header does not require the optional HTNIntegration
// library. Generated domains use their own ABI headers; gameplay policy and
// the default hook/planning-unit implementation live above the core.

#include "Core/HTNAtomOwner.h"
#include "Core/HTNBacktrackingMode.h"
#include "Core/HTNDecompositionStatus.h"
#include "Core/HTNTypeConversion.h"
#include "Core/HTNCallTermBinding.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "WorldState/HTNWorldState.h"

#ifdef HTN_DEBUG_DECOMPOSITION
#include "Translator/HTNGeneratedDebugger.h"
#endif
