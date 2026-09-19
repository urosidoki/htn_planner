// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

#include <string>
#include <unordered_map>

class HTNLexerContextBase;
enum class HTNTokenType : uint8;

/**
 * Base class that breaks a text into a series of tokens
 * Reports all lexical errors of the text
 */
class HTNLexerBase
{
public:
    virtual ~HTNLexerBase() = 0;

protected:
    // Lexes an identifier
    void LexIdentifier(const std::unordered_map<std::string, HTNTokenType>& inKeywords, HTNLexerContextBase& ioLexerContext) const;

    // Lexes a number
    void LexNumber(HTNLexerContextBase& ioLexerContext) const;

    // Lexes a string
    bool LexString(HTNLexerContextBase& ioLexerContext) const;

    // Lexes a comment
    void LexComment(HTNLexerContextBase& ioLexerContext) const;
};
