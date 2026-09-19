// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#ifdef HTN_DEBUG_DECOMPOSITION

#include "imgui.h"

#include <cstdint>
#include <string>

enum class HTNGeneratedDebuggerTheme : std::uint8_t
{
    Classic,
    StudioDark,
    Ocean,
    Guerrilla,
    Custom
};

struct HTNGeneratedDebuggerPalette
{
    ImVec4 Fail;
    ImVec4 Success;
    ImVec4 NoResult;
    ImVec4 Parameter;
    ImVec4 Argument;
    ImVec4 AnyArgument;
    ImVec4 CallExpression;
    ImVec4 FailedBranch;
    bool ShowFailedFactMarker;
};

namespace HTNGeneratedImGuiHelpers
{
const HTNGeneratedDebuggerPalette& GetDebuggerPalette();
ImVec4 GetResultColor(bool inResult);
ImVec4 GetVariableColor(const std::string& inVariableID);
void SetTreeNodeOpen(const std::string& inTreeNodeLabel, bool inIsOpen);
void RenderDebuggerThemeSelector();
}

#endif
