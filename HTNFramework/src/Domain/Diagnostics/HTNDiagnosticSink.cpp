// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Domain/Diagnostics/HTNDiagnosticSink.h"

#include <algorithm>

void HTNDiagnosticSink::Clear()
{
    mDiagnostics.clear();
}

void HTNDiagnosticSink::Report(const HTNDiagnostic& inDiagnostic)
{
    // Recovery can legitimately revisit a malformed construct. Keep the user-facing
    // diagnostic set stable and suppress exact duplicates at the common sink boundary.
    const auto Duplicate = std::find_if(
        mDiagnostics.begin(),
        mDiagnostics.end(),
        [&](const HTNDiagnostic& Existing)
        {
            return Existing.Severity == inDiagnostic.Severity &&
                   Existing.Recovery == inDiagnostic.Recovery &&
                   Existing.FilePath == inDiagnostic.FilePath &&
                   Existing.Message == inDiagnostic.Message &&
                   Existing.Range.Begin.Offset == inDiagnostic.Range.Begin.Offset &&
                   Existing.Range.End.Offset == inDiagnostic.Range.End.Offset &&
                   Existing.Range.Begin.Line == inDiagnostic.Range.Begin.Line &&
                   Existing.Range.Begin.Column == inDiagnostic.Range.Begin.Column &&
                   Existing.Range.End.Line == inDiagnostic.Range.End.Line &&
                   Existing.Range.End.Column == inDiagnostic.Range.End.Column;
        });

    if (Duplicate == mDiagnostics.end())
        mDiagnostics.emplace_back(inDiagnostic);
}

void HTNDiagnosticSink::Report(const HTNDiagnosticSeverity inSeverity,
                               const HTNDiagnosticRecovery inRecovery,
                               const std::string& inFilePath,
                               const HTNSourceRange& inRange,
                               const std::string& inMessage)
{
    HTNDiagnostic Diagnostic;
    Diagnostic.Severity = inSeverity;
    Diagnostic.Recovery = inRecovery;
    Diagnostic.FilePath = inFilePath;
    Diagnostic.Range = inRange;
    Diagnostic.Message = inMessage;
    Report(Diagnostic);
}

void HTNDiagnosticSink::Error(const std::string& inFilePath,
                              const std::string& inMessage,
                              const HTNDiagnosticRecovery inRecovery,
                              const HTNSourceRange& inRange)
{
    Report(HTNDiagnosticSeverity::Error, inRecovery, inFilePath, inRange, inMessage);
}

void HTNDiagnosticSink::Warning(const std::string& inFilePath,
                                const std::string& inMessage,
                                const HTNSourceRange& inRange)
{
    Report(HTNDiagnosticSeverity::Warning, HTNDiagnosticRecovery::Recoverable, inFilePath, inRange, inMessage);
}

bool HTNDiagnosticSink::HasErrors() const
{
    return std::any_of(mDiagnostics.begin(), mDiagnostics.end(), [](const HTNDiagnostic& inDiagnostic) {
        return inDiagnostic.Severity == HTNDiagnosticSeverity::Error;
    });
}

bool HTNDiagnosticSink::HasFatalErrors() const
{
    return std::any_of(mDiagnostics.begin(), mDiagnostics.end(), [](const HTNDiagnostic& inDiagnostic) {
        return inDiagnostic.Severity == HTNDiagnosticSeverity::Error && inDiagnostic.Recovery == HTNDiagnosticRecovery::Fatal;
    });
}

size_t HTNDiagnosticSink::GetErrorCount() const
{
    return static_cast<size_t>(std::count_if(mDiagnostics.begin(), mDiagnostics.end(), [](const HTNDiagnostic& inDiagnostic) {
        return inDiagnostic.Severity == HTNDiagnosticSeverity::Error;
    }));
}

const HTNDiagnostic* HTNDiagnosticSink::GetFirstError() const
{
    const auto It = std::find_if(mDiagnostics.begin(), mDiagnostics.end(), [](const HTNDiagnostic& inDiagnostic) {
        return inDiagnostic.Severity == HTNDiagnosticSeverity::Error;
    });
    return It != mDiagnostics.end() ? &(*It) : nullptr;
}
