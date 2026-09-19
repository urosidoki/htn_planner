// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

class HTNParserContextBase;
class HTNToken;
enum class HTNTokenType : uint8;

/**
 * Base class that builds an object from a series of tokens
 * Reports the first syntax error of the tokens (to avoid reporting cascaded ghost errors)
 */
class HTNParserBase
{
public:
    virtual ~HTNParserBase() = 0;

protected:
    // Parses the token of the given type
    HTN_NODISCARD const HTNToken* ParseToken(const HTNTokenType inTokenType, HTNParserContextBase& ioParserContext) const;
};
