// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNCodeGenerationPanel.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <commdlg.h>
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace
{
std::string MakeDefaultEntryPoint(const std::filesystem::path& inDomainPath)
{
    const std::string Stem = inDomainPath.stem().string();
    std::string Name;
    Name.reserve(Stem.size());

    bool CapitalizeNext = true;
    for (const char Character : Stem)
    {
        const unsigned char UnsignedCharacter = static_cast<unsigned char>(Character);
        if (!std::isalnum(UnsignedCharacter))
        {
            CapitalizeNext = true;
            continue;
        }

        if (CapitalizeNext)
            Name.push_back(static_cast<char>(std::toupper(UnsignedCharacter)));
        else
            Name.push_back(Character);
        CapitalizeNext = false;
    }

    return Name.empty() ? std::string{} : "Create" + Name + "HTN";
}

void SetDomainPath(HTNCodeGenerationPanelState& inOutState, const std::filesystem::path& inDomainPath)
{
    if (inDomainPath.empty())
        return;

    inOutState.DomainPath = inDomainPath.string();
    inOutState.OutputDirectory = inDomainPath.parent_path().string();
    inOutState.EntryPointName = MakeDefaultEntryPoint(inDomainPath);
    inOutState.HasResult = false;
}

#ifdef _WIN32
std::filesystem::path BrowseForDomain()
{
    char Filename[MAX_PATH] = {};
    OPENFILENAMEA Dialog{};
    Dialog.lStructSize = sizeof(Dialog);
    Dialog.lpstrFile = Filename;
    Dialog.nMaxFile = MAX_PATH;
    Dialog.lpstrFilter = "HTN Domains (*.domain)\0*.domain\0All Files (*.*)\0*.*\0";
    Dialog.nFilterIndex = 1;
    Dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    Dialog.lpstrDefExt = "domain";
    return GetOpenFileNameA(&Dialog) ? std::filesystem::path(Filename) : std::filesystem::path{};
}
#endif

const char* BacktrackingPolicyName(const HTNGeneratedBacktrackingPolicy inPolicy)
{
    switch (inPolicy)
    {
    case HTNGeneratedBacktrackingPolicy::FixedWithOverflow: return "Fixed with overflow";
    case HTNGeneratedBacktrackingPolicy::FixedCapacity: return "Fixed capacity";
    }
    return "Unknown";
}

const char* RuntimeBacktrackingSupportName(const HTNGeneratedRuntimeBacktrackingSupport inSupport)
{
    switch (inSupport)
    {
    case HTNGeneratedRuntimeBacktrackingSupport::Disabled: return "Disabled";
    case HTNGeneratedRuntimeBacktrackingSupport::Enabled: return "Enabled";
    }
    return "Unknown";
}

void RenderBacktrackingPolicyDescription(const HTNGeneratedBacktrackingPolicy inPolicy)
{
    ImGui::PushTextWrapPos(0.0f);
    switch (inPolicy)
    {
    case HTNGeneratedBacktrackingPolicy::FixedWithOverflow:
        ImGui::TextUnformatted(
            "Reserves the configured number of pending continuations inline. If that capacity is exhausted, "
            "the planner allocates overflow storage dynamically. This avoids a hard planning limit, but planning "
            "may allocate from the heap in unusually deep or highly branching decompositions.");
        break;

    case HTNGeneratedBacktrackingPolicy::FixedCapacity:
        ImGui::TextUnformatted(
            "Stores all pending continuations in fixed generated execution storage. Backtracking never allocates "
            "overflow memory from the heap. If the configured capacity is exhausted, the current decomposition "
            "fails instead of allocating more storage.");
        break;
    }
    ImGui::PopTextWrapPos();
}

void RenderDiagnostics(const HTNTranslationResult& inResult)
{
    if (!inResult.ErrorMessage.empty())
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(inResult.ErrorMessage.c_str());
        ImGui::PopTextWrapPos();
    }

    for (const HTNDiagnostic& Diagnostic : inResult.Diagnostics)
    {
        const int Line = std::max(1, Diagnostic.Range.Begin.Line);
        const int Column = std::max(1, Diagnostic.Range.Begin.Column);
        const std::string FileName = Diagnostic.FilePath.empty()
            ? std::string("<domain>")
            : std::filesystem::path(Diagnostic.FilePath).filename().string();

        ImGui::PushTextWrapPos(0.0f);
        ImGui::Text("%s(%d,%d): %s", FileName.c_str(), Line, Column, Diagnostic.Message.c_str());
        ImGui::PopTextWrapPos();
    }
}
}

void OpenHTNCodeGenerationPanel(HTNCodeGenerationPanelState& inOutState,
                                const std::filesystem::path& inSuggestedDomain)
{
    inOutState.Open = true;
    if (!inSuggestedDomain.empty())
        SetDomainPath(inOutState, inSuggestedDomain);
}

void RenderHTNCodeGenerationPanel(HTNCodeGenerationPanelState& inOutState)
{
    if (!inOutState.Open)
        return;

    ImGui::SetNextWindowSize(ImVec2(720.0f, 580.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Generate Code", &inOutState.Open))
    {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Domain");
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputText("##codegen_domain", &inOutState.DomainPath);
#ifdef _WIN32
    ImGui::SameLine();
    if (ImGui::Button("Browse...##domain"))
    {
        const std::filesystem::path Path = BrowseForDomain();
        if (!Path.empty())
            SetDomainPath(inOutState, Path);
    }
#endif

    ImGui::TextUnformatted("Entry point");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##codegen_entry_point", &inOutState.EntryPointName);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Name of the generated C function used to create this HTN domain.");

    ImGui::TextUnformatted("Output directory");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##codegen_output_directory", &inOutState.OutputDirectory);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The generated file is named <domain>.generated.c inside this directory.");

    ImGui::Spacing();
    ImGui::SeparatorText("Backtracking");

    const char* CurrentPolicyName = BacktrackingPolicyName(inOutState.BacktrackingPolicy);
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("Policy", CurrentPolicyName))
    {
        constexpr HTNGeneratedBacktrackingPolicy Policies[] = {
            HTNGeneratedBacktrackingPolicy::FixedWithOverflow,
            HTNGeneratedBacktrackingPolicy::FixedCapacity
        };

        for (const HTNGeneratedBacktrackingPolicy Policy : Policies)
        {
            const bool Selected = Policy == inOutState.BacktrackingPolicy;
            if (ImGui::Selectable(BacktrackingPolicyName(Policy), Selected))
                inOutState.BacktrackingPolicy = Policy;
            if (Selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    RenderBacktrackingPolicyDescription(inOutState.BacktrackingPolicy);

    const char* CurrentRuntimeSupportName = RuntimeBacktrackingSupportName(inOutState.RuntimeBacktrackingSupport);
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("Runtime backtracking support", CurrentRuntimeSupportName))
    {
        constexpr HTNGeneratedRuntimeBacktrackingSupport SupportModes[] = {
            HTNGeneratedRuntimeBacktrackingSupport::Disabled,
            HTNGeneratedRuntimeBacktrackingSupport::Enabled
        };

        for (const HTNGeneratedRuntimeBacktrackingSupport Support : SupportModes)
        {
            const bool Selected = Support == inOutState.RuntimeBacktrackingSupport;
            if (ImGui::Selectable(RuntimeBacktrackingSupportName(Support), Selected))
                inOutState.RuntimeBacktrackingSupport = Support;
            if (Selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Disabled keeps full backtracking semantics fixed in generated code and emits no runtime-mode checks. "
            "Enabled generates support for selecting HTNBacktrackingMode at runtime.");
    }

    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputInt("Capacity", &inOutState.BacktrackingCapacity, 1, 8);
    if (inOutState.BacktrackingCapacity < 1)
        inOutState.BacktrackingCapacity = 1;
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(
            "Maximum number of pending continuations stored inline. In Fixed capacity mode this is a hard limit; "
            "Fixed with overflow can exceed it using dynamic overflow storage.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool CanGenerate = !inOutState.DomainPath.empty() &&
                             !inOutState.EntryPointName.empty() &&
                             inOutState.BacktrackingCapacity > 0;

    if (!CanGenerate)
        ImGui::BeginDisabled();

    if (ImGui::Button("Generate Code", ImVec2(150.0f, 0.0f)))
    {
        HTNTranslationRequest Request;
        Request.DomainPath = std::filesystem::path(inOutState.DomainPath);
        Request.OutputDirectory = std::filesystem::path(inOutState.OutputDirectory);
        Request.EntryPointName = inOutState.EntryPointName;
        Request.BacktrackingPolicy = inOutState.BacktrackingPolicy;
        Request.RuntimeBacktrackingSupport = inOutState.RuntimeBacktrackingSupport;
        Request.BacktrackingCapacity = static_cast<uint32_t>(inOutState.BacktrackingCapacity);

        inOutState.LastResult = {};
        HTNTranslateDomain(Request, inOutState.LastResult);
        inOutState.HasResult = true;
    }

    if (!CanGenerate)
        ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("Generation reads the selected domain from disk.");

    if (inOutState.HasResult)
    {
        ImGui::Spacing();
        ImGui::SeparatorText(inOutState.LastResult.Succeeded ? "Generated successfully" : "Generation failed");
        if (inOutState.LastResult.Succeeded)
        {
            ImGui::Text("Domain: %s", inOutState.LastResult.DomainId.c_str());
            ImGui::Text("Linked source files: %zu", inOutState.LastResult.LinkedSourceFileCount);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::Text("Output: %s", inOutState.LastResult.OutputSourcePath.string().c_str());
            ImGui::PopTextWrapPos();
        }
        else
        {
            RenderDiagnostics(inOutState.LastResult);
        }
    }

    ImGui::End();
}
