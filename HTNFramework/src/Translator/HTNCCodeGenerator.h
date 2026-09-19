// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNCompilerOptions.h"

#include <cstdint>
#include <string>
#include <vector>

namespace HTNCompilerAST { struct Domain; }

struct HTNCCodeGeneratorOptions
{
    std::string OutputSourcePath;
    std::string EntryPointName;
    std::string SourceFilePath;
    std::string SourceText;
    std::vector<std::string> LinkedSourceFiles;

    // Backtracking storage strategy used by the generated planner.
    HTNGeneratedBacktrackingPolicy BacktrackingPolicy = HTNGeneratedBacktrackingPolicy::FixedWithOverflow;

    // Whether generated code may change semantic backtracking behavior at runtime.
    // Disabled is the product default: generated code keeps full backtracking
    // semantics and does not emit HTNBacktrackingMode checks.
    HTNGeneratedRuntimeBacktrackingSupport RuntimeBacktrackingSupport = HTNGeneratedRuntimeBacktrackingSupport::Disabled;

    // Maximum number of pending continuations stored inline. In FixedCapacity
    // mode this is also the hard maximum; FixedWithOverflow may exceed it by
    // allocating overflow storage.
    uint32_t BacktrackingCapacity = 32u;
};

class HTNCCodeGenerator final
{
public:
    bool Generate(const HTNCompilerAST::Domain& inDomain,
                  const HTNCCodeGeneratorOptions& inOptions,
                  std::string& outError) const;
};
