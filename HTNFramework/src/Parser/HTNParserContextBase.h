// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

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
    // Stores the most recent parse error. Structured diagnostics need this even when logging is disabled.
    void SetLastError(const std::string& inLastErrorMessage, const int32 inLastErrorRow, const int32 inLastErrorColumn);

    HTN_NODISCARD const std::string& GetLastErrorMessage() const;
    HTN_NODISCARD int32 GetLastErrorRow() const;
    HTN_NODISCARD int32 GetLastErrorColumn() const;

protected:
    std::string mLastErrorMessage;
    int32       mLastErrorRow    = -1;
    int32       mLastErrorColumn = -1;
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

inline void HTNParserContextBase::SetLastError(const std::string& inLastErrorMessage, const int32 inLastErrorRow, const int32 inLastErrorColumn)
{
    mLastErrorMessage = inLastErrorMessage;
    mLastErrorRow     = inLastErrorRow;
    mLastErrorColumn  = inLastErrorColumn;
}

inline const std::string& HTNParserContextBase::GetLastErrorMessage() const
{
    return mLastErrorMessage;
}

inline int32 HTNParserContextBase::GetLastErrorRow() const
{
    return mLastErrorRow;
}

inline int32 HTNParserContextBase::GetLastErrorColumn() const
{
    return mLastErrorColumn;
}
