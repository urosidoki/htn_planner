// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Domain/Source/HTNSourceText.h"

#include <string>

enum class HTNDiagnosticSeverity
{
    Info,
    Warning,
    Error
};

enum class HTNDiagnosticRecovery
{
    Recoverable,
    Dependent,
    Fatal
};

struct HTNDiagnostic
{
    HTNDiagnosticSeverity Severity = HTNDiagnosticSeverity::Error;
    HTNDiagnosticRecovery Recovery = HTNDiagnosticRecovery::Recoverable;
    std::string FilePath;
    HTNSourceRange Range;
    std::string Message;
};
