// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtom.h"
#include "HTNCoreMinimal.h"

#include <iterator>

namespace HTNWorldStateHelpers
{
// Returns the number of fact arguments that are bound
template<typename T>
HTN_NODISCARD uint32 CountFactArgumentsBound(const T& inFactArguments);

// Maximum number of fact arguments
inline constexpr size kMaxFactArgumentsNum = 10;

// Maximum number of fact arguments including the fact with no arguments
inline constexpr size kFactArgumentsSize = kMaxFactArgumentsNum + 1;
} // namespace HTNWorldStateHelpers

namespace HTNWorldStateHelpers
{
template<typename T>
uint32 CountFactArgumentsBound(const T& inFactArguments)
{
    uint32 FactArgumentsBoundNum = 0;

    const typename T::const_iterator ItEnd = std::cend(inFactArguments);
    for (auto It = std::cbegin(inFactArguments); It != ItEnd; ++It)
    {
        const HTNAtom& FactArgument = *It;
        if (HTNAtomIsBound(FactArgument))
        {
            ++FactArgumentsBoundNum;
        }
    }

    return FactArgumentsBoundNum;
}
} // namespace HTNWorldStateHelpers
