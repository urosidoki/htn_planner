// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

#include <string>

#ifdef HTN_ENABLE_LOGGING
class HTNLexerContextBase;
#endif

namespace HTNLexerHelpers
{
// Returns whether a character is valid
HTN_NODISCARD bool IsValidCharacter(const char inCharacter);

// Returns whether a character is a letter
HTN_NODISCARD bool IsLetter(const char inCharacter);

// Returns whether a character is a digit
HTN_NODISCARD bool IsDigit(const char inCharacter);

// Returns whether a character is a letter or a digit
HTN_NODISCARD bool IsAlphanumeric(const char inCharacter);

// Returns the escape sequence for the given special character
HTN_NODISCARD std::string GetSpecialCharacterEscapeSequence(const char inCharacter);

#ifdef HTN_ENABLE_LOGGING
// Prints the error of the given message and lexer context
void PrintError(const std::string& inMessage, const HTNLexerContextBase& inLexerContext);
#endif
} // namespace HTNLexerHelpers
