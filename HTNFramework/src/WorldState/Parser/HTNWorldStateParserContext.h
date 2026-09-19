// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"
#include "Parser/HTNParserContextBase.h"
#include "WorldState/HTNWorldStateFwd.h"

#include <vector>

class HTNToken;

/**
 * Contextual data of a parser of a world state
 */
class HTNWorldStateParserContext final : public HTNParserContextBase
{
public:
    explicit HTNWorldStateParserContext(const std::vector<HTNToken>& inTokens, HTNWorldState& outWorldState);

    // Returns the world state
    HTN_NODISCARD const HTNWorldState& GetWorldState() const;

    // Returns the world state
    HTN_NODISCARD HTNWorldState& GetWorldState();

private:
    //----------------------------------------------------------------------//
    // Output
    //----------------------------------------------------------------------//
    HTNWorldState& mWorldState;
};

inline const HTNWorldState& HTNWorldStateParserContext::GetWorldState() const
{
    return mWorldState;
}

inline HTNWorldState& HTNWorldStateParserContext::GetWorldState()
{
    return mWorldState;
}
