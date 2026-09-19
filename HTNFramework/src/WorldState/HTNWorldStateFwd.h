// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtomOwner.h"
#include "HTNCoreMinimal.h"
#include "WorldState/HTNWorldStateHelpers.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

struct HTNAtom;
class HTNFactArgumentsTable;
class HTNWorldState;
class HtnSymbol;

using HTNFactArguments           = std::array<HTNAtomOwner, HTNWorldStateHelpers::kFactArgumentsSize>;
using HTNFactArgumentsCollection = std::vector<HTNFactArguments>;

using HTNFactArgumentsTables = std::array<HTNFactArgumentsTable, HTNWorldStateHelpers::kFactArgumentsSize>;

// Interned fact symbol to fact argument tables. String-based public APIs intern at the boundary;
// runtime/generated paths use symbol identity directly.
using HTNFacts = std::unordered_map<const HtnSymbol*, HTNFactArgumentsTables>;
using HTNFact  = std::pair<const HtnSymbol* const, HTNFactArgumentsTables>;
