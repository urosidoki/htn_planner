// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNMissingCallTerm.h"
#include <cstdio>

inline void ReportDemoMissingCallTerm(const char* inBackend, const HTNMissingCallTermInfo& inInfo)
{
    const char* Reason = "unknown reason";
    switch (inInfo.Reason)
    {
    case HTNMissingCallTermReason::NotRegistered: Reason = "not registered"; break;
    case HTNMissingCallTermReason::MissingBinding: Reason = "no callable bound"; break;
    case HTNMissingCallTermReason::MissingInstance: Reason = "daemon instance missing"; break;
    }
    std::fprintf(stderr, "[%s] Missing callterm '%s': %s (daemon: %s, domain: %s, source: %s:%u:%u)\n",
                 inBackend, inInfo.Name ? inInfo.Name : "<unknown>", Reason,
                 inInfo.DaemonID ? inInfo.DaemonID : "-",
                 inInfo.Source.domain ? inInfo.Source.domain : "<unknown>",
                 inInfo.Source.file ? inInfo.Source.file : "<unavailable>",
                 inInfo.Source.line, inInfo.Source.column);
}

inline void ReportGeneratedDemoMissingCallTerm(void*, const HTNMissingCallTermInfo* inInfo)
{
    ReportDemoMissingCallTerm("Generated", *inInfo);
}
