// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Parser/HTNLexerContextBase.h"

#include "Parser/HTNToken.h"
#include "Parser/HTNTokenType.h"

#include <algorithm>

HTNLexerContextBase::HTNLexerContextBase(const std::string& inText, std::vector<HTNToken>& outTokens) : mText(inText), mTokens(outTokens)
{
}

HTNLexerContextBase::~HTNLexerContextBase() = default;

void HTNLexerContextBase::MarkTokenStart()
{
    mTokenStartPosition = mPosition;
    mTokenStartRow = mRow;
    mTokenStartColumn = mColumn;
}

void HTNLexerContextBase::AddToken(const HTNAtom& inValue, const HTNTokenType inType HTN_LOG_ONLY(, const std::string& inLexeme))
{
    HTNSourceRange Range;
    Range.Begin.Offset = mTokenStartPosition;
    Range.Begin.Line = static_cast<int>(mTokenStartRow + 1);
    Range.Begin.Column = static_cast<int>(mTokenStartColumn + 1);
    Range.End.Offset = std::max<uint32>(mPosition, mTokenStartPosition + 1);
    Range.End.Line = static_cast<int>(mRow + 1);
    Range.End.Column = static_cast<int>(mColumn + 1);
    if (Range.End.Offset <= Range.Begin.Offset)
    {
        Range.End.Offset = Range.Begin.Offset + 1;
        Range.End.Line = Range.Begin.Line;
        Range.End.Column = Range.Begin.Column + 1;
    }
    mTokens.emplace_back(inValue, inType, Range HTN_LOG_ONLY(, inLexeme));
}

char HTNLexerContextBase::GetCharacter(const uint32 inOffset) const
{
    const uint32 Position = mPosition + inOffset;
    if (Position <= mText.length())
    {
        return mText[Position];
    }

    return '\0';
}

void HTNLexerContextBase::AdvancePosition(const bool inIsNewLine)
{
    if (mPosition >= mText.length())
    {
        return;
    }

    ++mPosition;

    if (inIsNewLine)
    {
        ++mRow;
        mColumn = 0;
    }
    else
    {
        ++mColumn;
    }
}
