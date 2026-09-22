// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Parser/HTNLexerBase.h"

#include "HTNLexerHelpers.h"
#include "Parser/HTNLexerContextBase.h"
#include "Parser/HTNTokenType.h"

#include <charconv>

HTNLexerBase::~HTNLexerBase() = default;

void HTNLexerBase::LexIdentifier(const std::unordered_map<std::string, HTNTokenType>& inKeywords, HTNLexerContextBase& ioLexerContext) const
{
    OPTICK_EVENT("LexIdentifier");

    const uint32 StartPosition = ioLexerContext.GetPosition();

    ioLexerContext.AdvancePosition();

    for (char Character = ioLexerContext.GetCharacter(); HTNLexerHelpers::IsAlphanumeric(Character); Character = ioLexerContext.GetCharacter())
    {
        ioLexerContext.AdvancePosition();
    }

    const uint32       CurrentPosition = ioLexerContext.GetPosition();
    const uint32       EndPosition     = CurrentPosition - StartPosition;
    const std::string& Text            = ioLexerContext.GetText();
    const std::string  Lexeme          = Text.substr(StartPosition, EndPosition);
    const auto         It              = inKeywords.find(Lexeme);
    const HTNTokenType TokenType       = (It != inKeywords.cend()) ? It->second : HTNTokenType::IDENTIFIER;
    if (TokenType == HTNTokenType::TRUE)
    {
        ioLexerContext.AddToken(HTNAtomOwner(true), TokenType HTN_LOG_ONLY(, Lexeme));
    }
    else if (TokenType == HTNTokenType::FALSE)
    {
        ioLexerContext.AddToken(HTNAtomOwner(false), TokenType HTN_LOG_ONLY(, Lexeme));
    }
    else
    {
        ioLexerContext.AddToken(HTNAtomOwner(Lexeme), TokenType HTN_LOG_ONLY(, Lexeme));
    }
}

bool HTNLexerBase::LexNumber(HTNLexerContextBase& ioLexerContext) const
{
    OPTICK_EVENT("LexNumber");
    const uint32 Start = ioLexerContext.GetPosition();
    HTNSourceRange Range;
    Range.Begin = {Start, static_cast<int>(ioLexerContext.GetRow() + 1), static_cast<int>(ioLexerContext.GetColumn() + 1)};
    ioLexerContext.AdvancePosition();
    while (HTNLexerHelpers::IsDigit(ioLexerContext.GetCharacter()))
        ioLexerContext.AdvancePosition();
    const bool IsFloat = ioLexerContext.GetCharacter() == '.' && HTNLexerHelpers::IsDigit(ioLexerContext.GetCharacter(1));
    if (IsFloat)
    {
        ioLexerContext.AdvancePosition();
        while (HTNLexerHelpers::IsDigit(ioLexerContext.GetCharacter()))
            ioLexerContext.AdvancePosition();
    }
    const std::string Lexeme = ioLexerContext.GetText().substr(Start, ioLexerContext.GetPosition() - Start);
    const char* Begin = Lexeme.data();
    const char* End = Begin + Lexeme.size();
    int32 Integer = 0;
    float Floating = 0.0f;
    const auto Conversion = IsFloat ? std::from_chars(Begin, End, Floating) : std::from_chars(Begin, End, Integer);
    if (Conversion.ec != std::errc{} || Conversion.ptr != End)
    {
        Range.End = {ioLexerContext.GetPosition(), static_cast<int>(ioLexerContext.GetRow() + 1),
                     static_cast<int>(ioLexerContext.GetColumn() + 1)};
        ioLexerContext.SetLastError("Number out of bounds", Range);
        return false;
    }
    ioLexerContext.AddToken(IsFloat ? HTNAtomOwner(Floating) : HTNAtomOwner(Integer),
                            HTNTokenType::NUMBER HTN_LOG_ONLY(, Lexeme));
    return true;
}

bool HTNLexerBase::LexString(HTNLexerContextBase& ioLexerContext) const
{
    OPTICK_EVENT("LexString");

    const uint32 StartPosition = ioLexerContext.GetPosition();

    ioLexerContext.AdvancePosition();

    for (char Character = ioLexerContext.GetCharacter(); Character != '"' && ioLexerContext.GetPosition() < ioLexerContext.GetText().size(); Character = ioLexerContext.GetCharacter())
    {
        ioLexerContext.AdvancePosition(Character == '\n');
    }

    if (ioLexerContext.GetCharacter() != '"')
    {
        const uint32 Row    = ioLexerContext.GetRow();
        const uint32 Column = ioLexerContext.GetColumn();
        const std::string Message = "Character '\"' could not be found at end of the string";
        HTNSourceRange Range;
        Range.Begin.Offset = ioLexerContext.GetPosition();
        Range.Begin.Line = static_cast<int>(Row + 1);
        Range.Begin.Column = static_cast<int>(Column + 1);
        Range.End = Range.Begin;
        ioLexerContext.SetLastError(Message, Range);
        HTN_DOMAIN_LOG_ERROR(Row, Column, "{}", Message);
        return false;
    }

    const uint32 CurrentPosition = ioLexerContext.GetPosition();
    const uint32 EndPosition     = CurrentPosition - StartPosition;

    ioLexerContext.AdvancePosition();

    const std::string& Text   = ioLexerContext.GetText();
    const std::string  Lexeme = Text.substr(StartPosition + 1, EndPosition - 1);
    ioLexerContext.AddToken(HTNAtomOwner(Lexeme), HTNTokenType::STRING HTN_LOG_ONLY(, Lexeme));

    return true;
}

void HTNLexerBase::LexComment(HTNLexerContextBase& ioLexerContext) const
{
    OPTICK_EVENT("LexComment");

    for (char Character = ioLexerContext.GetCharacter(); Character != '\n' && ioLexerContext.GetPosition() < ioLexerContext.GetText().size(); Character = ioLexerContext.GetCharacter())
    {
        ioLexerContext.AdvancePosition();
    }

    if (ioLexerContext.GetCharacter() == '\n')
        ioLexerContext.AdvancePosition(true);
}
