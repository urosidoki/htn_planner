// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "UI/HTNGeneratedImGuiHelpers.h"

#ifdef HTN_DEBUG_DECOMPOSITION

#include "imgui_internal.h"

namespace
{
constexpr HTNGeneratedDebuggerPalette kPalettes[] = {
    {{1.00f, 0.50f, 0.50f, 1.00f}, {0.00f, 1.00f, 0.00f, 1.00f}, {0.60f, 0.60f, 0.60f, 1.00f}, {1.00f, 0.60f, 0.00f, 1.00f}, {1.00f, 1.00f, 0.00f, 1.00f}, {0.60f, 0.60f, 0.60f, 1.00f}, {1.00f, 1.00f, 0.00f, 1.00f}, {1.00f, 1.00f, 1.00f, 1.00f}, false},
    {{0.88f, 0.42f, 0.46f, 1.00f}, {0.48f, 0.80f, 0.53f, 1.00f}, {0.50f, 0.52f, 0.56f, 1.00f}, {0.90f, 0.63f, 0.35f, 1.00f}, {0.42f, 0.71f, 1.00f, 1.00f}, {0.55f, 0.58f, 0.63f, 1.00f}, {0.76f, 0.60f, 0.96f, 1.00f}, {1.00f, 1.00f, 1.00f, 1.00f}, false},
    {{0.96f, 0.45f, 0.48f, 1.00f}, {0.43f, 0.82f, 0.67f, 1.00f}, {0.48f, 0.57f, 0.63f, 1.00f}, {0.96f, 0.70f, 0.38f, 1.00f}, {0.35f, 0.78f, 0.94f, 1.00f}, {0.47f, 0.62f, 0.68f, 1.00f}, {0.69f, 0.65f, 0.95f, 1.00f}, {1.00f, 1.00f, 1.00f, 1.00f}, false},
    {{1.00f, 0.24f, 0.16f, 1.00f}, {0.22f, 0.86f, 0.46f, 1.00f}, {0.04f, 0.71f, 0.80f, 1.00f}, {1.00f, 0.77f, 0.00f, 1.00f}, {1.00f, 0.80f, 0.00f, 1.00f}, {0.04f, 0.71f, 0.80f, 1.00f}, {0.04f, 0.71f, 0.80f, 1.00f}, {0.04f, 0.71f, 0.80f, 1.00f}, true}
};

constexpr const char* kThemeNames[] = {"Classic", "Studio Dark", "Ocean", "Guerrilla"};
HTNGeneratedDebuggerTheme gTheme = HTNGeneratedDebuggerTheme::StudioDark;
HTNGeneratedDebuggerPalette gPalette = kPalettes[1];
}

namespace HTNGeneratedImGuiHelpers
{
const HTNGeneratedDebuggerPalette& GetDebuggerPalette() { return gPalette; }
ImVec4 GetResultColor(const bool inResult) { return inResult ? gPalette.Success : gPalette.Fail; }

ImVec4 GetVariableColor(const std::string& inVariableID)
{
    if (inVariableID.starts_with("?inp_") || inVariableID.starts_with("?out_") || inVariableID.starts_with("?io_"))
        return gPalette.Parameter;
    if (inVariableID == "?")
        return gPalette.AnyArgument;
    return gPalette.Argument;
}

void SetTreeNodeOpen(const std::string& inTreeNodeLabel, const bool inIsOpen)
{
    ImGuiWindow* Window = ImGui::GetCurrentWindow();
    ImGui::TreeNodeSetOpen(Window->GetID(inTreeNodeLabel.c_str()), inIsOpen);
}

void RenderDebuggerThemeSelector()
{
    const int ThemeIndex = static_cast<int>(gTheme);
    const char* Preview = ThemeIndex < 4 ? kThemeNames[ThemeIndex] : "Custom";
    if (ImGui::BeginCombo("Theme", Preview))
    {
        for (int I = 0; I < 4; ++I)
        {
            const bool Selected = ThemeIndex == I;
            if (ImGui::Selectable(kThemeNames[I], Selected))
            {
                gTheme = static_cast<HTNGeneratedDebuggerTheme>(I);
                gPalette = kPalettes[I];
            }
            if (Selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}
}

#endif
