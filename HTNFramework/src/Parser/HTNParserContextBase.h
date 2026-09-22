// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"
#include "Parser/HTNParserError.h"

#include <string>
#include <vector>

class HTNToken;

/**
 * Base class for the contextual data of a parser
 */
class HTNParserContextBase
{
public:
    explicit HTNParserContextBase(const std::vector<HTNToken>& inTokens);
    virtual ~HTNParserContextBase() = 0;

    // Returns the token at the given position
    HTN_NODISCARD const HTNToken* GetToken(const uint32 inPosition) const;

    // Returns the number of tokens
    HTN_NODISCARD size GetTokensSize() const;

    // Sets the position to the given one
    void SetPosition(const uint32 inPosition);

    // Increments the position
    void AdvancePosition();

    // Returns the position
    HTN_NODISCARD uint32 GetPosition() const;

private:
    //----------------------------------------------------------------------//
    // Input
    //----------------------------------------------------------------------//
    const std::vector<HTNToken>& mTokens;

    //----------------------------------------------------------------------//
    // Internal
    //----------------------------------------------------------------------//
    uint32 mPosition = 0;

public:
    void SetParseError(HTNParserErrorCode inCode, const std::string& inMessage,
                       int32 inRow, int32 inColumn);
    void ClearParseError() { mError = {}; }
    const HTNParserError& GetParseError() const { return mError; }
    HTNParserErrorCode GetParseErrorCode() const { return mError.Code; }
    const std::string& GetLastErrorMessage() const { return mError.Message; }
    int32 GetLastErrorRow() const { return mError.HasError() ? mError.Range.Begin.Line - 1 : -1; }
    int32 GetLastErrorColumn() const { return mError.HasError() ? mError.Range.Begin.Column - 1 : -1; }

private:
    HTNParserError mError;
};

inline void HTNParserContextBase::SetPosition(const uint32 inPosition)
{
    mPosition = inPosition;
}

inline void HTNParserContextBase::AdvancePosition()
{
    ++mPosition;
}

inline uint32 HTNParserContextBase::GetPosition() const
{
    return mPosition;
}

inline void HTNParserContextBase::SetParseError(const HTNParserErrorCode inCode,
                                               const std::string& inMessage,
                                               const int32 inRow,
                                               const int32 inColumn)
{
    HTNSourceRange Range;
    Range.Begin.Line = inRow + 1;
    Range.Begin.Column = inColumn + 1;
    Range.End = Range.Begin;
    ++Range.End.Column;
    mError = {inCode, inMessage, Range};
}
