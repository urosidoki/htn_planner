// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Domain/Source/HTNSourceText.h"
#include "HTNCoreMinimal.h"

#include <string>
#include <vector>

struct HTNAtom;
class HTNToken;
enum class HTNTokenType : uint8;

/**
 * Base class for the contextual data of a lexer
 */
class HTNLexerContextBase
{
public:
    explicit HTNLexerContextBase(const std::string& inText, std::vector<HTNToken>& outTokens);
    virtual ~HTNLexerContextBase() = 0;

    // Returns the text
    HTN_NODISCARD const std::string& GetText() const;

    // Marks the current character as the beginning of the next token.
    void MarkTokenStart();

    // Adds a new token using the most recently marked token start.
    void AddToken(const HTNAtom& inValue, HTNTokenType inType HTN_LOG_ONLY(, const std::string& inLexeme));

    // Returns the character at the position plus the given offset
    HTN_NODISCARD char GetCharacter(const uint32 inOffset = 0) const;

    // Increments the position while maintaining compiler source coordinates.
    void AdvancePosition(bool inIsNewLine = false);

    // Returns the position
    HTN_NODISCARD uint32 GetPosition() const;

    void SetLastError(const std::string& inMessage, const HTNSourceRange& inRange);
    HTN_NODISCARD const std::string& GetLastErrorMessage() const;
    HTN_NODISCARD const HTNSourceRange& GetLastErrorRange() const;

private:
    //----------------------------------------------------------------------//
    // Input
    //----------------------------------------------------------------------//
    const std::string& mText;

    //----------------------------------------------------------------------//
    // Output
    //----------------------------------------------------------------------//
    std::vector<HTNToken>& mTokens;

    //----------------------------------------------------------------------//
    // Internal
    //----------------------------------------------------------------------//
    uint32 mPosition = 0;

public:
    HTN_NODISCARD uint32 GetRow() const;
    HTN_NODISCARD uint32 GetColumn() const;

private:
    uint32 mRow = 0;
    uint32 mColumn = 0;
    uint32 mTokenStartPosition = 0;
    uint32 mTokenStartRow = 0;
    uint32 mTokenStartColumn = 0;
    std::string mLastErrorMessage;
    HTNSourceRange mLastErrorRange;
};

inline const std::string& HTNLexerContextBase::GetText() const
{
    return mText;
}

inline uint32 HTNLexerContextBase::GetPosition() const
{
    return mPosition;
}

inline uint32 HTNLexerContextBase::GetRow() const
{
    return mRow;
}

inline uint32 HTNLexerContextBase::GetColumn() const
{
    return mColumn;
}

inline void HTNLexerContextBase::SetLastError(const std::string& inMessage, const HTNSourceRange& inRange)
{
    mLastErrorMessage = inMessage;
    mLastErrorRange = inRange;
}

inline const std::string& HTNLexerContextBase::GetLastErrorMessage() const
{
    return mLastErrorMessage;
}

inline const HTNSourceRange& HTNLexerContextBase::GetLastErrorRange() const
{
    return mLastErrorRange;
}
