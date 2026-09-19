// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Domain/Diagnostics/HTNDiagnostic.h"

#include <string>
#include <vector>

class HTNDiagnosticSink final
{
public:
    void Clear();

    void Report(const HTNDiagnostic& inDiagnostic);
    void Report(HTNDiagnosticSeverity inSeverity,
                HTNDiagnosticRecovery inRecovery,
                const std::string& inFilePath,
                const HTNSourceRange& inRange,
                const std::string& inMessage);

    void Error(const std::string& inFilePath,
               const std::string& inMessage,
               HTNDiagnosticRecovery inRecovery = HTNDiagnosticRecovery::Recoverable,
               const HTNSourceRange& inRange = {});

    void Warning(const std::string& inFilePath,
                 const std::string& inMessage,
                 const HTNSourceRange& inRange = {});

    bool HasErrors() const;
    bool HasFatalErrors() const;
    size_t GetErrorCount() const;

    const std::vector<HTNDiagnostic>& GetDiagnostics() const { return mDiagnostics; }
    const HTNDiagnostic* GetFirstError() const;

private:
    std::vector<HTNDiagnostic> mDiagnostics;
};
