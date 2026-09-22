// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include <string>

inline constexpr char HTNVariablePrefix = '?';
inline constexpr char HTNConstantPrefix = '@';
inline constexpr char HTNAxiomCallPrefix = '#';
inline constexpr char HTNDeferredCallPrefix = '&';
inline constexpr char HTNPrimitiveTaskPrefix = '!';
inline constexpr char HTNDeclarationPrefix = ':';

inline std::string HTNMakeAxiomPrefixInTaskDiagnostic()
{
    return "'" + std::string(1u, HTNAxiomCallPrefix) +
        "' is reserved for axiom calls; use '" +
        std::string(1u, HTNDeferredCallPrefix) + "' for deferred method calls";
}
