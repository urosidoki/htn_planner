// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "WorldState/Parser/HTNWorldStateLexer.h"

#include "Parser/HTNLexerHelpers.h"
#include "Parser/HTNTokenType.h"
#include "WorldState/Parser/HTNWorldStateLexerContext.h"

bool HTNWorldStateLexer::Lex(HTNWorldStateLexerContext& ioWorldStateLexerContext) const
{
    OPTICK_EVENT("LexWorldState");

    bool Result = true;

    for (char Character = ioWorldStateLexerContext.GetCharacter(); HTNLexerHelpers::IsValidCharacter(Character);
         Character      = ioWorldStateLexerContext.GetCharacter())
    {
        ioWorldStateLexerContext.MarkTokenStart();
        switch (Character)
        {
        case '(': {
            // Left parenthesis
            ioWorldStateLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::LEFT_PARENTHESIS HTN_LOG_ONLY(, std::string(1, Character)));
            ioWorldStateLexerContext.AdvancePosition();
            break;
        }
        case ')': {
            // Right parenthesis
            ioWorldStateLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::RIGHT_PARENTHESIS HTN_LOG_ONLY(, std::string(1, Character)));
            ioWorldStateLexerContext.AdvancePosition();
            break;
        }
        case '/': {
            static constexpr uint32 LookAhead     = 1;
            const char              NextCharacter = ioWorldStateLexerContext.GetCharacter(LookAhead);
            if (NextCharacter == '/')
            {
                // Comment
                LexComment(ioWorldStateLexerContext);
                break;
            }

#ifdef HTN_ENABLE_LOGGING
            const std::string Message = std::format("Expected '/' after [{}] for a comment", Character);
            HTNLexerHelpers::PrintError(Message, ioWorldStateLexerContext);
#endif
            Result = false;
            ioWorldStateLexerContext.AdvancePosition();
            break;
        }
        case '"': {
            // String
            Result = LexString(ioWorldStateLexerContext) && Result;
            break;
        }
        case '\r':
        case ' ': {
            // Whitespace
            ioWorldStateLexerContext.AdvancePosition();
            break;
        }
        case '\n': {
// Newline
            ioWorldStateLexerContext.AdvancePosition(true);
            break;
        }
        default: {
            if (HTNLexerHelpers::IsDigit(Character))
            {
                // Number
                Result = LexNumber(ioWorldStateLexerContext) && Result;
                break;
            }
            else if (HTNLexerHelpers::IsLetter(Character))
            {
                // Identifier
                static const std::unordered_map<std::string, HTNTokenType> Keywords = {{"call", HTNTokenType::CALL}, {"true", HTNTokenType::TRUE}, {"false", HTNTokenType::FALSE}};
                LexIdentifier(Keywords, ioWorldStateLexerContext);
                break;
            }

#ifdef HTN_ENABLE_LOGGING
            const std::string Message = std::format("Character [{}] not recognized", HTNLexerHelpers::GetSpecialCharacterEscapeSequence(Character));
            HTNLexerHelpers::PrintError(Message, ioWorldStateLexerContext);
#endif
            Result = false;
            ioWorldStateLexerContext.AdvancePosition();
        }
        }
    }

    ioWorldStateLexerContext.MarkTokenStart();
    ioWorldStateLexerContext.AddToken(HTNAtomOwner(), HTNTokenType::END_OF_FILE HTN_LOG_ONLY(, ""));

    return Result;
}
