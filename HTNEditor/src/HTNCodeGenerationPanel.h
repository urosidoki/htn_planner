// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Translator/HTNTranslation.h"

#include <filesystem>
#include <string>

struct HTNCodeGenerationPanelState
{
    bool Open = false;
    std::string DomainPath;
    std::string OutputDirectory;
    std::string EntryPointName;
    HTNGeneratedBacktrackingPolicy BacktrackingPolicy = HTNGeneratedBacktrackingPolicy::FixedWithOverflow;
    HTNGeneratedRuntimeBacktrackingSupport RuntimeBacktrackingSupport = HTNGeneratedRuntimeBacktrackingSupport::Disabled;
    int BacktrackingCapacity = 32;

    bool HasResult = false;
    HTNTranslationResult LastResult;
};

// Opens the code-generation panel and, when possible, initializes it from the
// currently active domain. Existing user-entered settings are preserved unless
// a new suggested domain is supplied explicitly.
void OpenHTNCodeGenerationPanel(HTNCodeGenerationPanelState& inOutState,
                                const std::filesystem::path& inSuggestedDomain = {});

// Renders the code-generation frontend. The panel calls the same translation
// API used by HTNTranslator; it never launches the CLI as a child process.
void RenderHTNCodeGenerationPanel(HTNCodeGenerationPanelState& inOutState);
