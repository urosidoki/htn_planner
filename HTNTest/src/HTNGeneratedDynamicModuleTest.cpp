// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HtnSymbol.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "Translator/HTNRuntimeBridge.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace
{
class DynamicLibrary
{
public:
    ~DynamicLibrary() { Close(); }

    bool Open(const std::filesystem::path& inPath)
    {
#ifdef _WIN32
        mHandle = LoadLibraryW(inPath.c_str());
#else
        mHandle = dlopen(inPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        return mHandle != nullptr;
    }

    template<typename T>
    T Find(const char* inName) const
    {
#ifdef _WIN32
        const FARPROC Symbol = GetProcAddress(mHandle, inName);
#else
        void* const Symbol = dlsym(mHandle, inName);
#endif
        static_assert(sizeof(T) == sizeof(Symbol));
        T Result = nullptr;
        std::memcpy(&Result, &Symbol, sizeof(Result));
        return Result;
    }

    void Close()
    {
        if (!mHandle)
            return;
#ifdef _WIN32
        FreeLibrary(mHandle);
#else
        dlclose(mHandle);
#endif
        mHandle = nullptr;
    }

private:
#ifdef _WIN32
    HMODULE mHandle = nullptr;
#else
    void* mHandle = nullptr;
#endif
};

std::filesystem::path GetExecutableDirectory()
{
#ifdef _WIN32
    wchar_t Path[MAX_PATH];
    const DWORD Length = GetModuleFileNameW(nullptr, Path, MAX_PATH);
    return std::filesystem::path(std::wstring(Path, Length)).parent_path();
#else
    char Path[4096];
    const ssize_t Length = readlink("/proc/self/exe", Path, sizeof(Path));
    return Length > 0 ? std::filesystem::path(std::string(Path, static_cast<std::size_t>(Length))).parent_path()
                      : std::filesystem::current_path();
#endif
}

std::filesystem::path ModulePath(const char* inName)
{
#ifdef _WIN32
    return GetExecutableDirectory() / (std::string(inName) + ".dll");
#else
    return GetExecutableDirectory() / (std::string("lib") + inName + ".so");
#endif
}

bool BindRuntimeBridge(DynamicLibrary& outRuntime)
{
    if (!outRuntime.Open(ModulePath("HTNRuntimeBridge")))
        return false;

    const auto Bind = outRuntime.Find<HTNRuntimeBridgeBindFn>("HTNRuntimeBridge_Bind");
    if (!Bind)
        return false;

    const HTNHostRuntimeAPI API = HTNCreateHostRuntimeAPI();
    return Bind(&API) != 0;
}

bool MatchesBacktrackingPlan(const HTNPlanningUnit& inPlanningUnit, bool inSmallPlan)
{
    static constexpr const char* ExpectedArguments[] = {"one", "two", "three"};
    static constexpr const char* ExpectedSmallArguments[] = {"small-one", "small-two"};
    const char* const* Arguments = inSmallPlan ? ExpectedSmallArguments : ExpectedArguments;
    const std::size_t ArgumentCount = inSmallPlan ? std::size(ExpectedSmallArguments)
                                                : std::size(ExpectedArguments);
    const std::vector<HTNAtomOwner>& Plan = inPlanningUnit.GetCurrentPlan();
    if (Plan.size() != ArgumentCount)
        return false;

    for (std::size_t Index = 0u; Index < Plan.size(); ++Index)
    {
        const HtnSymbol* Head = HTNGetTaskHead(Plan[Index]);
        if (!Head || Head->GetString() != "!step" || HTNGetTaskArgumentCount(Plan[Index]) != 1u)
            return false;

        const HTNAtom& Argument = HTNGetTaskArgument(Plan[Index], 0u);
        if (!HTNAtomIsBound(Argument) ||
            !HTNAtomIsType<std::string>(Argument) ||
            HTNAtomGetValue<std::string>(Argument) != Arguments[Index])
        {
            return false;
        }
    }

    return true;
}
}

TEST(HTNGeneratedDynamicModuleTest, RuntimeBridgeRejectsInvalidAPIAndAcceptsMatchingRebind)
{
    DynamicLibrary Runtime;
    ASSERT_TRUE(Runtime.Open(ModulePath("HTNRuntimeBridge")));
    const auto Bind = Runtime.Find<HTNRuntimeBridgeBindFn>("HTNRuntimeBridge_Bind");
    ASSERT_NE(Bind, nullptr);

    const HTNHostRuntimeAPI API = HTNCreateHostRuntimeAPI();
    EXPECT_EQ(Bind(nullptr), 0);

    HTNHostRuntimeAPI Invalid = API;
    // Same feature configuration, previous bridge ABI revision.
    Invalid.abi_version = (API.abi_version & UINT32_C(0xFFFF0000)) | UINT32_C(1);
    EXPECT_EQ(Bind(&Invalid), 0);
    Invalid = API;
    --Invalid.size;
    EXPECT_EQ(Bind(&Invalid), 0);
    Invalid = API;
    Invalid.HTNAtom_Init = nullptr;
    EXPECT_EQ(Bind(&Invalid), 0);

    ASSERT_NE(Bind(&API), 0);
    EXPECT_NE(Bind(&API), 0);
    EXPECT_EQ(Bind(&Invalid), 0);
    EXPECT_NE(Bind(&API), 0);
}

TEST(HTNGeneratedDynamicModuleTest, SharedLoadedDefinitionKeepsPerEntityStorageIsolatedUnderStress)
{
    constexpr std::size_t WorkerCount = 8u;
    constexpr std::size_t IterationCount = 128u;

    DynamicLibrary Runtime;
    ASSERT_TRUE(BindRuntimeBridge(Runtime));

    DynamicLibrary Module;
    ASSERT_TRUE(Module.Open(ModulePath("HTNTestDomainModule")));
    const auto GetDefinition = Module.Find<const HTNGeneratedPlannerDefinition* (*)()>(
        "CreateBacktrackingPolicyModuleHTN_GetDefinition");
    ASSERT_NE(GetDefinition, nullptr);
    const HTNGeneratedPlannerDefinition* Definition = GetDefinition();
    ASSERT_NE(Definition, nullptr);

    {
        std::vector<std::unique_ptr<HTNDatabaseHook>> Databases;
        std::vector<std::unique_ptr<HTNPlannerHook>> PlannerHooks;
        std::vector<std::unique_ptr<HTNPlanningUnit>> PlanningUnits;
        Databases.reserve(WorkerCount);
        PlannerHooks.reserve(WorkerCount);
        PlanningUnits.reserve(WorkerCount);

        const HtnSymbol* TopLevelMethod = HtnSymbol::sGetSymbol("run");
        const HtnSymbol* SmallMethod = HtnSymbol::sGetSymbol("run_small");
        for (std::size_t WorkerIndex = 0u; WorkerIndex < WorkerCount; ++WorkerIndex)
        {
            auto Database = std::make_unique<HTNDatabaseHook>();
            auto PlannerHook = std::make_unique<HTNPlannerHook>(Database->GetWorldState());
            ASSERT_TRUE(PlannerHook->SetGeneratedPlannerDefinition(Definition));
            auto PlanningUnit = std::make_unique<HTNPlanningUnit>(
                *Database, *PlannerHook, TopLevelMethod);

            Databases.emplace_back(std::move(Database));
            PlannerHooks.emplace_back(std::move(PlannerHook));
            PlanningUnits.emplace_back(std::move(PlanningUnit));
        }

        for (std::size_t Left = 0u; Left < WorkerCount; ++Left)
        {
            ASSERT_NE(PlannerHooks[Left]->GetGeneratedPreparedStorage(), nullptr);
            for (std::size_t Right = Left + 1u; Right < WorkerCount; ++Right)
            {
                EXPECT_NE(PlannerHooks[Left]->GetGeneratedPreparedStorage(),
                          PlannerHooks[Right]->GetGeneratedPreparedStorage());
            }
        }

        std::atomic<std::size_t> ReadyCount{0u};
        std::atomic<bool> Start{false};
        std::atomic<std::size_t> FailureCount{0u};
        std::vector<std::thread> Workers;
        Workers.reserve(WorkerCount);

        for (std::size_t WorkerIndex = 0u; WorkerIndex < WorkerCount; ++WorkerIndex)
        {
            HTNPlanningUnit* PlanningUnit = PlanningUnits[WorkerIndex].get();
            Workers.emplace_back([PlanningUnit, WorkerIndex, TopLevelMethod, SmallMethod,
                                  &ReadyCount, &Start, &FailureCount]()
            {
                ReadyCount.fetch_add(1u, std::memory_order_release);
                while (!Start.load(std::memory_order_acquire))
                    std::this_thread::yield();

                for (std::size_t Iteration = 0u; Iteration < IterationCount; ++Iteration)
                {
                    const bool SmallPlan = (Iteration + WorkerIndex) % 2u != 0u;
                    if (PlanningUnit->DecomposeTopLevelMethod(SmallPlan ? SmallMethod : TopLevelMethod) !=
                            HTN_DECOMPOSITION_SUCCEEDED ||
                        !MatchesBacktrackingPlan(*PlanningUnit, SmallPlan))
                    {
                        FailureCount.fetch_add(1u, std::memory_order_relaxed);
                    }
#ifdef HTN_GENERATED_EXECUTION_PROFILING
                    if (PlanningUnit->GetLastGeneratedTimingBreakdown().ExecutionStorageCreated !=
                        (Iteration == 0u))
                    {
                        FailureCount.fetch_add(1u, std::memory_order_relaxed);
                    }
#endif
                }
            });
        }

        while (ReadyCount.load(std::memory_order_acquire) != WorkerCount)
            std::this_thread::yield();
        Start.store(true, std::memory_order_release);

        for (std::thread& Worker : Workers)
            Worker.join();

        EXPECT_EQ(FailureCount.load(std::memory_order_relaxed), 0u);
    }

    // All hooks and planning-unit storage are gone before callbacks and descriptors become invalid.
    Module.Close();
    Runtime.Close();
}

TEST(HTNGeneratedDynamicModuleTest, RejectsIncompatibleGeneratedDomain)
{
    DynamicLibrary Runtime;
    ASSERT_TRUE(BindRuntimeBridge(Runtime));

    DynamicLibrary Module;
    ASSERT_TRUE(Module.Open(ModulePath("HTNTestIncompatibleDomainModule")));
    const auto GetDefinition = Module.Find<const HTNGeneratedPlannerDefinition* (*)()>(
        "CreateBacktrackingPolicyIncompatibleHTN_GetDefinition");
    ASSERT_NE(GetDefinition, nullptr);

    HTNWorldState WorldState;
    HTNPlannerHook PlannerHook(WorldState);
    EXPECT_FALSE(PlannerHook.SetGeneratedPlannerDefinition(GetDefinition()));
}
