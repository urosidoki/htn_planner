// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#ifdef HTN_DEBUG_DECOMPOSITION

#include "WorldState/HTNWorldState.h"
#include "imgui.h"

#include <string>

inline void RenderHTNWorldState(const HTNWorldState& inWorldState, const ImGuiTextFilter& inFilter)
{
    for (const HTNFact& Fact : inWorldState.GetFacts())
    {
        const std::string& FactName = Fact.first->GetString();
        for (const HTNFactArgumentsTable& Table : Fact.second)
        {
            for (const HTNFactArguments& Arguments : Table.GetFactArgumentsCollection())
            {
                std::string Row = FactName;
                for (const HTNAtom& Argument : Arguments)
                    Row += " " + HTNAtomToString(Argument, true);
                if (inFilter.PassFilter(Row.c_str()))
                    ImGui::TextUnformatted(Row.c_str());
            }
        }
    }
}

#endif
