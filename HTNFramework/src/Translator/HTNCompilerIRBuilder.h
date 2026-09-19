// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNCompilerIR.h"

#include <string>
#include <vector>

namespace HTNCompilerAST { struct Domain; }

// Lower a linked domain into the compiler's independent representation.
// The output has no references to the AST or source text.
bool HTNBuildCompilerIR(const HTNCompilerAST::Domain& inDomain,
                        const std::vector<std::string>& inSourceFiles,
                        HTNGeneratedRuntimeBacktrackingSupport inRuntimeBacktrackingSupport,
                        HTNCompilerIR& outIR,
                        std::string& outError);
