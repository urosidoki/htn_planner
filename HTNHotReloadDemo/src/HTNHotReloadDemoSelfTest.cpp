// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNHotReloadDemo.h"
#include "AI/AIHTNDemoWandererAgent.h"
#include "UI/HTNNPCSimulationPanel.h"
#include "WorldState/HTNWorldState.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace
{
std::string ReadFile(const std::filesystem::path& inPath)
{
    std::ifstream Stream(inPath, std::ios::binary);
    if (!Stream) return {};
    return {std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
}
bool CopyFile(const std::filesystem::path& inFrom, const std::filesystem::path& inTo)
{
    std::error_code Error;
    std::filesystem::create_directories(inTo.parent_path(), Error);
    if (!Error) (void)std::filesystem::copy_file(inFrom, inTo,
        std::filesystem::copy_options::overwrite_existing, Error);
    return !Error;
}
bool WriteFile(const std::filesystem::path& inPath, const std::string& inText)
{
    std::ofstream Stream(inPath, std::ios::binary | std::ios::trunc);
    Stream.write(inText.data(), static_cast<std::streamsize>(inText.size()));
    Stream.close();
    return static_cast<bool>(Stream);
}
}

// Real modules, without video. Pipeline mode uses the SAME async compile path
// as the button, with an isolated source override. Original DLL is restored after
// all NPCs/hooks/modules die, on success AND on ordinary test failure.
int HTNHotReloadDemoSelfTest(const std::filesystem::path& inRoot, const std::filesystem::path& inBin,
                           bool inTestCompiler)
{
#ifndef _WIN32
    if (inTestCompiler)
    {
        std::fputs("Pipeline self-test requires Windows/MSVC; it was NOT executed.\n", stderr);
        return 2;
    }
#endif
    const auto Active = inBin / "WandererHTN.dll";
    const auto Original = inBin / "self-test-original" / "WandererHTN.dll";
    const auto OriginalBytes = ReadFile(Active);
    if (OriginalBytes.empty() || !CopyFile(Active, Original))
    {
        std::fputs("Self-test: initial DLL backup failed; nothing loaded/modified.\n", stderr);
        return 1;
    }
    const int Result = [&]() -> int
    {
        HTNHotReloadDemo Demo(inRoot, inBin);
        Demo.Initialize();
        const auto Fail = [&Demo](const char* Message)
        {
            std::fprintf(stderr, "Hot reload self-test FAIL: %s\n%s\n%s\n",
                Message, Demo.mStatus.c_str(), Demo.mCompileLog.c_str());
            return 1;
        };
        if (!Demo.mDefinition) return Fail("initial domain unavailable");
        DemoGridTerrain Terrain;
        AIHTNDemoWandererAgent Agent(42u, Demo.mDefinition, 0u, Terrain, Demo.mRegistry);
        if (!Agent.Initialize()) return Fail("agent initialization");
        const auto* WorldState = &Agent.GetWorldState();
        for (const char* Fact : {"wanderer_state", "wanderer_location", "wanderer_destination"})
            if (!WorldState->ContainsFactArgumentsTable(Fact, 1u)) return Fail("daemon fact missing");
        if (!WorldState->ContainsFactArgumentsTable("contextual_animation_at", 2u))
            return Fail("terrain facts missing");
        Agent.GetWanderer().SetMoveSpeedCellsPerSecond(7.0f);
        const auto Step = [&]
        {
            Agent.Update(0.016f);
            Demo.Update(0.016f);
        };
        const auto FindMovingTask = [&]
        {
            for (int Frame = 0; Frame < 30000; ++Frame)
            {
                Step();
                if (std::string(Agent.GetCurrentTaskName()) == "!walk_segment" &&
                    Agent.GetCurrentTaskRemainingSeconds() > 0.02f) return true;
            }
            return false;
        };
        const auto HasMarker = [&Agent](const std::string& Marker, float SinceAge = -1.0f)
        {
            for (const auto& Entry : Agent.GetHistory())
                if (Entry.AgeSeconds >= SinceAge && Entry.Text.find("Start:") != std::string::npos &&
                    Entry.Text.find(Marker) != std::string::npos) return true;
            return false;
        };
        const auto ApplyCandidate = [&]
        {
            // This extra agent is not panel-owned; release it before panel reload.
            Agent.ReleaseGeneratedPlanner();
            return Demo.HotReload() && Agent.AttachGeneratedPlanner(Demo.mDefinition);
        };
        if (!FindMovingTask()) return Fail("NPC never started moving");

        if (inTestCompiler)
        {
            const auto FixtureDir = inBin / "pipeline-fixture";
            const auto Source = FixtureDir / "Wanderer.domain";
            if (!CopyFile(inRoot / "Domains" / "Includes" / "movement2.domain",
                          FixtureDir / "Includes" / "movement2.domain")) return Fail("fixture include copy");
            const auto Template = ReadFile(inRoot / "HTNHotReloadDemo" / "Validation" / "Wanderer.domain");
            if (Template.empty()) return Fail("validation domain missing");
            const auto BuildRevision = [&](const std::string& Revision)
            {
                std::string Text = Template;
                std::size_t Position = 0;
                while ((Position = Text.find("@REVISION@", Position)) != std::string::npos)
                {
                    Text.replace(Position, 10u, Revision);
                    Position += Revision.size();
                }
                return WriteFile(Source, Text);
            };
            const auto Compile = [&]
            {
                Demo.CompileDomain(Source);
                if (!Demo.mCompiling) return false;
                // NPCs keep updating while the real compiler runs, with no UI.
                while (Demo.mCompiling)
                {
                    Step();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return true;
            };
            for (const std::string Revision : {"A", "B"})
            {
                if (!BuildRevision(Revision)) return Fail("fixture source write");
                const auto* Definition = Demo.mDefinition;
                const auto ActiveRevision = Demo.mActiveRevision;
                const auto ActiveBytes = ReadFile(Active);
                const float Age = Agent.GetAgeSeconds();
                if (!Compile() || !Demo.mCandidateReady || Demo.mCompileExitCode != 0)
                    return Fail("valid source compile");
                if (Demo.mDefinition != Definition || Demo.mActiveRevision != ActiveRevision ||
                    ReadFile(Active) != ActiveBytes || Agent.GetAgeSeconds() <= Age || !Agent.IsInitialized())
                    return Fail("Compile changed active domain or stopped NPC updates");
                const std::string Marker = "reload-validation-" + Revision;
                if (HasMarker(Marker)) return Fail("candidate behavior used before reload");
                if (!ApplyCandidate()) return Fail("compiled candidate reload");
                Step();
                if (!HasMarker(Marker) || &Agent.GetWorldState() != WorldState)
                    return Fail("new domain behavior not observed/state replaced");
                if (!FindMovingTask()) return Fail("new domain did not move NPC");
            }
            const auto* Definition = Demo.mDefinition;
            const auto ActiveRevision = Demo.mActiveRevision;
            const auto ActiveBytes = ReadFile(Active);
            const float Age = Agent.GetAgeSeconds();
            if (!WriteFile(Source, "(:domain InvalidSource (unterminated")) return Fail("invalid fixture write");
            if (!Compile() || Demo.mCompileExitCode == 0 || Demo.mCandidateReady ||
                Demo.mDefinition != Definition || Demo.mActiveRevision != ActiveRevision ||
                ReadFile(Active) != ActiveBytes || Agent.GetAgeSeconds() <= Age || !Agent.IsInitialized())
                return Fail("failed compilation did not preserve active domain/NPC updates");
            if (Demo.HotReload()) return Fail("failed build allowed reload");
            std::puts("Compile pipeline: PASS (A -> B, candidate isolation, invalid source, NPC updates)");
        }

        for (int Cycle = 0; Cycle < 8; ++Cycle)
        {
            if (!FindMovingTask()) return Fail("reload not reached during movement");
            bool HasDeferred = false;
            const auto& Plan = Agent.GetCurrentPlan();
            for (std::size_t Index = Agent.GetCurrentTaskIndex(); Index < Plan.size(); ++Index)
                HasDeferred = HasDeferred || HTNIsDeferredCallHead(HTNGetTaskHead(Plan[Index]));
            if (!HasDeferred) return Fail("no pending deferred call during moving reload");
            const auto Location = Agent.GetWanderer().GetCurrentLocation();
            const float Age = Agent.GetAgeSeconds();
            const auto Plans = Agent.GetPlanCount();
            if (!CopyFile(Active, Demo.mCandidatePath)) return Fail("reload fixture copy");
            Demo.mCandidateReady = true;
            ++Demo.mCompiledRevision;
            Agent.ReleaseGeneratedPlanner();
            if (!Agent.GetCurrentPlan().empty() || Agent.GetCurrentTaskRemainingSeconds() != 0.0f)
                return Fail("old active plan/timer retained");
            if (!Demo.HotReload() || !Agent.AttachGeneratedPlanner(Demo.mDefinition)) return Fail("moving reload");
            if (&Agent.GetWorldState() != WorldState || Agent.GetAgeSeconds() != Age ||
                !(Agent.GetWanderer().GetCurrentLocation() == Location) ||
                Agent.GetWanderer().GetMoveSpeedCellsPerSecond() != 7.0f ||
                !WorldState->ContainsFactArgumentsTable("wanderer_location", 1u))
                return Fail("moving reload lost gameplay/world state");
            Step();
            if (!Agent.DidLastPlanSucceed() || Agent.GetPlanCount() <= Plans) return Fail("replan after reload");
        }
        const auto Location = Agent.GetWanderer().GetCurrentLocation();
        const auto Completed = Agent.GetCompletedTaskCount();
        const float AfterReloadAge = Agent.GetAgeSeconds();
        for (int Frame = 0; Frame < 30000 && Agent.GetWanderer().GetCurrentLocation() == Location; ++Frame) Step();
        if (Agent.GetWanderer().GetCurrentLocation() == Location || Agent.GetCompletedTaskCount() <= Completed)
            return Fail("movement did not resume after reloads");
        if (inTestCompiler)
        {
            for (int Frame = 0; Frame < 30000 && !HasMarker("reload-validation-B-deferred", AfterReloadAge); ++Frame) Step();
            if (!HasMarker("reload-validation-B-deferred", AfterReloadAge)) return Fail("new deferred behavior never executed after moving reload");
        }
        Agent.ReleaseGeneratedPlanner();
        if (!WriteFile(Demo.mCandidatePath, "Deliberately invalid DLL")) return Fail("invalid DLL write");
        const auto Revision = Demo.mActiveRevision;
        Demo.mCandidateReady = true;
        if (Demo.HotReload() || !Demo.mDefinition || Demo.mActiveRevision != Revision ||
            Demo.mStatus.find("previous domain restored") == std::string::npos ||
            !Agent.AttachGeneratedPlanner(Demo.mDefinition)) return Fail("invalid DLL rollback");
        Step();
        if (!Agent.DidLastPlanSucceed() || &Agent.GetWorldState() != WorldState) return Fail("rollback replan");
        if (!FindMovingTask()) return Fail("movement did not resume after rollback");
        Agent.ReleaseGeneratedPlanner();
        Demo.mSimulation->ReleaseGeneratedPlanner();
        Demo.UnloadDomain();
        Demo.mCandidateReady = true;
        if (Demo.HotReload() || Demo.mDefinition) return Fail("unavailable domain accepted invalid DLL");
        Demo.Update(0.016f);
        std::puts("Lifecycle checks completed: facts, 8 moving reloads with deferred calls, state retention, resumed movement, rollback, unavailable domain.");
        return 0;
    }(); // All DLL callbacks/handles gone before restoring the original file.
    if (!CopyFile(Original, Active) || ReadFile(Active) != OriginalBytes)
    {
        std::fputs("Self-test: original DLL restoration FAILED; recover from self-test-original/WandererHTN.dll.\n", stderr);
        return 1;
    }
    if (Result == 0)
        std::puts(inTestCompiler ? "Hot reload pipeline self-test: PASS (original DLL restored)" :
                                  "Hot reload self-test: PASS (original DLL restored)");
    return Result;
}
