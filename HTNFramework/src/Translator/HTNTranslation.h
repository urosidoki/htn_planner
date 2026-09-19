// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Domain/Diagnostics/HTNDiagnostic.h"
#include "Translator/HTNCCodeGenerator.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Complete set of inputs required to translate one HTN domain into generated C.
// Frontends such as HTNTranslator and HTNEditor should build this request instead
// of reproducing domain-loading or code-generation logic themselves.
struct HTNTranslationRequest
{
    std::filesystem::path DomainPath;
    std::filesystem::path OutputDirectory;
    std::string EntryPointName;

    // Controls whether the generated planner may allocate overflow storage when
    // its inline backtracking capacity is exhausted.
    HTNGeneratedBacktrackingPolicy BacktrackingPolicy = HTNGeneratedBacktrackingPolicy::FixedWithOverflow;

    // Controls whether generated code includes runtime-selectable semantic
    // backtracking. Disabled keeps full backtracking semantics fixed and emits
    // no runtime-mode checks.
    HTNGeneratedRuntimeBacktrackingSupport RuntimeBacktrackingSupport = HTNGeneratedRuntimeBacktrackingSupport::Disabled;

    // Number of pending continuations reserved inline in the generated execution
    // storage. FixedCapacity treats this as a hard limit; FixedWithOverflow may
    // exceed it by allocating overflow storage.
    uint32_t BacktrackingCapacity = 32u;
};

// Identifies which stage prevented a translation request from completing.
enum class HTNTranslationFailure
{
    // Translation completed successfully; ErrorMessage is empty.
    None,

    // The request itself is invalid, for example an empty entry point or zero capacity.
    InvalidOptions,

    // The domain could not be loaded, linked or semantically validated. Source diagnostics
    // are returned in HTNTranslationResult::Diagnostics whenever available.
    DomainLoad,

    // The domain was valid, but generated C could not be produced or written.
    CodeGeneration
};

// Result returned by the shared translation API. Diagnostics come from the
// domain loader/semantic pipeline; ErrorMessage is reserved for request or
// generation errors that do not naturally carry a source range.
struct HTNTranslationResult
{
    bool Succeeded = false;
    HTNTranslationFailure Failure = HTNTranslationFailure::None;
    std::string ErrorMessage;
    std::vector<HTNDiagnostic> Diagnostics;

    std::string DomainId;
    size_t LinkedSourceFileCount = 0u;
    std::filesystem::path OutputSourcePath;
};

// Loads, links and validates the requested domain, then generates C using the
// supplied options. This is the single translation entry point shared by CLI
// and graphical frontends.
bool HTNTranslateDomain(const HTNTranslationRequest& inRequest, HTNTranslationResult& outResult);
