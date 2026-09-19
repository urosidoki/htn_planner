// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

#ifdef HTN_ENABLE_LOGGING
class HTNParserContextBase;
#endif

namespace HTNParserHelpers
{
#ifdef HTN_ENABLE_LOGGING
// Prints the last error of the given parser context
void PrintLastError(const HTNParserContextBase& inParserContext);
#endif
} // namespace HTNParserHelpers
