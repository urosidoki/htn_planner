// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNCallTermRegistry.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

class HTNNPCSimulationPanel;
struct HTNGeneratedPlannerDefinition;

// Editor/demo-owned module lifetime. No production planner ABI changes.
class HTNHotReloadDemo
{
public:
    HTNHotReloadDemo(std::filesystem::path inRepositoryRoot, std::filesystem::path inBinaryDirectory);
    ~HTNHotReloadDemo();
    HTNHotReloadDemo(const HTNHotReloadDemo&) = delete;
    HTNHotReloadDemo& operator=(const HTNHotReloadDemo&) = delete;
    void Initialize();
    void Update(float inDeltaTime);
    void Render();

private:
    friend int HTNHotReloadDemoSelfTest(const std::filesystem::path&, const std::filesystem::path&, bool);
    void CompileDomain(const std::filesystem::path& inSource = {});
    bool HotReload();
    bool LoadDomain();
    void UnloadDomain();
    bool RestorePreviousDomain();
    void ReadSource();
    void SaveSource();

    std::filesystem::path mRoot, mBin, mActivePath, mCandidatePath, mBackupPath;
    HTNCallTermRegistry mRegistry;
    void* mRuntimeModule = nullptr;
    void* mDomainModule = nullptr;
    const HTNGeneratedPlannerDefinition* mDefinition = nullptr;
    std::unique_ptr<HTNNPCSimulationPanel> mSimulation;
    std::thread mCompiler;
    std::atomic<bool> mCompileFinished{false};
    bool mCompiling = false;
    int mCompileExitCode = -1; // Worker writes; main reads only after acquire/join.
    bool mCandidateReady = false;
    bool mSourceDirty = false;
    std::uint64_t mBuildAttempt = 0u, mCompiledRevision = 0u, mActiveRevision = 0u;
    std::string mStatus, mCompileLog, mSource;
};

int HTNHotReloadDemoSelfTest(const std::filesystem::path& inRoot, const std::filesystem::path& inBin,
                           bool inTestCompiler = false);
