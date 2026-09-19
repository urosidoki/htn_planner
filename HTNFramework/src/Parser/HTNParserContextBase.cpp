// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Parser/HTNParserContextBase.h"

#include "Parser/HTNToken.h"

HTNParserContextBase::HTNParserContextBase(const std::vector<HTNToken>& inTokens) : mTokens(inTokens)
{
}

HTNParserContextBase::~HTNParserContextBase() = default;

const HTNToken* HTNParserContextBase::GetToken(const uint32 inPosition) const
{
    return (inPosition < mTokens.size()) ? &mTokens[inPosition] : nullptr;
}

size HTNParserContextBase::GetTokensSize() const
{
    return mTokens.size();
}
