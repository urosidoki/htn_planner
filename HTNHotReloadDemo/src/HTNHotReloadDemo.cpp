// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNHotReloadDemo.h"
#include "AI/AIHTNDemoWandererAgent.h"
#include "UI/HTNNPCSimulationPanel.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "Translator/HTNRuntimeBridge.h"
#include "SDL.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>
#include <utility>

namespace
{
std::string ReadText(const std::filesystem::path& inPath)
{
    std::ifstream Stream(inPath, std::ios::binary);
    // Keep compiler diagnostics bounded, and avoid GUI text buffers of arbitrary size.
    std::string Text(256u * 1024u, '\0');
    Stream.read(Text.data(), static_cast<std::streamsize>(Text.size()));
    Text.resize(static_cast<std::size_t>(Stream.gcount()));
    return Text;
}

[[maybe_unused]] std::string DomainCompilerDefines()
{
    std::string Defines = "/DHTN_GENERATED_MODULE_EXPORTS";
#ifdef HTN_DEBUG_DECOMPOSITION
    Defines += " /DHTN_DEBUG_DECOMPOSITION";
#endif
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    Defines += " /DHTN_GENERATED_EXECUTION_PROFILING";
#endif
#ifdef HTN_PROFILE_DETAILED
    Defines += " /DHTN_PROFILE_DETAILED";
#endif
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    Defines += " /DHTN_MEMORY_ATOM_DIAGNOSTICS";
#endif
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS_DETAILED
    Defines += " /DHTN_MEMORY_ATOM_DIAGNOSTICS_DETAILED";
#endif
    return Defines;
}
}

HTNHotReloadDemo::HTNHotReloadDemo(std::filesystem::path inRoot, std::filesystem::path inBin)
    : mRoot(std::move(inRoot)), mBin(std::move(inBin))
    , mActivePath(mBin / "WandererHTN.dll")
    , mCandidatePath(mBin / "candidate" / "WandererHTN.dll")
    , mBackupPath(mBin / "previous" / "WandererHTN.dll")
{
    AIHTNDemoWandererAgent::BindCallTerms(mRegistry);
}

HTNHotReloadDemo::~HTNHotReloadDemo()
{
    if (mCompiler.joinable())
        mCompiler.join(); // Closing the demo waits for its in-flight compile.
    mSimulation.reset(); // Units/hooks must die before either DLL is unloaded.
    UnloadDomain();
    if (mRuntimeModule)
        SDL_UnloadObject(mRuntimeModule);
}

void HTNHotReloadDemo::Initialize()
{
    ReadSource();
    mSimulation = std::make_unique<HTNNPCSimulationPanel>(nullptr, mRegistry);
    mRuntimeModule = SDL_LoadObject((mBin / "HTNRuntimeBridge.dll").string().c_str());
    if (!mRuntimeModule)
    {
        mStatus = std::string("Runtime bridge could not be loaded: ") + SDL_GetError();
        return;
    }
    const auto Bind = reinterpret_cast<HTNRuntimeBridgeBindFn>(
        SDL_LoadFunction(mRuntimeModule, "HTNRuntimeBridge_Bind"));
    const auto API = HTNCreateHostRuntimeAPI();
    if (!Bind || !Bind(&API))
    {
        mStatus = "Runtime bridge ABI/binding rejected. Rebuild matching projects.";
        SDL_UnloadObject(mRuntimeModule);
        mRuntimeModule = nullptr;
        return;
    }
    if (LoadDomain() && mSimulation->AttachGeneratedPlanner(mDefinition))
        mStatus = "Initial build active. Edit/save source, Compile Domain, then Hot Reload.";
    else
    {
        mSimulation->ReleaseGeneratedPlanner();
        UnloadDomain();
        if (mStatus.empty())
            mStatus = "Initial domain preparation failed; simulation paused.";
    }
}

bool HTNHotReloadDemo::LoadDomain()
{
    mDomainModule = SDL_LoadObject(mActivePath.string().c_str());
    if (!mDomainModule)
    {
        mStatus = std::string("Domain load failed: ") + SDL_GetError();
        return false;
    }
    using GetDefinition = const HTNGeneratedPlannerDefinition* (*)();
    const auto Get = reinterpret_cast<GetDefinition>(
        SDL_LoadFunction(mDomainModule, "CreateWandererHotReloadHTN_GetDefinition"));
    mDefinition = Get ? Get() : nullptr;
    if (!HTNGeneratedPlanner_ValidateDefinition(mDefinition))
    {
        mStatus = "Missing domain export or incompatible ABI/lifecycle. Recompile with matching flags.";
        UnloadDomain();
        return false;
    }
    // Validate execution initialization without invoking domain callterms. Actual
    // per-agent scratch is recreated lazily by each new unit's first plan.
    void* Storage = ::operator new(mDefinition->execution_storage_size, std::nothrow);
    const bool Ready = Storage && mDefinition->initialize_execution_storage(Storage);
    if (Ready)
        mDefinition->destroy_execution_storage(Storage);
    ::operator delete(Storage);
    if (!Ready)
    {
        mStatus = "Domain execution-storage initialization failed.";
        UnloadDomain();
        return false;
    }
    return true;
}

void HTNHotReloadDemo::UnloadDomain()
{
    mDefinition = nullptr;
    if (mDomainModule)
        SDL_UnloadObject(mDomainModule);
    mDomainModule = nullptr;
}

void HTNHotReloadDemo::CompileDomain([[maybe_unused]]const std::filesystem::path& inSource)
{
    if (mCompiling || mSourceDirty || !mRuntimeModule)
        return;
#ifndef _WIN32
    mStatus = "Interactive compilation requires Windows/MSVC in this first version.";
#else
    mCandidateReady = false;
    std::error_code Error;
    std::filesystem::create_directories(mCandidatePath.parent_path(), Error);
    if (!Error)
        (void)std::filesystem::remove(mCandidatePath, Error);
    if (Error)
    {
        mStatus = "Candidate directory/file could not be prepared: " + Error.message();
        return;
    }
    ++mBuildAttempt;
    mCompiling = true;
    mCompileFinished.store(false, std::memory_order_relaxed);
    mStatus = "Compiling candidate; agents keep using the active domain.";
    const auto Script = mRoot / "HTNHotReloadDemo" / "CompileDomain.cmd";
    const auto Log = mBin / "candidate" / "Compile.log";
    const auto Source = inSource.empty() ? mRoot / "Domains" / "Wanderer.domain" : inSource;
    // Only trusted fixed paths/configuration/macros form this command, never editor text.
    // The outer quotes are required by cmd.exe when the script path contains spaces.
    const std::string Command = "\"\"" + Script.string() + "\" \"" +
        HTN_HOT_RELOAD_CONFIGURATION + "\" \"" + DomainCompilerDefines() + "\" \"" + Source.string() + "\" > \"" +
        Log.string() + "\" 2>&1\"";
    mCompiler = std::thread([this, Command]
    {
        mCompileExitCode = std::system(Command.c_str());
        mCompileFinished.store(true, std::memory_order_release);
    });
#endif
}

void HTNHotReloadDemo::Update(float inDeltaTime)
{
    if (mCompiling && mCompileFinished.load(std::memory_order_acquire))
    {
        mCompiler.join();
        mCompiling = false;
        mCompileLog = ReadText(mBin / "candidate" / "Compile.log");
        std::error_code Error;
        mCandidateReady = mCompileExitCode == 0 && std::filesystem::is_regular_file(mCandidatePath, Error);
        if (mCandidateReady)
        {
            mCompiledRevision = mBuildAttempt;
            mStatus = "Compile succeeded. Candidate ready; active domain unchanged.";
        }
        else
            mStatus = "Compile failed. Active domain unchanged; see compiler output.";
    }
    if (mDefinition && mSimulation)
        mSimulation->Update(inDeltaTime);
}

bool HTNHotReloadDemo::RestorePreviousDomain()
{
    std::error_code Error;
    (void)std::filesystem::copy_file(mBackupPath, mActivePath,
                                   std::filesystem::copy_options::overwrite_existing, Error);
    if (!Error && LoadDomain() && mSimulation->AttachGeneratedPlanner(mDefinition))
        return true;
    mSimulation->ReleaseGeneratedPlanner();
    UnloadDomain();
    return false;
}

bool HTNHotReloadDemo::HotReload()
{
    if (mCompiling || !mCandidateReady || mSourceDirty || !mRuntimeModule)
        return false;
    const bool HadActiveDomain = mDefinition != nullptr;
    std::error_code Error;
    if (HadActiveDomain)
    {
        std::filesystem::create_directories(mBackupPath.parent_path(), Error);
        if (!Error)
            (void)std::filesystem::copy_file(mActivePath, mBackupPath,
                                           std::filesystem::copy_options::overwrite_existing, Error);
        if (Error)
        {
            mStatus = "Backup failed; active domain untouched: " + Error.message();
            return false;
        }
    }
    mCandidateReady = false;
    // Called synchronously on the UI/simulation thread between Update calls.
    mSimulation->ReleaseGeneratedPlanner();
    UnloadDomain();
    (void)std::filesystem::copy_file(mCandidatePath, mActivePath,
                                   std::filesystem::copy_options::overwrite_existing, Error);
    if (!Error && LoadDomain() && mSimulation->AttachGeneratedPlanner(mDefinition))
    {
        mActiveRevision = mCompiledRevision;
        mStatus = "Hot reload succeeded. NPC state preserved; replanning on next update.";
        return true;
    }
    const std::string Failure = Error ? Error.message() : mStatus;
    // Some agents may have prepared the rejected new domain. Release ALL before unload.
    mSimulation->ReleaseGeneratedPlanner();
    UnloadDomain();
    if (HadActiveDomain && RestorePreviousDomain())
        mStatus = "Hot reload failed; previous domain restored. " + Failure;
    else
        mStatus = "Hot reload failed; restoration unavailable/failed. Simulation paused. " + Failure;
    return false;
}

void HTNHotReloadDemo::ReadSource()
{
    std::ifstream Stream(mRoot / "Domains" / "Wanderer.domain", std::ios::binary);
    if (!Stream)
    {
        mStatus = "Domain source could not be read; editor contents unchanged.";
        return;
    }
    std::string Source{std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
    if (Stream.bad())
    {
        mStatus = "Domain source read failed; editor contents unchanged.";
        return;
    }
    mSource = std::move(Source);
    mSourceDirty = false;
}

void HTNHotReloadDemo::SaveSource()
{
    if (mCompiling)
        return;
    std::ofstream Stream(mRoot / "Domains" / "Wanderer.domain", std::ios::binary | std::ios::trunc);
    Stream.write(mSource.data(), static_cast<std::streamsize>(mSource.size()));
    Stream.close();
    if (!Stream)
    {
        mStatus = "Source save failed. Compile disabled until source is saved.";
        return;
    }
    mSourceDirty = false;
    mCandidateReady = false;
    mStatus = "Source saved. Compile Domain again before Hot Reload.";
}

void HTNHotReloadDemo::Render()
{
    ImGui::BeginDisabled(mCompiling || mSourceDirty || !mRuntimeModule);
    if (ImGui::Button("Compile Domain"))
        CompileDomain();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(mCompiling || !mCandidateReady || mSourceDirty || !mRuntimeModule);
    if (ImGui::Button("Hot Reload"))
        (void)HotReload();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("Compiled: %llu%s | Active: %llu%s",
        static_cast<unsigned long long>(mCompiledRevision), mCandidateReady ? " (ready)" : "",
        static_cast<unsigned long long>(mActiveRevision), mDefinition ? "" : " (unavailable)");
    ImGui::TextWrapped("%s", mStatus.c_str());
    if (ImGui::BeginTabBar("HotReloadTabs"))
    {
        if (ImGui::BeginTabItem("NPC Simulation"))
        {
            mSimulation->Render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Domain Source"))
        {
            ImGui::TextUnformatted((mRoot / "Domains" / "Wanderer.domain").string().c_str());
            ImGui::BeginDisabled(mCompiling);
            if (ImGui::Button("Save Domain")) SaveSource();
            ImGui::SameLine();
            if (ImGui::Button("Discard edits / Read from disk")) ReadSource();
            ImGui::TextDisabled("Edit linked movement code externally in Domains/Includes/movement2.domain.");
            if (ImGui::InputTextMultiline("##DomainSource", &mSource, ImVec2(-1, -1), ImGuiInputTextFlags_AllowTabInput))
                mSourceDirty = true;
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Compiler Output"))
        {
            ImGui::TextUnformatted(mCompiling ? "Build in progress; output updates when it finishes." : "Last compiler output:");
            ImGui::BeginChild("CompileOutput", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(mCompileLog.c_str());
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
