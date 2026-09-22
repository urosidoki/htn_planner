// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNCompilerDomainLexer.h"

#include "Core/HTNDomainSyntax.h"
#include "Translator/HTNCompilerDomainLexerContext.h"
#include "Parser/HTNLexerHelpers.h"
#include "Parser/HTNTokenType.h"

bool HTNCompilerDomainLexer::Lex(HTNCompilerDomainLexerContext& ioDomainLexerContext) const
{
    OPTICK_EVENT("LexDomain");

    bool Result = true;

    for (char Character = ioDomainLexerContext.GetCharacter(); HTNLexerHelpers::IsValidCharacter(Character);
         Character      = ioDomainLexerContext.GetCharacter())
    {
        ioDomainLexerContext.MarkTokenStart();
        switch (Character)
        {
        case ':': {
            // Colon
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::COLON HTN_LOG_ONLY(, std::string(1, Character)));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '(': {
            // Left parenthesis
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::LEFT_PARENTHESIS HTN_LOG_ONLY(, std::string(1, Character)));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case ')': {
            // Right parenthesis
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::RIGHT_PARENTHESIS HTN_LOG_ONLY(, std::string(1, Character)));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case HTNPrimitiveTaskPrefix: {
            static constexpr uint32 LookAhead = 1;
            if (ioDomainLexerContext.GetCharacter(LookAhead) == '=')
            {
                ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::NOT_EQUAL HTN_LOG_ONLY(, "!="));
                ioDomainLexerContext.AdvancePosition();
                ioDomainLexerContext.AdvancePosition();
            }
            else
            {
                // Exclamation mark (primitive task prefix)
                ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::EXCLAMATION_MARK HTN_LOG_ONLY(, std::string(1, Character)));
                ioDomainLexerContext.AdvancePosition();
            }
            break;
        }
        case '=': {
            static constexpr uint32 LookAhead = 1;
            if (ioDomainLexerContext.GetCharacter(LookAhead) == '=')
            {
                ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::EQUAL_EQUAL HTN_LOG_ONLY(, "=="));
                ioDomainLexerContext.AdvancePosition();
                ioDomainLexerContext.AdvancePosition();
                break;
            }
            const std::string Message = "Expected '=' after '=' for comparison operator '=='";
            HTNSourceRange ErrorRange;
            ErrorRange.Begin.Offset = ioDomainLexerContext.GetPosition();
            ErrorRange.Begin.Line = static_cast<int>(ioDomainLexerContext.GetRow() + 1);
            ErrorRange.Begin.Column = static_cast<int>(ioDomainLexerContext.GetColumn() + 1);
            ErrorRange.End = ErrorRange.Begin;
            ++ErrorRange.End.Offset;
            ++ErrorRange.End.Column;
            ioDomainLexerContext.SetLastError(Message, ErrorRange);
            Result = false;
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '<': {
            static constexpr uint32 LookAhead = 1;
            const bool HasEqual = ioDomainLexerContext.GetCharacter(LookAhead) == '=';
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HasEqual ? HTNTokenType::LESS_EQUAL : HTNTokenType::LESS
                HTN_LOG_ONLY(, HasEqual ? "<=" : "<"));
            ioDomainLexerContext.AdvancePosition();
            if (HasEqual) ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '>': {
            static constexpr uint32 LookAhead = 1;
            const bool HasEqual = ioDomainLexerContext.GetCharacter(LookAhead) == '=';
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HasEqual ? HTNTokenType::GREATER_EQUAL : HTNTokenType::GREATER
                HTN_LOG_ONLY(, HasEqual ? ">=" : ">"));
            ioDomainLexerContext.AdvancePosition();
            if (HasEqual) ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '+': {
            const bool IsIncrement = ioDomainLexerContext.GetCharacter(1) == '+';
            ioDomainLexerContext.AddToken(HTNAtomOwner(), IsIncrement ? HTNTokenType::INCREMENT : HTNTokenType::PLUS
                HTN_LOG_ONLY(, IsIncrement ? "++" : "+"));
            ioDomainLexerContext.AdvancePosition();
            if (IsIncrement) ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '-': {
            const bool IsDecrement = ioDomainLexerContext.GetCharacter(1) == '-';
            ioDomainLexerContext.AddToken(HTNAtomOwner(), IsDecrement ? HTNTokenType::DECREMENT : HTNTokenType::MINUS
                HTN_LOG_ONLY(, IsDecrement ? "--" : "-"));
            ioDomainLexerContext.AdvancePosition();
            if (IsDecrement) ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '*': {
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::MULTIPLY HTN_LOG_ONLY(, "*"));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '%': {
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::MODULO HTN_LOG_ONLY(, "%"));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case HTNVariablePrefix: {
            // Question mark
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::QUESTION_MARK HTN_LOG_ONLY(, std::string(1, Character)));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case HTNAxiomCallPrefix: {
            // Hash
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::HASH HTN_LOG_ONLY(, std::string(1, Character)));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case HTNDeferredCallPrefix: {
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::AMPERSAND HTN_LOG_ONLY(, std::string(1, Character)));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case HTNConstantPrefix: {
            // At
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::AT HTN_LOG_ONLY(, std::string(1, Character)));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '/': {
            static constexpr uint32 LookAhead     = 1;
            const char              NextCharacter = ioDomainLexerContext.GetCharacter(LookAhead);
            if (NextCharacter == '/')
            {
                // Comment
                LexComment(ioDomainLexerContext);
                break;
            }
            ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::DIVIDE HTN_LOG_ONLY(, "/"));
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '"': {
            // String
            Result = LexString(ioDomainLexerContext) && Result;
            break;
        }
        case '\r':
        case ' ': {
            // Whitespace
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '\t': {
            // Tab
            ioDomainLexerContext.AdvancePosition();
            break;
        }
        case '\n': {
// Newline
            ioDomainLexerContext.AdvancePosition(true);
            break;
        }
        default: {
            if (HTNLexerHelpers::IsDigit(Character))
            {
                // Number
                Result = LexNumber(ioDomainLexerContext) && Result;
                break;
            }
            else if (HTNLexerHelpers::IsLetter(Character))
            {
                // Identifier
                static const std::unordered_map<std::string, HTNTokenType> Keywords = {
                    {"domain", HTNTokenType::HTN_DOMAIN}, {"top_level_domain", HTNTokenType::HTN_TOP_LEVEL_DOMAIN},
                    {"base", HTNTokenType::HTN_BASE}, {"overrides", HTNTokenType::HTN_OVERRIDES},
                    {"method", HTNTokenType::HTN_METHOD}, {"top_level_method", HTNTokenType::HTN_TOP_LEVEL_METHOD},
                    {"axiom", HTNTokenType::HTN_AXIOM},   {"constants", HTNTokenType::HTN_CONSTANTS},
                    {"and", HTNTokenType::AND},           {"or", HTNTokenType::OR},
                    {"alt", HTNTokenType::ALT},           {"not", HTNTokenType::NOT},
                    {"call", HTNTokenType::CALL},         {"true", HTNTokenType::TRUE},         {"false", HTNTokenType::FALSE}};
                LexIdentifier(Keywords, ioDomainLexerContext);
                break;
            }

            const std::string Message = std::format("Character [{}] not recognized", HTNLexerHelpers::GetSpecialCharacterEscapeSequence(Character));
            HTNSourceRange ErrorRange;
            ErrorRange.Begin.Offset = ioDomainLexerContext.GetPosition();
            ErrorRange.Begin.Line = static_cast<int>(ioDomainLexerContext.GetRow() + 1);
            ErrorRange.Begin.Column = static_cast<int>(ioDomainLexerContext.GetColumn() + 1);
            ErrorRange.End = ErrorRange.Begin;
            ++ErrorRange.End.Offset;
            ++ErrorRange.End.Column;
            ioDomainLexerContext.SetLastError(Message, ErrorRange);
#ifdef HTN_ENABLE_LOGGING
            HTNLexerHelpers::PrintError(Message, ioDomainLexerContext);
#endif
            Result = false;
            ioDomainLexerContext.AdvancePosition();
        }
        }
    }

    ioDomainLexerContext.MarkTokenStart();
    ioDomainLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::END_OF_FILE HTN_LOG_ONLY(, ""));

    return Result;
}
