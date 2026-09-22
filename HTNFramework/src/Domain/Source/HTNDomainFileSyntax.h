// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Parser/HTNParserError.h"
#include <string>
#include <vector>

struct HTNDomainInclude
{
    std::string Path;
    HTNSourceRange Range;
};

// File-level syntax independent of compiler AST representations.
// Masks include directives with spaces while preserving original source coordinates.
bool HTNSplitDomainFile(const std::string& inText, std::vector<HTNDomainInclude>& outIncludes,
                        std::string& outDomainText, HTNParserError& outError);
