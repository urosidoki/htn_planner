// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"
#include "Parser/HTNParserBase.h"
#include "WorldState/HTNWorldStateFwd.h"

struct HTNAtom;
class HTNWorldStateParserContext;

/**
 * Recursive descent parser
 * Builds a world state from a series of tokens
 */
class HTNWorldStateParser final : public HTNParserBase
{
public:
    // Main parse member method
    bool Parse(HTNWorldStateParserContext& ioWorldStateParserContext) const;

private:
    // Parses a fact
    bool ParseFact(HTNWorldStateParserContext& ioWorldStateParserContext, HTNWorldState& outWorldState) const;

    // Parses an identifier
    bool ParseIdentifier(HTNWorldStateParserContext& ioWorldStateParserContext, HTNAtomOwner& outIdentifier) const;

    // Parses an argument
    bool ParseArgument(HTNWorldStateParserContext& ioWorldStateParserContext, HTNAtomOwner& outArgument) const;
};
