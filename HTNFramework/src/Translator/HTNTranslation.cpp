// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNTranslation.h"

#include "Domain/Diagnostics/HTNDiagnosticSink.h"
#include "Translator/HTNCompilerDomainLoader.h"

namespace
{
std::string MakePortableDomainPath(const std::filesystem::path& inPath)
{
    const std::string GenericPath = inPath.generic_string();
    const std::string Marker = "Domains/";
    const size_t MarkerPosition = GenericPath.find(Marker);
    if (MarkerPosition != std::string::npos)
        return GenericPath.substr(MarkerPosition);
    return inPath.filename().generic_string();
}
}

bool HTNTranslateDomain(const HTNTranslationRequest& inRequest, HTNTranslationResult& outResult)
{
    outResult = {};

    if (inRequest.DomainPath.empty())
    {
        outResult.Failure = HTNTranslationFailure::InvalidOptions;
        outResult.ErrorMessage = "A domain file must be specified.";
        return false;
    }

    if (inRequest.EntryPointName.empty())
    {
        outResult.Failure = HTNTranslationFailure::InvalidOptions;
        outResult.ErrorMessage = "An entry point name must be specified.";
        return false;
    }

    if (inRequest.BacktrackingPolicy != HTNGeneratedBacktrackingPolicy::FixedWithOverflow &&
        inRequest.BacktrackingPolicy != HTNGeneratedBacktrackingPolicy::FixedCapacity)
    {
        outResult.Failure = HTNTranslationFailure::InvalidOptions;
        outResult.ErrorMessage = "Unknown backtracking policy.";
        return false;
    }

    if (inRequest.RuntimeBacktrackingSupport != HTNGeneratedRuntimeBacktrackingSupport::Disabled &&
        inRequest.RuntimeBacktrackingSupport != HTNGeneratedRuntimeBacktrackingSupport::Enabled)
    {
        outResult.Failure = HTNTranslationFailure::InvalidOptions;
        outResult.ErrorMessage = "Unknown runtime backtracking support mode.";
        return false;
    }

    if (inRequest.BacktrackingCapacity == 0u)
    {
        outResult.Failure = HTNTranslationFailure::InvalidOptions;
        outResult.ErrorMessage = "Backtracking capacity must be greater than zero.";
        return false;
    }

    const std::filesystem::path OutputDirectory =
        inRequest.OutputDirectory.empty() ? inRequest.DomainPath.parent_path() : inRequest.OutputDirectory;

    HTNCompilerDomainLoadResult LoadResult;
    HTNDiagnosticSink Diagnostics;
    HTNCompilerDomainLoader Loader;
    const bool LoadSucceeded = Loader.Load(inRequest.DomainPath.string(), LoadResult, Diagnostics);
    outResult.Diagnostics = Diagnostics.GetDiagnostics();

    if (!LoadSucceeded || Diagnostics.HasErrors())
    {
        outResult.Failure = HTNTranslationFailure::DomainLoad;
        outResult.ErrorMessage = "Loading/linking failed for '" + inRequest.DomainPath.string() + "'.";
        return false;
    }

    HTNCCodeGeneratorOptions Options;
    Options.OutputSourcePath = (OutputDirectory / (inRequest.DomainPath.stem().string() + ".generated.c")).string();
    Options.EntryPointName = inRequest.EntryPointName;
    Options.SourceFilePath = MakePortableDomainPath(inRequest.DomainPath);
    Options.SourceText = LoadResult.LinkedSourceText;
    Options.BacktrackingPolicy = inRequest.BacktrackingPolicy;
    Options.RuntimeBacktrackingSupport = inRequest.RuntimeBacktrackingSupport;
    Options.BacktrackingCapacity = inRequest.BacktrackingCapacity;
    for (const std::string& SourceFile : LoadResult.SourceFiles)
        Options.LinkedSourceFiles.emplace_back(MakePortableDomainPath(std::filesystem::path(SourceFile)));

    HTNCCodeGenerator Generator;
    std::string GenerationError;
    if (!Generator.Generate(LoadResult.Domain, Options, GenerationError))
    {
        outResult.Failure = HTNTranslationFailure::CodeGeneration;
        outResult.ErrorMessage = GenerationError;
        return false;
    }

    outResult.Succeeded = true;
    outResult.Failure = HTNTranslationFailure::None;
    outResult.DomainId = LoadResult.Domain.GetID();
    outResult.LinkedSourceFileCount = LoadResult.SourceFiles.size();
    outResult.OutputSourcePath = Options.OutputSourcePath;
    return true;
}
