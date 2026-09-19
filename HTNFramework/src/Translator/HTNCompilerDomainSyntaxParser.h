// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNCompilerAST.h"

#include <cstdint>
#include <string>

class HTNDiagnosticSink;

// Parses source text directly into compiler-owned syntax.
// The domain loader performs semantic validation and include linking separately.
bool HTNParseCompilerDomainSyntax(const std::string& inSource,
                                  uint32_t inFileIndex,
                                  HTNCompilerAST::Domain& outDomain,
                                  std::string& outError,
                                  HTNSourceRange* outErrorRange = nullptr,
                                  HTNDiagnosticSink* outDiagnostics = nullptr,
                                  const std::string& inFilePath = {});
