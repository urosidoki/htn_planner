// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Domain/Source/HTNDomainSource.h"
#include "Translator/HTNCompilerAST.h"

#include <string>
#include <vector>

class HTNDiagnosticSink;

struct HTNCompilerDomainLoadResult
{
    HTNCompilerAST::Domain Domain;
    std::string LinkedSourceText;
    std::vector<std::string> SourceFiles;
};

class HTNCompilerDomainLoader final
{
public:
    bool Load(const std::string& inRootFilePath,
              HTNCompilerDomainLoadResult& outResult,
              HTNDiagnosticSink& outDiagnostics,
              const HTNDomainLoadOptions& inOptions = {}) const;

    bool LoadFromSource(const std::string& inRootFilePath,
                        const std::string& inRootSourceText,
                        const HTNDomainSourceProvider& inSourceProvider,
                        HTNCompilerDomainLoadResult& outResult,
                        HTNDiagnosticSink& outDiagnostics,
                        const HTNDomainLoadOptions& inOptions = {}) const;
};
