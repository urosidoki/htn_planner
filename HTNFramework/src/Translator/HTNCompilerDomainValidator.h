// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNCompilerAST.h"

#include <string>
#include <vector>

class HTNDiagnosticSink;

// Modules are in dependency-first order, with the root module last.
bool HTNValidateCompilerDomainModules(const std::vector<HTNCompilerAST::Domain>& inModules,
                                      const std::vector<std::string>& inSourceFiles,
                                      bool inRequireTopLevelRoot,
                                      HTNDiagnosticSink& outDiagnostics);
