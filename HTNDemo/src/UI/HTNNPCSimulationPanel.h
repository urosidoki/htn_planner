// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "World/DemoGridTerrain.h"

#include <cstdint>
#include <memory>
#include <vector>

class AIHTNDemoWandererAgent;
class HTNCallTermRegistry;
struct HTNGeneratedPlannerDefinition;
struct HTNNPCDecompositionViewState;

class HTNNPCSimulationPanel
{
public:
    HTNNPCSimulationPanel(const HTNGeneratedPlannerDefinition* inWandererDefinition,
                          const HTNCallTermRegistry& inCallTermRegistry);
    ~HTNNPCSimulationPanel();

    HTNNPCSimulationPanel(const HTNNPCSimulationPanel&) = delete;
    HTNNPCSimulationPanel& operator=(const HTNNPCSimulationPanel&) = delete;

    void Update(float inDeltaTime);
    void Render();
#ifdef HTN_HOT_RELOAD_DEMO
    void ReleaseGeneratedPlanner();
    bool AttachGeneratedPlanner(const HTNGeneratedPlannerDefinition* inDefinition);
#endif

private:
    void SpawnNPC();
    void SpawnNPCs(int inCount);
    void RemoveSelectedNPC();
    void ResetSimulation();
    void RenderToolbar();
    void RenderNPCList();
    void RenderWorldMap();
    void RenderSelectedNPC();
    void RenderSelectedNPCWorldState(const AIHTNDemoWandererAgent& inNPC);
    void RenderSelectedNPCPlan(const AIHTNDemoWandererAgent& inNPC);
    void RenderSelectedNPCDecomposition(const AIHTNDemoWandererAgent& inNPC);
    void RenderSelectedNPCHistory(const AIHTNDemoWandererAgent& inNPC);

    const HTNGeneratedPlannerDefinition* mWandererDefinition = nullptr;
    const HTNCallTermRegistry& mCallTermRegistry;
    DemoGridTerrain mTerrain;
    std::vector<std::unique_ptr<AIHTNDemoWandererAgent>> mNPCs;
    std::uint32_t mNextNPCId = 1u;
    int mSelectedNPCIndex = -1;
    bool mRunning = true;
    float mTimeScale = 1.0f;
    float mSimulationAgeSeconds = 0.0f;
    float mLeftPaneWidth = 0.0f;
    float mNPCListPanelHeight = 0.0f;
    std::unique_ptr<HTNNPCDecompositionViewState> mDecompositionViewState;
};
