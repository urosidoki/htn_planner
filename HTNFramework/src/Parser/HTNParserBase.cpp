// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Parser/HTNParserBase.h"

#include "Parser/HTNParserContextBase.h"
#include "Parser/HTNToken.h"
#include "Parser/HTNTokenHelpers.h"
#include "Parser/HTNTokenType.h"

#include <algorithm>

HTNParserBase::~HTNParserBase() = default;

const HTNToken* HTNParserBase::ParseToken(const HTNTokenType inTokenType, HTNParserContextBase& ioParserContext) const
{
    const uint32    Position = ioParserContext.GetPosition();
    const HTNToken* Token    = ioParserContext.GetToken(Position);
    if (!Token)
    {
        const size        TokensSize       = ioParserContext.GetTokensSize();
        const std::string LastErrorMessage = std::format("Token at [{}] is out of bounds [{}]", Position, TokensSize);
        ioParserContext.SetLastError(LastErrorMessage, -1, -1);
        return nullptr;
    }

    const HTNTokenType TokenType = Token->GetType();
    if (inTokenType != TokenType)
    {
#ifdef HTN_ENABLE_LOGGING
        const std::string LastErrorMessage =
            std::format("Token [{}] is of type [{}] instead of [{}]", Token->GetLexeme(), HTNTokenHelpers::GetTokenTypeString(Token->GetType()),
                        HTNTokenHelpers::GetTokenTypeString(inTokenType));
#else
        const std::string LastErrorMessage =
            std::format("Token is of type [{}] instead of [{}]", HTNTokenHelpers::GetTokenTypeString(Token->GetType()),
                        HTNTokenHelpers::GetTokenTypeString(inTokenType));
#endif
        const HTNSourceRange& SourceRange = Token->GetSourceRange();
        ioParserContext.SetLastError(
            LastErrorMessage,
            static_cast<int32>(std::max(0, SourceRange.Begin.Line - 1)),
            static_cast<int32>(std::max(0, SourceRange.Begin.Column - 1)));
        return nullptr;
    }

    ioParserContext.AdvancePosition();

    return Token;
}
