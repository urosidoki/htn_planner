// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Domain/Diagnostics/HTNDiagnosticSink.h"
#include "Translator/HTNCompilerDomainLoader.h"
#include "Translator/HTNTranslation.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void PrintUsage()
{
    std::cout << "HTNTranslator <domain-file> <entry-point> [output-directory] [options]\n"
                 "HTNTranslator --check <domain-file>\n"
                 "Options:\n"
                 "  --backtracking-policy=fixed-with-overflow|fixed-capacity (default: fixed-with-overflow)\n"
                 "  --backtracking-capacity=<positive integer> (default: 32)\n"
                 "  --runtime-backtracking-support=disabled|enabled (default: disabled)\n"
                 "Example: HTNTranslator Domains/Test/human.domain CreateHumanHTN Generated\n";
}

bool ParsePositiveUint32(const std::string& inText, uint32_t& outValue)
{
    if (inText.empty())
        return false;

    uint32_t Value = 0u;
    const char* Begin = inText.data();
    const char* End = Begin + inText.size();
    const auto Result = std::from_chars(Begin, End, Value);
    if (Result.ec != std::errc{} || Result.ptr != End || Value == 0u)
        return false;

    outValue = Value;
    return true;
}

const char* DiagnosticSeverityName(const HTNDiagnosticSeverity inSeverity)
{
    switch (inSeverity)
    {
    case HTNDiagnosticSeverity::Info: return "info";
    case HTNDiagnosticSeverity::Warning: return "warning";
    case HTNDiagnosticSeverity::Error: return "error";
    }
    return "error";
}

void PrintDiagnostics(const std::vector<HTNDiagnostic>& inDiagnostics, const std::filesystem::path& inFallbackPath)
{
    for (const HTNDiagnostic& Diagnostic : inDiagnostics)
    {
        const std::filesystem::path FilePath =
            Diagnostic.FilePath.empty() ? inFallbackPath : std::filesystem::path(Diagnostic.FilePath);

        const int Line = std::max(1, Diagnostic.Range.Begin.Line);
        const int Column = std::max(1, Diagnostic.Range.Begin.Column);

        // MSVC/VS Code-friendly compiler diagnostic form:
        // file(line,column): error|warning: message
        std::cerr << FilePath.string()
                  << "(" << Line << "," << Column << "): "
                  << DiagnosticSeverityName(Diagnostic.Severity)
                  << ": " << Diagnostic.Message << "\n";
    }
}

}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--check")
    {
        const std::filesystem::path DomainPath(argv[2]);
        HTNCompilerDomainLoadResult LoadResult;
        HTNDiagnosticSink Diagnostics;
        HTNCompilerDomainLoader Loader;
        const bool LoadSucceeded = Loader.Load(DomainPath.string(), LoadResult, Diagnostics);
        PrintDiagnostics(Diagnostics.GetDiagnostics(), DomainPath);
        if (!LoadSucceeded || Diagnostics.HasErrors())
        {
            std::cerr << "HTNTranslator: check failed for '" << DomainPath.string() << "' with "
                      << Diagnostics.GetErrorCount() << " error(s).\n";
            return 4;
        }

        std::cout << "HTNTranslator: '" << DomainPath.string() << "' compiled successfully ("
                  << LoadResult.SourceFiles.size() << " linked source file(s)).\n";
        return 0;
    }

    if (argc < 3)
    {
        PrintUsage();
        return 1;
    }

    const std::filesystem::path DomainPath(argv[1]);
    const std::string EntryPointName(argv[2]);
    std::filesystem::path OutputDirectory = DomainPath.parent_path();
    bool OutputDirectorySpecified = false;
    HTNGeneratedBacktrackingPolicy BacktrackingPolicy = HTNGeneratedBacktrackingPolicy::FixedWithOverflow;
    HTNGeneratedRuntimeBacktrackingSupport RuntimeBacktrackingSupport = HTNGeneratedRuntimeBacktrackingSupport::Disabled;
    uint32_t BacktrackingCapacity = 32u;

    for (int ArgumentIndex = 3; ArgumentIndex < argc; ++ArgumentIndex)
    {
        const std::string Argument(argv[ArgumentIndex]);
        const std::string PolicyPrefix = "--backtracking-policy=";
        const std::string CapacityPrefix = "--backtracking-capacity=";
        const std::string RuntimeBacktrackingSupportPrefix = "--runtime-backtracking-support=";

        if (Argument.rfind(PolicyPrefix, 0u) == 0u)
        {
            const std::string Value = Argument.substr(PolicyPrefix.size());
            if (Value == "fixed-with-overflow")
                BacktrackingPolicy = HTNGeneratedBacktrackingPolicy::FixedWithOverflow;
            else if (Value == "fixed-capacity")
                BacktrackingPolicy = HTNGeneratedBacktrackingPolicy::FixedCapacity;
            else
            {
                std::cerr << "HTNTranslator: unknown backtracking policy '" << Value << "'.\n";
                PrintUsage();
                return 1;
            }
            continue;
        }

        if (Argument.rfind(CapacityPrefix, 0u) == 0u)
        {
            if (!ParsePositiveUint32(Argument.substr(CapacityPrefix.size()), BacktrackingCapacity))
            {
                std::cerr << "HTNTranslator: backtracking capacity must be a positive integer.\n";
                return 1;
            }
            continue;
        }

        if (Argument.rfind(RuntimeBacktrackingSupportPrefix, 0u) == 0u)
        {
            const std::string Value = Argument.substr(RuntimeBacktrackingSupportPrefix.size());
            if (Value == "disabled")
                RuntimeBacktrackingSupport = HTNGeneratedRuntimeBacktrackingSupport::Disabled;
            else if (Value == "enabled")
                RuntimeBacktrackingSupport = HTNGeneratedRuntimeBacktrackingSupport::Enabled;
            else
            {
                std::cerr << "HTNTranslator: unknown runtime backtracking support mode '" << Value << "'.\n";
                PrintUsage();
                return 1;
            }
            continue;
        }

        if (!Argument.empty() && Argument[0] == '-')
        {
            std::cerr << "HTNTranslator: unknown option '" << Argument << "'.\n";
            PrintUsage();
            return 1;
        }

        if (OutputDirectorySpecified)
        {
            std::cerr << "HTNTranslator: more than one output directory was specified.\n";
            PrintUsage();
            return 1;
        }

        OutputDirectory = std::filesystem::path(Argument);
        OutputDirectorySpecified = true;
    }

    HTNTranslationRequest Request;
    Request.DomainPath = DomainPath;
    Request.OutputDirectory = OutputDirectory;
    Request.EntryPointName = EntryPointName;
    Request.BacktrackingPolicy = BacktrackingPolicy;
    Request.RuntimeBacktrackingSupport = RuntimeBacktrackingSupport;
    Request.BacktrackingCapacity = BacktrackingCapacity;

    HTNTranslationResult Result;
    if (!HTNTranslateDomain(Request, Result))
    {
        PrintDiagnostics(Result.Diagnostics, DomainPath);
        std::cerr << "HTNTranslator: " << Result.ErrorMessage << "\n";
        return Result.Failure == HTNTranslationFailure::DomainLoad ? 4 : 5;
    }

    std::cout << "Translated domain '" << Result.DomainId << "' (" << Result.LinkedSourceFileCount << " linked source file(s)).\n"
              << "  Entry point: " << Request.EntryPointName << "\n"
              << "  Backtracking: " << (BacktrackingPolicy == HTNGeneratedBacktrackingPolicy::FixedWithOverflow ? "fixed-with-overflow" : "fixed-capacity")
              << " (capacity " << BacktrackingCapacity << ")\n"
              << "  Runtime backtracking support: "
              << (RuntimeBacktrackingSupport == HTNGeneratedRuntimeBacktrackingSupport::Enabled ? "enabled" : "disabled") << "\n"
              << "  Output: " << Result.OutputSourcePath.string() << "\n";
    return 0;
}
