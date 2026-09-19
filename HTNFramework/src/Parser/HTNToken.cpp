// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Parser/HTNToken.h"

HTNToken::HTNToken(const HTNAtom& inValue,
                   const HTNTokenType inType,
                   const HTNSourceRange& inSourceRange HTN_LOG_ONLY(, const std::string& inLexeme))
    : mValue(inValue), mType(inType), mSourceRange(inSourceRange) HTN_LOG_ONLY(, mLexeme(inLexeme))
{
}
