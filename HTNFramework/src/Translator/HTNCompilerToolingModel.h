// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Domain/Diagnostics/HTNDiagnostic.h"
#include "Domain/Source/HTNDomainSource.h"
#include "Translator/HTNCompilerDomainLoader.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

enum class HTNCompilerToolingTokenKind
{
    None,
    VariableValid,
    VariableInvalid,
    ConstantValid,
    ConstantInvalid,
    MethodValid,
    MethodInvalid
};

struct HTNCompilerToolingDefinition
{
    std::filesystem::path FilePath;
    HTNSourceRange Range;
};

class HTNCompilerToolingModel final
{
public:
    void Analyze(const std::filesystem::path& inFilePath,
                 const std::string& inText,
                 const HTNDomainSourceProvider& inSourceProvider = {});

    HTNCompilerToolingTokenKind GetTokenKind(size_t inOffset) const;
    std::vector<std::string> GetAutocompleteCandidates(size_t inCursorOffset) const;
    bool GetDefinitionAtOffset(size_t inCursorOffset,
                               HTNCompilerToolingDefinition& outDefinition) const;

    const std::vector<HTNDiagnostic>& GetDiagnostics() const { return mDiagnostics; }
    const HTNCompilerDomainLoadResult& GetLoadResult() const { return mResult; }
    bool IsLoaded() const { return mLoaded; }

private:
    int FindCurrentFileIndex() const;

    std::filesystem::path mFilePath;
    std::string mText;
    HTNCompilerDomainLoadResult mResult;
    std::vector<HTNDiagnostic> mDiagnostics;
    bool mLoaded = false;
};
