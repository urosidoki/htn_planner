// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Parser/HTNParserHelpers.h"

#ifdef HTN_ENABLE_LOGGING
#include "Parser/HTNParserContextBase.h"
#endif

namespace HTNParserHelpers
{
#ifdef HTN_ENABLE_LOGGING
void PrintLastError(const HTNParserContextBase& inParserContext)
{
    const int32        LastErrorRow     = inParserContext.GetLastErrorRow();
    const int32        LastErrorColumn  = inParserContext.GetLastErrorColumn();
    const std::string& LastErrorMessage = inParserContext.GetLastErrorMessage();
    HTN_DOMAIN_LOG_ERROR(LastErrorRow, LastErrorColumn, "{}", LastErrorMessage);
}
#endif
} // namespace HTNParserHelpers
