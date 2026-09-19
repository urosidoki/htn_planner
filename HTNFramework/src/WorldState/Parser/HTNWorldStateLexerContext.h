// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"
#include "Parser/HTNLexerContextBase.h"

#include <string>
#include <vector>

class HTNToken;

/**
 * Contextual data of a lexer of a world state
 */
class HTNWorldStateLexerContext final : public HTNLexerContextBase
{
public:
    explicit HTNWorldStateLexerContext(const std::string& inText, std::vector<HTNToken>& outTokens);
};
