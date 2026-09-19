// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "AI/AIHTNDemoGridTerrainDaemon.h"
#include "AI/AIHTNDemoPathfinder.h"
#include "AI/AIHTNDemoWanderer.h"
#include "Core/HTNTask.h"
#ifdef HTN_DEBUG_DECOMPOSITION
#include "Translator/HTNGeneratedDebugger.h"
#endif
#include "World/DemoWanderer.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class HTNDatabaseHook;
class HTNCallTermRegistry;
class HTNPlannerHook;
class HTNPlanningUnit;
class HTNWorldState;
class DemoGridTerrain;
struct HTNGeneratedPlannerDefinition;

// Long-lived demo NPC. The agent owns gameplay state plus the daemons that
// mirror/operate on it. The terrain itself is shared and referenced.
class AIHTNDemoWandererAgent
{
public:
    struct HistoryEntry
    {
        float AgeSeconds = 0.0f;
        std::string Text;
    };

    AIHTNDemoWandererAgent(
        std::uint32_t inId,
        const HTNGeneratedPlannerDefinition* inGeneratedDefinition,
        std::size_t inInitialWaypointIndex,
        DemoGridTerrain& inTerrain,
        const HTNCallTermRegistry& inCallTermRegistry);
    ~AIHTNDemoWandererAgent();

    AIHTNDemoWandererAgent(const AIHTNDemoWandererAgent&) = delete;
    AIHTNDemoWandererAgent& operator=(const AIHTNDemoWandererAgent&) = delete;

    static void BindCallTerms(HTNCallTermRegistry& ioRegistry);

    bool Initialize();
    void Update(float inDeltaTime);
#ifdef HTN_HOT_RELOAD_DEMO
    // Called only between simulation updates. The hook/world state/daemons stay alive.
    void ReleaseGeneratedPlanner();
    bool AttachGeneratedPlanner(const HTNGeneratedPlannerDefinition* inDefinition);
#endif

    [[nodiscard]] std::uint32_t GetId() const { return mId; }
    [[nodiscard]] float GetAgeSeconds() const { return mAgeSeconds; }
    [[nodiscard]] bool IsInitialized() const { return mInitialized; }
    [[nodiscard]] bool DidLastPlanSucceed() const { return mLastPlanSucceeded; }
    [[nodiscard]] std::uint64_t GetPlanCount() const { return mPlanCount; }
    [[nodiscard]] std::uint64_t GetCompletedTaskCount() const { return mCompletedTaskCount; }
    [[nodiscard]] double GetLastPlannerMilliseconds() const { return mLastPlannerMilliseconds; }
    [[nodiscard]] double GetTotalPlannerMilliseconds() const { return mTotalPlannerMilliseconds; }
    [[nodiscard]] double GetMaxPlannerMilliseconds() const { return mMaxPlannerMilliseconds; }
    [[nodiscard]] std::uint64_t GetMaxPlannerPlanIndex() const { return mMaxPlannerPlanIndex; }
    [[nodiscard]] float GetMaxPlannerAgeSeconds() const { return mMaxPlannerAgeSeconds; }
    [[nodiscard]] std::size_t GetMaxPlannerPrimitiveTaskCount() const { return mMaxPlannerPrimitiveTaskCount; }
    [[nodiscard]] bool DidMaxPlannerPlanSucceed() const { return mMaxPlannerPlanSucceeded; }
    [[nodiscard]] const DemoWanderer& GetWanderer() const { return mWanderer; }
    [[nodiscard]] DemoWanderer& GetWanderer() { return mWanderer; }
    [[nodiscard]] const std::vector<HTNAtomOwner>& GetCurrentPlan() const;
    [[nodiscard]] std::size_t GetCurrentTaskIndex() const;
    [[nodiscard]] const char* GetCurrentTaskName() const;
    [[nodiscard]] float GetCurrentTaskRemainingSeconds() const { return mCurrentTaskRemainingSeconds; }
    [[nodiscard]] bool GetCurrentNavigationPath(std::vector<Cell>& outPath) const;
    [[nodiscard]] const std::deque<HistoryEntry>& GetHistory() const { return mHistory; }
    [[nodiscard]] const HTNWorldState& GetWorldState() const;
    [[nodiscard]] std::string FormatTaskForDisplay(const HTNAtomOwner& inTask) const { return FormatTask(inTask); }
#ifdef HTN_DEBUG_DECOMPOSITION
    [[nodiscard]] const HTNGeneratedDebugger& GetLastGeneratedDebugger() const { return mGeneratedDebugger; }
#endif


private:
    static constexpr std::size_t MaxImmediatePlanningPasses = 4u;
    static constexpr float WaitTaskDurationSeconds = 0.15f;
    static constexpr float IdleTaskDurationSeconds = 0.4f;
    static constexpr std::size_t MaxHistoryEntries = 160u;

    void WriteWorldState();
    void TryPlan();
    void StartCurrentTask();
    void CompleteCurrentTask();
    void FinishPlan();
    void AddHistory(const std::string& inText);
    std::string FormatTask(const HTNAtomOwner& inTask) const;

    std::uint32_t mId = 0u;
    const HTNGeneratedPlannerDefinition* mGeneratedDefinition = nullptr;
    std::size_t mInitialWaypointIndex = 0u;
    const HTNCallTermRegistry& mCallTermRegistry;

    std::unique_ptr<HTNDatabaseHook> mDatabaseHook;
    std::unique_ptr<HTNPlannerHook> mPlannerHook;
    std::unique_ptr<HTNPlanningUnit> mPlanningUnit;
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNGeneratedDebugger mGeneratedDebugger;
#endif

    // Shared world data is referenced, never owned by an individual NPC.
    const DemoGridTerrain& mTerrain;

    // Persistent demo/game state first, then lightweight systems that reference it.
    DemoWanderer mWanderer;
    AIHTNDemoWanderer mWandererDaemon;
    AIHTNDemoPathfinder mPathfinderDaemon;
    AIHTNDemoGridTerrainDaemon mTerrainDaemon;

    bool mInitialized = false;
    float mAgeSeconds = 0.0f;
    float mCurrentTaskRemainingSeconds = 0.0f;
    bool mCurrentTaskStarted = false;

    bool mLastPlanSucceeded = false;
    std::uint64_t mPlanCount = 0u;
    std::uint64_t mCompletedTaskCount = 0u;
    double mLastPlannerMilliseconds = 0.0;
    double mTotalPlannerMilliseconds = 0.0;
    double mMaxPlannerMilliseconds = 0.0;
    std::uint64_t mMaxPlannerPlanIndex = 0u;
    float mMaxPlannerAgeSeconds = 0.0f;
    std::size_t mMaxPlannerPrimitiveTaskCount = 0u;
    bool mMaxPlannerPlanSucceeded = false;
    std::deque<HistoryEntry> mHistory;
};
