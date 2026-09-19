// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtom.h"
#include "Core/HTNAtomOwner.h"
#include "HTNCoreMinimal.h"
#include "Domain/Source/HTNSourceText.h"
#include "Parser/HTNTokenType.h"

#include <string>

/**
 * Sequence of characters representing a unit of meaning in the grammar of a language
 */
class HTNToken
{
public:
    explicit HTNToken(const HTNAtom& inValue,
                      HTNTokenType inType,
                      const HTNSourceRange& inSourceRange HTN_LOG_ONLY(, const std::string& inLexeme));

    // Returns the value of the token
    HTN_NODISCARD const HTNAtom& GetValue() const;

    // Returns the type of the token
    HTN_NODISCARD HTNTokenType GetType() const;

private:
    HTNAtomOwner mValue;
    HTNTokenType mType;
    HTNSourceRange mSourceRange;

public:
    // Source location is compiler/tooling data and is available in every build configuration.
    HTN_NODISCARD const HTNSourceRange& GetSourceRange() const;

private:

#ifdef HTN_ENABLE_LOGGING
public:
    HTN_NODISCARD const std::string& GetLexeme() const;

private:
    std::string mLexeme;
#endif
};

inline const HTNAtom& HTNToken::GetValue() const
{
    return *mValue.Get();
}

inline HTNTokenType HTNToken::GetType() const
{
    return mType;
}

inline const HTNSourceRange& HTNToken::GetSourceRange() const
{
    return mSourceRange;
}

#ifdef HTN_ENABLE_LOGGING
inline const std::string& HTNToken::GetLexeme() const
{
    return mLexeme;
}
#endif
