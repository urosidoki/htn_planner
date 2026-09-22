// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNDemoCallTermReporting.h"
#include "AI/AIHTNDemoWandererAgent.h"

#include "Core/HTNFileHelpers.h"
#include "Core/HTNCallTermBinding.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "WorldState/HTNWorldState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <sstream>

namespace
{
struct WandererCallTerms
{
    static int32 Increment(const int32 inValue)
    {
        return inValue + 1;
    }

    static bool BothCoordinatesEven(const Cell& inLocation)
    {
        return (inLocation.X % 2) == 0 && (inLocation.Y % 2) == 0;
    }

    static bool BothCoordinatesOdd(const Cell& inLocation)
    {
        return (inLocation.X % 2) != 0 && (inLocation.Y % 2) != 0;
    }
};

const HtnSymbol* WalkSegmentTask()
{
    static const HtnSymbol* Symbol = HtnSymbol::sGetSymbol("!walk_segment");
    return Symbol;
}

const HtnSymbol* PlayContextualAnimationTask()
{
    static const HtnSymbol* Symbol = HtnSymbol::sGetSymbol("!play_contextual_animation");
    return Symbol;
}

const HtnSymbol* WaitForPathfindingQueryTask()
{
    static const HtnSymbol* Symbol = HtnSymbol::sGetSymbol("!wait_for_pathfinding_query");
    return Symbol;
}

const HtnSymbol* WandererIdleTask()
{
    static const HtnSymbol* Symbol = HtnSymbol::sGetSymbol("!wanderer_idle");
    return Symbol;
}

const HtnSymbol* SayTask()
{
    static const HtnSymbol* Symbol = HtnSymbol::sGetSymbol("!say");
    return Symbol;
}
}

AIHTNDemoWandererAgent::AIHTNDemoWandererAgent(
    const std::uint32_t inId,
    const HTNGeneratedPlannerDefinition* inGeneratedDefinition,
    const std::size_t inInitialWaypointIndex,
    DemoGridTerrain& inTerrain,
    const HTNCallTermRegistry& inCallTermRegistry)
    : mId(inId)
    , mGeneratedDefinition(inGeneratedDefinition)
    , mInitialWaypointIndex(inInitialWaypointIndex)
    , mCallTermRegistry(inCallTermRegistry)
    , mTerrain(inTerrain)
    , mWanderer(inTerrain)
    , mWandererDaemon(mWanderer)
    , mPathfinderDaemon(inTerrain)
    , mTerrainDaemon(inTerrain)
{
}

AIHTNDemoWandererAgent::~AIHTNDemoWandererAgent() = default;

void AIHTNDemoWandererAgent::BindCallTerms(HTNCallTermRegistry& ioRegistry)
{
    HTN_CALLTERM_BIND(ioRegistry, "inc", WandererCallTerms, Increment);
    HTN_CALLTERM_BIND(ioRegistry, "both_coordinates_even", WandererCallTerms, BothCoordinatesEven);
    HTN_CALLTERM_BIND(ioRegistry, "both_coordinates_odd", WandererCallTerms, BothCoordinatesOdd);
    AIHTNDemoPathfinder::BindCallTerms(ioRegistry);
}

bool AIHTNDemoWandererAgent::Initialize()
{
    if (mInitialized)
        return true;

    if (!mGeneratedDefinition)
    {
        AddHistory("Initialization failed: missing generated Wanderer definition");
        return false;
    }

    mDatabaseHook = std::make_unique<HTNDatabaseHook>();
    mPlannerHook = std::make_unique<HTNPlannerHook>(
        mDatabaseHook->GetWorldState(), mCallTermRegistry);
#ifdef HTN_HOT_RELOAD_DEMO
    // Host daemon schema, independent of the editable DLL's queries. This
    // registry stays with the permanent hook across reloads; no AST is needed.
    for (const char* Fact : {"wanderer_state", "wanderer_location", "wanderer_destination",
                             "contextual_animation_at", "pathfind_state", "pathfinder",
                             "pathfinding_segment"})
        (void)mPlannerHook->GetFactRegistry().Register(HtnSymbol::sGetSymbol(Fact));
#endif
    HTN_CALLTERM_SET_DAEMON(
        mPlannerHook->GetCallTermBindingContext(), AIHTNDemoPathfinder, &mPathfinderDaemon);
    mWanderer.Reset(mInitialWaypointIndex);

    mWandererDaemon.Initialize(mPlannerHook.get());
    mPathfinderDaemon.Initialize(mPlannerHook.get());
    mTerrainDaemon.Initialize(mPlannerHook.get());
    if (!mPlannerHook->SetGeneratedPlannerDefinition(mGeneratedDefinition))
    {
        AddHistory("Initialization failed: incompatible generated planner ABI");
        return false;
    }

    mPlanningUnit = std::make_unique<HTNPlanningUnit>(*mDatabaseHook, *mPlannerHook, "run");
    mPlanningUnit->GetExecutionContext().MissingCallTermPolicy = HTNMissingCallTermPolicy::Report;
    mPlanningUnit->GetExecutionContext().MissingCallTermCallback = ReportGeneratedDemoMissingCallTerm;
#ifdef HTN_DEBUG_DECOMPOSITION
    mGeneratedDebugger.SetEnabled(true);
    mPlanningUnit->SetGeneratedDebugger(&mGeneratedDebugger);
#endif
    WriteWorldState();

    mInitialized = true;
    AddHistory("NPC spawned; persistent planner/execution storage created");
    return true;
}

void AIHTNDemoWandererAgent::Update(const float inDeltaTime)
{
    if (!mInitialized)
        return;

    const float DeltaTime = std::max(0.0f, inDeltaTime);
    mAgeSeconds += DeltaTime;

    mWandererDaemon.Update(DeltaTime);
    mTerrainDaemon.Update(DeltaTime);
    mPathfinderDaemon.Update(DeltaTime);
    WriteWorldState();

    if (!mPlanningUnit || mPlanningUnit->GetCurrentPlan().empty())
    {
        // Planning is synchronous in the demo. If there is no executable task,
        // immediately ask the planner for the next piece of work instead of
        // introducing an artificial cooldown between plans.
        TryPlan();

        if (!mPlanningUnit || mPlanningUnit->GetCurrentPlan().empty())
            return;
    }

    if (!mCurrentTaskStarted)
        StartCurrentTask();

    if (!mCurrentTaskStarted)
        return;

    mCurrentTaskRemainingSeconds -= DeltaTime;
    if (mCurrentTaskRemainingSeconds <= 0.0f)
        CompleteCurrentTask();
}

#ifdef HTN_HOT_RELOAD_DEMO
void AIHTNDemoWandererAgent::ReleaseGeneratedPlanner()
{
    // Destructors/deleters call into the old DLL: do this BEFORE unloading it.
    mPlanningUnit.reset();
    if (mPlannerHook)
        (void)mPlannerHook->SetGeneratedPlannerDefinition(nullptr);
    mGeneratedDefinition = nullptr;
    mInitialized = false;
    mCurrentTaskStarted = false;
    mCurrentTaskRemainingSeconds = 0.0f;
}

bool AIHTNDemoWandererAgent::AttachGeneratedPlanner(const HTNGeneratedPlannerDefinition* inDefinition)
{
    if (!inDefinition || !mPlannerHook || mPlanningUnit)
        return false;
    if (!mPlannerHook->SetGeneratedPlannerDefinition(inDefinition))
        return false;
    mGeneratedDefinition = inDefinition;
    mPlanningUnit = std::make_unique<HTNPlanningUnit>(*mDatabaseHook, *mPlannerHook, "run");
    mPlanningUnit->GetExecutionContext().MissingCallTermPolicy = HTNMissingCallTermPolicy::Report;
    mPlanningUnit->GetExecutionContext().MissingCallTermCallback = ReportGeneratedDemoMissingCallTerm;
#ifdef HTN_DEBUG_DECOMPOSITION
    mGeneratedDebugger.SetEnabled(true);
    mPlanningUnit->SetGeneratedDebugger(&mGeneratedDebugger);
#endif
    mInitialized = true;
    mLastPlanSucceeded = false;
    // Replan on the next update; do not execute candidate callterms as validation.
    AddHistory("Domain reloaded; old plan discarded, gameplay state preserved");
    return true;
}
#endif

const std::vector<HTNAtomOwner>& AIHTNDemoWandererAgent::GetCurrentPlan() const
{
    static const std::vector<HTNAtomOwner> EmptyPlan;
    return mPlanningUnit ? mPlanningUnit->GetCurrentPlan() : EmptyPlan;
}

std::size_t AIHTNDemoWandererAgent::GetCurrentTaskIndex() const
{
    return mPlanningUnit ? mPlanningUnit->GetCurrentPrimitiveTaskIndex() : 0u;
}

const char* AIHTNDemoWandererAgent::GetCurrentTaskName() const
{
    const HTNAtomOwner* Task = mPlanningUnit ? mPlanningUnit->GetCurrentPrimitiveTask() : nullptr;
    if (!Task)
        return mLastPlanSucceeded ? "<no task>" : "<plan failed>";

    const HtnSymbol* TaskHead = HTNGetTaskHead(*Task);
    return TaskHead ? TaskHead->GetString().c_str() : "<invalid task>";
}

bool AIHTNDemoWandererAgent::GetCurrentNavigationPath(std::vector<Cell>& outPath) const
{
    return mPathfinderDaemon.GetRemainingPath(
        mWanderer.GetCurrentLocation(),
        mWanderer.GetDestination(),
        outPath);
}

const HTNWorldState& AIHTNDemoWandererAgent::GetWorldState() const
{
    return mDatabaseHook->GetWorldState();
}

void AIHTNDemoWandererAgent::WriteWorldState()
{
    if (!mDatabaseHook)
        return;

    HTNWorldState& WorldState = mDatabaseHook->GetWorldState();
    mTerrainDaemon.WriteWorldState(WorldState);
    mWandererDaemon.WriteWorldState(WorldState);
    mPathfinderDaemon.WriteWorldState(WorldState);
}

void AIHTNDemoWandererAgent::TryPlan()
{
    if (!mPlanningUnit)
        return;

    // A successful decomposition can legitimately contain no primitive tasks.
    // The pathfinding request branch is the important example: the callterm
    // mutates the world state by starting an async request, then the decomposition
    // completes with an empty plan. Replan immediately so that the next pass can
    // observe that new state and produce wait_for_pathfinding_query.
    //
    // Keep a small safety budget so a domain that repeatedly succeeds with an
    // empty plan and makes no world-state progress cannot spin forever in one frame.
    for (std::size_t PlanningPass = 0u;
         PlanningPass < MaxImmediatePlanningPasses;
         ++PlanningPass)
    {
        const auto Start = std::chrono::steady_clock::now();
        mLastPlanSucceeded = mPlanningUnit->DecomposeTopLevelMethod() == HTN_DECOMPOSITION_SUCCEEDED;
        const auto End = std::chrono::steady_clock::now();
        mLastPlannerMilliseconds = std::chrono::duration<double, std::milli>(End - Start).count();
        mTotalPlannerMilliseconds += mLastPlannerMilliseconds;
        ++mPlanCount;

        const HTNAtomOwner& Output = mPlanningUnit->GetLastDecomposition().GetResult();
        const int32_t OutputCount = Output.GetListSize();
        if (mLastPlannerMilliseconds > mMaxPlannerMilliseconds)
        {
            mMaxPlannerMilliseconds = mLastPlannerMilliseconds;
            mMaxPlannerPlanIndex = mPlanCount;
            mMaxPlannerAgeSeconds = mAgeSeconds;
            mMaxPlannerPrimitiveTaskCount = OutputCount > 0 ? static_cast<std::size_t>(OutputCount) : 0u;
            mMaxPlannerPlanSucceeded = mLastPlanSucceeded;
        }

        if (!mLastPlanSucceeded)
        {
            AddHistory("Planning failed; retrying next update");
            return;
        }

        mCurrentTaskStarted = false;

        if (mPlanningUnit->GetCurrentPlan().empty())
        {
            AddHistory("Plan completed immediately (side effect); replanning now");

            // A callterm may have changed daemon-owned state and/or the database
            // during decomposition. Re-publish the authoritative state before
            // the next immediate planning pass.
            WriteWorldState();
            continue;
        }

        std::ostringstream Stream;
        Stream << "New plan: " << mPlanningUnit->GetCurrentPlan().size() << " plan step(s)";
        AddHistory(Stream.str());
        StartCurrentTask();
        return;
    }

    AddHistory("Warning: immediate replanning budget exhausted with no executable task");
}

void AIHTNDemoWandererAgent::StartCurrentTask()
{
    if (!mPlanningUnit)
        return;

    const HTNPrimitiveTaskResolution Resolution = mPlanningUnit->ResolveCurrentPrimitiveTask();
    if (Resolution == HTNPrimitiveTaskResolution::PlanCompleted)
    {
        FinishPlan();
        return;
    }

    if (Resolution == HTNPrimitiveTaskResolution::Failed)
    {
        mLastPlanSucceeded = false;
        AddHistory("Plan resolution failed");
        mPlanningUnit->ClearCurrentPlan();
        mCurrentTaskStarted = false;
        return;
    }

    const HTNAtomOwner* CurrentTask = mPlanningUnit->GetCurrentPrimitiveTask();
    if (!CurrentTask)
    {
        mLastPlanSucceeded = false;
        AddHistory("Plan contains no executable primitive task");
        mPlanningUnit->ClearCurrentPlan();
        mCurrentTaskStarted = false;
        return;
    }

    const HTNAtomOwner& Task = *CurrentTask;
    const HtnSymbol* TaskHead = HTNGetTaskHead(Task);

    if (TaskHead == WalkSegmentTask())
    {
        Cell TargetLocation;
        const uint32 ArgumentCount = HTNGetTaskArgumentCount(Task);
        const Cell& CurrentLocation = mWanderer.GetCurrentLocation();
        float DistanceInCells = 1.0f;
        if (ArgumentCount > 0u && HTNTryParseType(HTNGetTaskArgument(Task, 0u), TargetLocation))
        {
            const float DeltaX = static_cast<float>(TargetLocation.X - CurrentLocation.X);
            const float DeltaY = static_cast<float>(TargetLocation.Y - CurrentLocation.Y);
            DistanceInCells = std::sqrt(DeltaX * DeltaX + DeltaY * DeltaY);
        }

        const float Speed = std::max(0.01f, mWanderer.GetMoveSpeedCellsPerSecond());
        mCurrentTaskRemainingSeconds = std::max(0.01f, DistanceInCells / Speed);
    }
    else if (TaskHead == PlayContextualAnimationTask())
    {
        // The primitive is declared as:
        //   (!play_contextual_animation ?animation ?location)
        // Resolve the real world interactable at that location so execution
        // duration comes from the shared terrain object rather than a duplicated
        // executor constant.
        const uint32 ArgumentCount = HTNGetTaskArgumentCount(Task);
        Cell InteractableLocation;
        const DemoGridInteractable* Interactable = nullptr;

        if (ArgumentCount >= 2u &&
            HTNTryParseType(HTNGetTaskArgument(Task, 1u), InteractableLocation))
        {
            Interactable = mTerrain.GetInteractableAt(InteractableLocation);
        }

        if (Interactable)
        {
            mCurrentTaskRemainingSeconds =
                std::max(0.01f, Interactable->UsageTimeSeconds);
        }
        else
        {
            // This indicates a mismatch between the HTN world state and the
            // actual demo world. Keep execution alive, but make the problem
            // visible in History instead of silently using a stale fixed time.
            mCurrentTaskRemainingSeconds = 0.05f;
            AddHistory("Warning: contextual animation has no matching interactable");
        }
    }
    else if (TaskHead == WaitForPathfindingQueryTask())
        mCurrentTaskRemainingSeconds = WaitTaskDurationSeconds;
    else if (TaskHead == WandererIdleTask())
        mCurrentTaskRemainingSeconds = IdleTaskDurationSeconds;
    else if (TaskHead == SayTask())
        mCurrentTaskRemainingSeconds = 0.8f;
    else
        mCurrentTaskRemainingSeconds = 0.05f;

    mCurrentTaskStarted = true;
    AddHistory("Start: " + FormatTask(Task));
}

void AIHTNDemoWandererAgent::CompleteCurrentTask()
{
    const HTNAtomOwner* CurrentTask = mPlanningUnit ? mPlanningUnit->GetCurrentPrimitiveTask() : nullptr;
    if (!CurrentTask)
    {
        FinishPlan();
        return;
    }

    const HTNAtomOwner& Task = *CurrentTask;
    const HtnSymbol* TaskHead = HTNGetTaskHead(Task);

    if (TaskHead == WalkSegmentTask())
    {
        const uint32 ArgumentCount = HTNGetTaskArgumentCount(Task);
        Cell Location;
        if (ArgumentCount > 0u && HTNTryParseType(HTNGetTaskArgument(Task, 0u), Location))
            mWanderer.NotifyWalkSegmentCompleted(Location);
    }
    else if (TaskHead == PlayContextualAnimationTask())
    {
        mWanderer.NotifyContextualAnimationCompleted();
    }

    ++mCompletedTaskCount;
    AddHistory("Complete: " + FormatTask(Task));

    mPlanningUnit->CompleteCurrentPrimitiveTask();
    mCurrentTaskStarted = false;
    mCurrentTaskRemainingSeconds = 0.0f;
    WriteWorldState();
    StartCurrentTask();
}

void AIHTNDemoWandererAgent::FinishPlan()
{
    if (mPlanningUnit)
        mPlanningUnit->ClearCurrentPlan();
    mCurrentTaskStarted = false;
    mCurrentTaskRemainingSeconds = 0.0f;

    // There is no reason to leave an NPC without work until a timer expires.
    // The planner is synchronous, so immediately request the next plan.
    TryPlan();
}

void AIHTNDemoWandererAgent::AddHistory(const std::string& inText)
{
    mHistory.push_back({mAgeSeconds, inText});
    while (mHistory.size() > MaxHistoryEntries)
        mHistory.pop_front();
}

std::string AIHTNDemoWandererAgent::FormatTask(const HTNAtomOwner& inTask) const
{
    std::ostringstream Stream;
    const HtnSymbol* TaskHead = HTNGetTaskHead(inTask);
    Stream << (TaskHead ? TaskHead->GetString() : "<invalid task>");
    const uint32 ArgumentCount = HTNGetTaskArgumentCount(inTask);
    for (uint32 ArgumentIndex = 0u; ArgumentIndex < ArgumentCount; ++ArgumentIndex)
        Stream << ' ' << HTNAtomToString(HTNGetTaskArgument(inTask, ArgumentIndex), true);
    return Stream.str();
}
