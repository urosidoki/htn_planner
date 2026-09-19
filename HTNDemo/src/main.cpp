// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "AI/AIHTNDemoWandererAgent.h"
#include "AI/AIHtnDaemonDemoTest.h"
#include "AI/AIHtnListDaemon.h"
#include "Core/HTNCallTermBinding.h"
#include "Core/HTNFileHelpers.h"
#include "Core/HTNTask.h"
#include "Core/HtnSymbol.h"
#include "HTNCoreMinimal.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "UI/HTNMemoryDebugPanel.h"
#include "UI/HTNNPCSimulationPanel.h"
#ifdef HTN_DEBUG_DECOMPOSITION
#include "Translator/HTNGeneratedDebugger.h"
#include "UI/HTNGeneratedDebuggerView.h"
#include "UI/HTNGeneratedImGuiHelpers.h"
#include "UI/HTNWorldStateView.h"
#endif
#include "SDL.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "optick.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

extern "C" const HTNGeneratedPlannerDefinition* CreateAAACombatNPCHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateAtomListDemoHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateEliteNinjaHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateGruntHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateNormalNinjaHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateCalltermsHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateComplexScenarioHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateHumanHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateNestedCallsHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateIncludeDemoHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateWandererHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateBuiltinComparisonsDemoHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateHierarchicalBacktrackingHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateRuntimeBacktrackingDemoHTN_GetDefinition(void);

namespace
{
struct Domain
{
    const char*                          Name;
    const HTNGeneratedPlannerDefinition* Definition;
    const char*                          Source;
    std::vector<const char*>             Methods;
};
// clang-format off
const Domain kDomains[] = {
    {
        "AAACombatNPC",
        CreateAAACombatNPCHTN_GetDefinition(),
        "AAACombatNPC",
        {"run"}
    },
    {
        "AtomListDemo",
        CreateAtomListDemoHTN_GetDefinition(),
        "atom_list_demo",
        {
            "show_atom_list",
            "split_list_basic",
            "split_list_single_element",
            "split_list_empty_fails",
            "split_list_bound_outputs",
            "split_list_rollback",
            "split_list_front_basic",
            "split_list_back_basic",
            "split_list_back_bound_outputs"
        }
    },
    {
        "EliteNinja",
        CreateEliteNinjaHTN_GetDefinition(),
        "EliteNinja",
        {"run"}
    },
    {
        "Grunt",
        CreateGruntHTN_GetDefinition(),
        "Grunt",
        {"run"}
    },
    {
        "NormalNinja",
        CreateNormalNinjaHTN_GetDefinition(),
        "NormalNinja",
        {"run"}
    },
    {
        "CallTermsDemo",
        CreateCalltermsHTN_GetDefinition(),
        "callterms",
        {
            "test_callterms",
            "callterm_creates_fact_visible_immediately",
            "callterm_world_state_mutation_survives_backtracking"
        }
    },
    {
        "ComplexScenario",
        CreateComplexScenarioHTN_GetDefinition(),
        "complex_scenario",
        {"run_scenario"}
    },
    {
        "Human",
        CreateHumanHTN_GetDefinition(),
        "human",
        {"behave", "behave_upper_body"}
    },
    {
        "NestedCallsDemo",
        CreateNestedCallsHTN_GetDefinition(),
        "nested_calls",
        {"test_nested_calls"}
    },
    {
        "IncludeDemo",
        CreateIncludeDemoHTN_GetDefinition(),
        "include_demo",
        {"run_include_demo"}
    },
    {
        "Wanderer",
        CreateWandererHTN_GetDefinition(),
        "Wanderer",
        {"run"}
    },
    {
        "BuiltinComparisonsDemo",
        CreateBuiltinComparisonsDemoHTN_GetDefinition(),
        "BuiltinComparisonsDemo",
        {"run"}
    },
    {
        "HierarchicalBacktracking",
        CreateHierarchicalBacktrackingHTN_GetDefinition(),
        "hierarchical_backtracking",
        {
            "validate_parent_guard",
            "validate_child_guard",
            "validate_fact_backtracking",
            "validate_axiom_backtracking"
        }
    },
    {
        "RuntimeBacktrackingDemo",
        CreateRuntimeBacktrackingDemoHTN_GetDefinition(),
        "RuntimeBacktrackingDemo",
        {
            "demo_fact_alternatives",
            "demo_axiom_alternatives",
            "demo_hierarchical_branches",
            "demo_direct_branch_fallback"
        }
    }
};
// clang-format on
constexpr int kDomainCount = sizeof(kDomains) / sizeof(kDomains[0]);

void BindCalls(HTNCallTermRegistry& r)
{
    AIHtnListDaemon::BindCallTerms(r);
    AIHTNDemoWandererAgent::BindCallTerms(r);
    AIHtnDaemonDemoTest::BindCallTerms(r);
    r.Bind("binded_function_with_args", [](const HTNCallTermArguments& a) { return a.size() == 1u; });
    r.Bind("get_health", [](const HTNCallTermArguments&) { return 50; });
    r.Bind("get_max_speed", [](const HTNCallTermArguments&) { return 1.0f; });
    r.Bind("lt", [](const HTNCallTermArguments& a) {
        return a.size() == 2u && HTNAtomIsType<int32>(a[0]) && HTNAtomIsType<int32>(a[1]) &&
               HTNAtomGetValue<int32>(a[0]) < HTNAtomGetValue<int32>(a[1]);
    });
    r.Bind("inc", [](const HTNCallTermArguments& a) { return a.size() == 1u && HTNAtomIsType<int32>(a[0]) ? HTNAtomGetValue<int32>(a[0]) + 1 : 0; });
    r.Bind("add", [](const HTNCallTermArguments& a) {
        return a.size() == 2u && HTNAtomIsType<int32>(a[0]) && HTNAtomIsType<int32>(a[1])
                   ? HTNAtomGetValue<int32>(a[0]) + HTNAtomGetValue<int32>(a[1])
                   : 0;
    });
    r.Bind("mul", [](const HTNCallTermArguments& a) {
        return a.size() == 2u && HTNAtomIsType<int32>(a[0]) && HTNAtomIsType<int32>(a[1])
                   ? HTNAtomGetValue<int32>(a[0]) * HTNAtomGetValue<int32>(a[1])
                   : 0;
    });
}
std::vector<std::filesystem::path> FindWorldStates()
{
    std::vector<std::filesystem::path> v;
    std::error_code                    e;
    auto                               root = HTNFileHelpers::MakeAbsolutePath("WorldStates");
    for (std::filesystem::recursive_directory_iterator i(root, e), end; i != end && !e; i.increment(e))
        if (i->is_regular_file() && i->path().extension() == ".worldstate")
            v.push_back(i->path());
    std::sort(v.begin(), v.end());
    return v;
}
std::string WSName(const std::filesystem::path& p)
{
    return p.filename().string();
}
int BestWS(const Domain& d, const std::vector<std::filesystem::path>& v)
{
    for (int i = 0; i < (int)v.size(); ++i)
    {
        auto s = v[i].stem().string();
        if (s == d.Source || s.starts_with(std::string(d.Source) + "_"))
            return i;
    }
    return v.empty() ? -1 : 0;
}
std::string FormatTask(const HTNAtomOwner& t)
{
    std::ostringstream s;
    auto*              h = HTNGetTaskHead(t);
    s << (h ? h->GetString() : "<invalid>");
    for (uint32 i = 0; i < HTNGetTaskArgumentCount(t); ++i)
        s << ' ' << HTNAtomToString(HTNGetTaskArgument(t, i), true);
    return s.str();
}
const char* ModeName(HTNBacktrackingMode m)
{
    switch (m)
    {
    case HTN_BACKTRACKING_NONE:
        return "None";
    case HTN_BACKTRACKING_FACTS_AND_AXIOMS:
        return "Facts and axioms";
    case HTN_BACKTRACKING_BRANCHES:
        return "Branches";
    case HTN_BACKTRACKING_ALL:
        return "All";
    }
    return "Unknown";
}
} // namespace

int main(int, char**)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        std::printf("SDL error: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window*   window   = SDL_CreateWindow("HTN Generated Planner Demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1440, 900,
                                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED) : nullptr;
    if (!window || !renderer)
    {
        std::printf("SDL error: %s\n", SDL_GetError());
        if (renderer)
            SDL_DestroyRenderer(renderer);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    HTNCallTermRegistry calls;
    BindCalls(calls);
    HTNDatabaseHook                  database;
    std::unique_ptr<HTNPlannerHook>  planner;
    std::unique_ptr<HTNPlanningUnit> unit;
    auto                             worldStates = FindWorldStates();
    int                              domainIndex = 6, methodIndex = 0, worldStateIndex = BestWS(kDomains[6], worldStates), loadedWS = -1;
    HTNBacktrackingMode              mode     = HTN_BACKTRACKING_ALL;
    bool                             wsLoaded = false, hasResult = false, succeeded = false;
    double                           elapsed = 0;
    std::vector<std::string>         plan;
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNGeneratedDebugger     debugger;
    HTNGeneratedDebuggerView debugView;
    ImGuiTextFilter          wsFilter;
    debugger.SetEnabled(true);
#endif
    HTNNPCSimulationPanel simulation(CreateWandererHTN_GetDefinition(), calls);
    HTNMemoryDebugPanel   memory;
    auto                  reload = [&]() {
        const Domain& d = kDomains[domainIndex];
        planner         = std::make_unique<HTNPlannerHook>(database.GetWorldState(), calls);
        unit.reset();
        if (planner->SetGeneratedPlannerDefinition(d.Definition))
        {
            methodIndex = std::clamp(methodIndex, 0, (int)d.Methods.size() - 1);
            unit        = std::make_unique<HTNPlanningUnit>(database, *planner, d.Methods[methodIndex]);
            unit->SetBacktrackingMode(mode);
#ifdef HTN_DEBUG_DECOMPOSITION
            unit->SetGeneratedDebugger(&debugger);
            debugView.ClearSelection();
#endif
        }
        worldStateIndex = BestWS(d, worldStates);
        loadedWS        = -1;
        wsLoaded        = false;
        hasResult       = false;
        plan.clear();
    };
    reload();
    bool done = false;
    while (!done)
    {
        OPTICK_FRAME("MainThread");
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT ||
                (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window)))
                done = true;
        }
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        simulation.Update(ImGui::GetIO().DeltaTime);
        ImGui::Begin("HTN Generated Planner");
        if (ImGui::BeginTabBar("tabs"))
        {
            if (ImGui::BeginTabItem("Domain Runner"))
            {
                const Domain& d = kDomains[domainIndex];
                if (ImGui::BeginCombo("Domain", d.Name))
                {
                    for (int i = 0; i < kDomainCount; ++i)
                        if (ImGui::Selectable(kDomains[i].Name, domainIndex == i))
                        {
                            domainIndex = i;
                            methodIndex = 0;
                            reload();
                        }
                    ImGui::EndCombo();
                }
                const Domain& selected = kDomains[domainIndex];
                if (ImGui::BeginCombo("Top level method", selected.Methods[methodIndex]))
                {
                    for (int i = 0; i < (int)selected.Methods.size(); ++i)
                        if (ImGui::Selectable(selected.Methods[i], methodIndex == i))
                        {
                            methodIndex = i;
                            reload();
                        }
                    ImGui::EndCombo();
                }
                if (ImGui::BeginCombo("Backtracking", ModeName(mode)))
                {
                    HTNBacktrackingMode modes[] = {HTN_BACKTRACKING_NONE, HTN_BACKTRACKING_FACTS_AND_AXIOMS, HTN_BACKTRACKING_BRANCHES,
                                                   HTN_BACKTRACKING_ALL};
                    for (auto m : modes)
                        if (ImGui::Selectable(ModeName(m), mode == m))
                        {
                            mode = m;
                            if (unit)
                                unit->SetBacktrackingMode(m);
                        }
                    ImGui::EndCombo();
                }
                std::string preview = worldStateIndex >= 0 ? WSName(worldStates[worldStateIndex]) : "No world states";
                if (ImGui::BeginCombo("World state", preview.c_str()))
                {
                    for (int i = 0; i < (int)worldStates.size(); ++i)
                        if (ImGui::Selectable(WSName(worldStates[i]).c_str(), worldStateIndex == i))
                            worldStateIndex = i;
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (ImGui::Button("Load") && worldStateIndex >= 0)
                {
                    wsLoaded  = database.ParseWorldStateFile(worldStates[worldStateIndex].string());
                    loadedWS  = wsLoaded ? worldStateIndex : -1;
                    hasResult = false;
                }
                bool canRun = unit && wsLoaded && loadedWS == worldStateIndex;
                if (!canRun)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Run generated planner"))
                {
#ifdef HTN_DEBUG_DECOMPOSITION
                    debugger.Reset();
                    debugView.ResetExpansionOnNextRender();
#endif
                    auto start  = std::chrono::steady_clock::now();
                    auto status = unit->DecomposeTopLevelMethod(HtnSymbol::sGetSymbol(selected.Methods[methodIndex]));
                    elapsed     = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    succeeded   = status == HTN_DECOMPOSITION_SUCCEEDED;
                    hasResult   = true;
                    plan.clear();
                    if (succeeded)
                    {
                        const auto& output = unit->GetLastDecomposition().GetResult();
                        for (int32 i = 0; i < output.GetListSize(); ++i)
                            plan.push_back(FormatTask(HTNAtomOwner(output.GetListElement((uint32)i))));
                    }
                }
                if (!canRun)
                    ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled("World state: %s", wsLoaded ? "loaded" : "load one to run");
                if (hasResult)
                {
                    ImGui::Separator();
                    ImGui::TextColored(succeeded ? ImVec4(.4f, .9f, .5f, 1) : ImVec4(.95f, .35f, .35f, 1), "%s in %.3f ms",
                                       succeeded ? "Success" : "Failure", elapsed);
                    for (const auto& task : plan)
                        ImGui::BulletText("%s", task.c_str());
                }
#ifdef HTN_DEBUG_DECOMPOSITION
                ImGui::SeparatorText("World state");
                wsFilter.Draw("Filter");
                if (ImGui::BeginChild("world", ImVec2(0, 180), true, ImGuiWindowFlags_HorizontalScrollbar))
                {
                    if (wsLoaded)
                        RenderHTNWorldState(database.GetWorldState(), wsFilter);
                    else
                        ImGui::TextDisabled("Load a world state.");
                }
                ImGui::EndChild();
                ImGui::SeparatorText("Generated event debugger");
                HTNGeneratedImGuiHelpers::RenderDebuggerThemeSelector();
                ImGui::SameLine();
                debugView.RenderDisplayModeSelector();
                if (ImGui::BeginTable("debug", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
                {
                    ImGui::TableNextColumn();
                    debugView.RenderTree(debugger, false);
                    ImGui::TableNextColumn();
                    debugView.RenderWatch(debugger);
                    ImGui::EndTable();
                }
#endif
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("NPC Simulation"))
            {
                simulation.Render();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Memory"))
            {
                memory.Render();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 45, 55, 65, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
