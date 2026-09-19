// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "WorldState/Parser/HTNWorldStateParser.h"

#include "Parser/HTNParserHelpers.h"
#include "Parser/HTNToken.h"
#include "Parser/HTNTokenType.h"
#include "WorldState/HTNWorldState.h"
#include "WorldState/Parser/HTNWorldStateParserContext.h"

// clang-format off
/**
 * Backus Naur Form (BNF)
 * <fact> ::= <identifier> <argument>* <end-of-line>
 * <identifier> ::= 'identifier'
 * <argument> ::= ('(' <argument>+ ')') | 'true' | 'false' | 'number' | 'string' | 'identifier-as-symbol'
 */
// clang-format on

bool HTNWorldStateParser::Parse(HTNWorldStateParserContext& ioWorldStateParserContext) const
{
    OPTICK_EVENT("ParseWorldState");

    HTNWorldState& WorldState = ioWorldStateParserContext.GetWorldState();
    while (!ParseToken(HTNTokenType::END_OF_FILE, ioWorldStateParserContext))
    {
        if (!ParseFact(ioWorldStateParserContext, WorldState))
        {
#ifdef HTN_ENABLE_LOGGING
            HTNParserHelpers::PrintLastError(ioWorldStateParserContext);
#endif
            return false;
        }
    }

    return true;
}

bool HTNWorldStateParser::ParseFact(HTNWorldStateParserContext& ioWorldStateParserContext, HTNWorldState& outWorldState) const
{
    OPTICK_EVENT("ParseFact");

    const uint32 StartPosition = ioWorldStateParserContext.GetPosition();

    const HTNToken* IdentifierToken = ioWorldStateParserContext.GetToken(StartPosition);

    HTNAtomOwner Identifier;
    if (!ParseIdentifier(ioWorldStateParserContext, Identifier) || !IdentifierToken)
    {
        ioWorldStateParserContext.SetPosition(StartPosition);
        return false;
    }

    // World-state facts are line-delimited. This used to be implicit because an
    // identifier could only start the next fact. Now that bare identifiers are also
    // valid symbol arguments, the line boundary must remain part of the grammar or
    // the next fact identifier would be consumed as another argument.
    const int FactLine = IdentifierToken->GetSourceRange().Begin.Line;

    std::vector<HTNAtomOwner> Arguments;
    HTNAtomOwner         Argument;
    while (const HTNToken* Token = ioWorldStateParserContext.GetToken(ioWorldStateParserContext.GetPosition()))
    {
        if (Token->GetType() == HTNTokenType::END_OF_FILE || Token->GetSourceRange().Begin.Line != FactLine)
            break;

        if (!ParseArgument(ioWorldStateParserContext, Argument))
            break;

        Arguments.emplace_back(Argument);
    }

    outWorldState.AddFact(Identifier.GetValue<std::string>().c_str(), Arguments);

    return true;
}

bool HTNWorldStateParser::ParseIdentifier(HTNWorldStateParserContext& ioWorldStateParserContext, HTNAtomOwner& outIdentifier) const
{
    OPTICK_EVENT("ParseIdentifier");

    const uint32 StartPosition = ioWorldStateParserContext.GetPosition();

    const HTNToken* IdentifierToken = ParseToken(HTNTokenType::IDENTIFIER, ioWorldStateParserContext);
    if (!IdentifierToken)
    {
        ioWorldStateParserContext.SetPosition(StartPosition);
        return false;
    }

    outIdentifier = IdentifierToken->GetValue();
    return true;
}

bool HTNWorldStateParser::ParseArgument(HTNWorldStateParserContext& ioWorldStateParserContext, HTNAtomOwner& outArgument) const
{
    OPTICK_EVENT("ParseArgument");

    const uint32 StartPosition = ioWorldStateParserContext.GetPosition();

    HTNAtomOwner Argument;

    if (ParseToken(HTNTokenType::LEFT_PARENTHESIS, ioWorldStateParserContext))
    {
        HTNAtomOwner ArgumentElement;
        while (ParseArgument(ioWorldStateParserContext, ArgumentElement))
        {
            Argument.PushBackElementToList(ArgumentElement);
        }

        if (Argument.IsListEmpty())
        {
            ioWorldStateParserContext.SetPosition(StartPosition);
            return false;
        }

        if (!ParseToken(HTNTokenType::RIGHT_PARENTHESIS, ioWorldStateParserContext))
        {
            ioWorldStateParserContext.SetPosition(StartPosition);
            return false;
        }
    }
    else if (const HTNToken* TrueToken = ParseToken(HTNTokenType::TRUE, ioWorldStateParserContext))
    {
        Argument = TrueToken->GetValue();
    }
    else if (const HTNToken* FalseToken = ParseToken(HTNTokenType::FALSE, ioWorldStateParserContext))
    {
        Argument = FalseToken->GetValue();
    }
    else if (const HTNToken* NumberToken = ParseToken(HTNTokenType::NUMBER, ioWorldStateParserContext))
    {
        Argument = NumberToken->GetValue();
    }
    else if (const HTNToken* StringToken = ParseToken(HTNTokenType::STRING, ioWorldStateParserContext))
    {
        Argument = StringToken->GetValue();
    }
    else if (const HTNToken* IdentifierToken = ParseToken(HTNTokenType::IDENTIFIER, ioWorldStateParserContext))
    {
        Argument = HTNAtomOwner(HtnSymbol::sGetSymbol(HTNAtomGetValue<std::string>(IdentifierToken->GetValue())));
    }
    else
    {
        ioWorldStateParserContext.SetPosition(StartPosition);
        return false;
    }

    outArgument = std::move(Argument);
    return true;
}
