// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

#include <string>

enum class HTNTokenType : uint8;

namespace HTNTokenHelpers
{
// Returns a string representation of the token type
HTN_NODISCARD std::string GetTokenTypeString(const HTNTokenType inTokenType);
} // namespace HTNTokenHelpers
