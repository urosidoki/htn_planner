// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNCoreMinimal.h"
#include "Core/HtnSymbol.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "Core/HTNCallTermBindingContext.h"
#include "Core/HTNCallTermRegistry.h"
#include "WorldState/HTNGeneratedWorldState.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "Translator/HTNGeneratedProfiling.h"
#include "Translator/HTNAllocationTrace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>
#include <utility>

namespace AllocationTracking
{
constexpr std::size_t MaxTrackedSize = 4096u;

std::atomic<bool> Enabled{false};
std::atomic<uint64_t> Count{0u};
std::atomic<uint64_t> Bytes{0u};
std::array<std::atomic<uint64_t>, MaxTrackedSize + 1u> CountBySize{};
std::atomic<uint64_t> OversizeCount{0u};
std::atomic<uint64_t> OversizeBytes{0u};
constexpr std::size_t SourceCount = static_cast<std::size_t>(HTNAllocationTrace::Source::Count);
std::array<std::atomic<uint64_t>, SourceCount> CountBySource{};
std::array<std::atomic<uint64_t>, SourceCount> BytesBySource{};
constexpr std::size_t PhaseCount = static_cast<std::size_t>(HTNAllocationTrace::Phase::Count);
std::array<std::atomic<uint64_t>, PhaseCount> CountByPhase{};
std::array<std::atomic<uint64_t>, PhaseCount> BytesByPhase{};

void Reset()
{
    Count.store(0u, std::memory_order_relaxed);
    Bytes.store(0u, std::memory_order_relaxed);
    for (auto& Bucket : CountBySize)
        Bucket.store(0u, std::memory_order_relaxed);
    OversizeCount.store(0u, std::memory_order_relaxed);
    OversizeBytes.store(0u, std::memory_order_relaxed);
    for (auto& Counter : CountBySource)
        Counter.store(0u, std::memory_order_relaxed);
    for (auto& Counter : BytesBySource)
        Counter.store(0u, std::memory_order_relaxed);
    for (auto& Counter : CountByPhase)
        Counter.store(0u, std::memory_order_relaxed);
    for (auto& Counter : BytesByPhase)
        Counter.store(0u, std::memory_order_relaxed);
}

void Record(const std::size_t inSize)
{
    Count.fetch_add(1u, std::memory_order_relaxed);
    Bytes.fetch_add(static_cast<uint64_t>(inSize), std::memory_order_relaxed);
    const auto PhaseIndex = static_cast<std::size_t>(HTNAllocationTrace::GetCurrentPhase());
    CountByPhase[PhaseIndex].fetch_add(1u, std::memory_order_relaxed);
    BytesByPhase[PhaseIndex].fetch_add(static_cast<uint64_t>(inSize), std::memory_order_relaxed);
    const std::size_t SourceIndex = static_cast<std::size_t>(HTNAllocationTrace::GetCurrentSource());
    if (SourceIndex < SourceCount)
    {
        CountBySource[SourceIndex].fetch_add(1u, std::memory_order_relaxed);
        BytesBySource[SourceIndex].fetch_add(static_cast<uint64_t>(inSize), std::memory_order_relaxed);
    }
    if (inSize <= MaxTrackedSize)
        CountBySize[inSize].fetch_add(1u, std::memory_order_relaxed);
    else
    {
        OversizeCount.fetch_add(1u, std::memory_order_relaxed);
        OversizeBytes.fetch_add(static_cast<uint64_t>(inSize), std::memory_order_relaxed);
    }
}

struct Scope
{
    Scope()
    {
        Reset();
        Enabled.store(true, std::memory_order_release);
    }

    ~Scope()
    {
        Enabled.store(false, std::memory_order_release);
    }
};
}

#ifdef HTN_BENCHMARK_ALLOCATIONS
void* operator new(const std::size_t inSize)
{
    if (void* Memory = std::malloc(std::max<std::size_t>(inSize, 1u)))
    {
        if (AllocationTracking::Enabled.load(std::memory_order_acquire))
        {
            AllocationTracking::Record(inSize);
        }
        return Memory;
    }
    std::abort(); // Benchmark OOM is fatal; the project does not require exceptions.
}

void* operator new[](const std::size_t inSize) { return ::operator new(inSize); }
void operator delete(void* inMemory) noexcept { std::free(inMemory); }
void operator delete[](void* inMemory) noexcept { std::free(inMemory); }
void operator delete(void* inMemory, std::size_t) noexcept { std::free(inMemory); }
void operator delete[](void* inMemory, std::size_t) noexcept { std::free(inMemory); }

void* operator new(std::size_t inSize, const std::nothrow_t&) noexcept
{
    void* Memory = std::malloc(std::max<std::size_t>(inSize, 1u));
    if (Memory && AllocationTracking::Enabled.load(std::memory_order_acquire))
        AllocationTracking::Record(inSize);
    return Memory;
}
void* operator new[](std::size_t inSize, const std::nothrow_t& inTag) noexcept
{ return ::operator new(inSize, inTag); }
void operator delete(void* inMemory, const std::nothrow_t&) noexcept { std::free(inMemory); }
void operator delete[](void* inMemory, const std::nothrow_t&) noexcept { std::free(inMemory); }

void* operator new(std::size_t inSize, std::align_val_t inAlignment,
                   const std::nothrow_t&) noexcept
{
    const std::size_t Alignment = static_cast<std::size_t>(inAlignment);
    const std::size_t Size = std::max<std::size_t>(inSize, 1u);
    if (Size > std::numeric_limits<std::size_t>::max() - Alignment - sizeof(void*))
        return nullptr;
    std::size_t Space = Size + Alignment;
    void* Raw = std::malloc(Space + sizeof(void*));
    if (!Raw)
        return nullptr;
    void* Memory = static_cast<char*>(Raw) + sizeof(void*);
    if (!std::align(Alignment, Size, Memory, Space))
    {
        std::free(Raw);
        return nullptr;
    }
    std::memcpy(static_cast<char*>(Memory) - sizeof(void*), &Raw, sizeof(Raw));
    if (AllocationTracking::Enabled.load(std::memory_order_acquire))
        AllocationTracking::Record(inSize);
    return Memory;
}
void* operator new(std::size_t inSize, std::align_val_t inAlignment)
{
    if (void* Memory = ::operator new(inSize, inAlignment, std::nothrow))
        return Memory;
    std::abort();
}
void* operator new[](std::size_t inSize, std::align_val_t inAlignment)
{ return ::operator new(inSize, inAlignment); }
void* operator new[](std::size_t inSize, std::align_val_t inAlignment,
                     const std::nothrow_t& inTag) noexcept
{ return ::operator new(inSize, inAlignment, inTag); }
void operator delete(void* inMemory, std::align_val_t) noexcept
{
    if (!inMemory)
        return;
    void* Raw = nullptr;
    std::memcpy(&Raw, static_cast<char*>(inMemory) - sizeof(void*), sizeof(Raw));
    std::free(Raw);
}
void operator delete[](void* inMemory, std::align_val_t inAlignment) noexcept
{ ::operator delete(inMemory, inAlignment); }
void operator delete(void* inMemory, std::size_t, std::align_val_t inAlignment) noexcept
{ ::operator delete(inMemory, inAlignment); }
void operator delete[](void* inMemory, std::size_t, std::align_val_t inAlignment) noexcept
{ ::operator delete(inMemory, inAlignment); }
void operator delete(void* inMemory, std::align_val_t inAlignment, const std::nothrow_t&) noexcept
{ ::operator delete(inMemory, inAlignment); }
void operator delete[](void* inMemory, std::align_val_t inAlignment, const std::nothrow_t&) noexcept
{ ::operator delete(inMemory, inAlignment); }
#endif

extern "C" const HTNGeneratedPlannerDefinition* CreateComplexScenarioHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateWorldstateLookupScenariosHTN_GetDefinition(void);

namespace
{
using Clock = std::chrono::steady_clock;

struct BenchmarkCase
{
    const char* Name;
    const char* Category;
    const char* WorldStateRelativePath;
};

constexpr BenchmarkCase BenchmarkCases[] = {
    {"Idle",       "Small / branch-light",    "WorldStates/Test/complex_scenario_idle.worldstate"},
    {"Combat",     "Branching",               "WorldStates/Test/complex_scenario_combat.worldstate"},
    {"Emergency",  "Alternative branch",      "WorldStates/Test/complex_scenario_emergency.worldstate"},
    {"Mobility",   "Callterm / movement",     "WorldStates/Test/complex_scenario_mobility.worldstate"},
    {"Recovery",   "Conditions / recovery",   "WorldStates/Test/complex_scenario_recovery.worldstate"},
    {"FactHeavy100","Fact-heavy / recursive", "WorldStates/Test/complex_scenario_recursive_100.worldstate"},
};

std::filesystem::path ResolveRepositoryPath(const std::filesystem::path& inRelativePath)
{
    std::filesystem::path Probe = std::filesystem::current_path();
    for (uint32_t Depth = 0u; Depth < 6u; ++Depth)
    {
        const std::filesystem::path Candidate = Probe / inRelativePath;
        if (std::filesystem::exists(Candidate))
            return std::filesystem::absolute(Candidate);

        if (!Probe.has_parent_path() || Probe.parent_path() == Probe)
            break;
        Probe = Probe.parent_path();
    }

    return {};
}

void BindBenchmarkCallTerms(HTNCallTermRegistry& ioRegistry)
{
    ioRegistry.Bind("binded_function_with_args",
        [](const HTNCallTermArguments& inArguments) -> bool
        {
            return inArguments.size() == 1u && HTNAtomIsBound(inArguments[0]);
        });

    ioRegistry.Bind("get_health",
        [](const HTNCallTermArguments& inArguments) -> int
        {
            return inArguments.size() == 1u && HTNAtomIsBound(inArguments[0]) ? 50 : 0;
        });

    ioRegistry.Bind("get_max_speed",
        [](const HTNCallTermArguments& inArguments) -> float
        {
            return inArguments.size() == 1u && HTNAtomIsBound(inArguments[0]) ? 1.0f : 0.0f;
        });

    ioRegistry.Bind("lt",
        [](const HTNCallTermArguments& inArguments) -> bool
        {
            if (inArguments.size() != 2u ||
                !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]) ||
                !HTNAtomIsBound(inArguments[1]) || !HTNAtomIsType<int32>(inArguments[1]))
                return false;
            return HTNAtomGetValue<int32>(inArguments[0]) < HTNAtomGetValue<int32>(inArguments[1]);
        });

    ioRegistry.Bind("inc",
        [](const HTNCallTermArguments& inArguments) -> int
        {
            if (inArguments.size() != 1u ||
                !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]))
                return 0;
            return HTNAtomGetValue<int32>(inArguments[0]) + 1;
        });
}

struct Runner
{
    HTNDatabaseHook Database;
    void* ExecutionStorage = nullptr;
    void* PreparedStorage = nullptr;
    const HTNGeneratedPlannerDefinition* Definition = nullptr;
    std::unique_ptr<HTNCallTermBindingContext> CallTermBindingContext;
    HTNGeneratedPlannerContext Context{};
    const HtnSymbol* TopLevelMethod = nullptr;
    HTNAtom Call{};
    bool HasCall = false;

    ~Runner()
    {
        if (HasCall)
            HTNAtom::sDestroy(Call);
        if (ExecutionStorage && Definition)
        {
            Definition->destroy_execution_storage(ExecutionStorage);
            ::operator delete(ExecutionStorage);
        }
        if (PreparedStorage && Definition)
        {
            Definition->destroy_prepared_storage(PreparedStorage);
            ::operator delete(PreparedStorage);
        }
    }

    bool Initialize(
        const std::string& inWorldStatePath,
        const HTNGeneratedPlannerDefinition& inRegistration,
        const HTNCallTermRegistry& inRegistry)
    {
        if (!Database.ParseWorldStateFile(inWorldStatePath))
        {
            std::cerr << "Benchmark initialization failed: could not parse world state:\n  "
                      << inWorldStatePath << "\n";
            return false;
        }

        Definition = &inRegistration;
        CallTermBindingContext = std::make_unique<HTNCallTermBindingContext>(inRegistry);

        PreparedStorage = ::operator new(Definition->prepared_storage_size, std::nothrow);
        if (!PreparedStorage || !Definition->initialize_prepared_storage(PreparedStorage))
        {
            ::operator delete(PreparedStorage);
            PreparedStorage = nullptr;
            std::cerr << "Benchmark initialization failed: prepared storage initialization failed.\n";
            return false;
        }

        ExecutionStorage = ::operator new(Definition->execution_storage_size, std::nothrow);
        if (!ExecutionStorage || !Definition->initialize_execution_storage(ExecutionStorage))
        {
            ::operator delete(ExecutionStorage);
            ExecutionStorage = nullptr;
            std::cerr << "Benchmark initialization failed: execution storage initialization failed.\n";
            return false;
        }

        TopLevelMethod = HtnSymbol::sGetSymbol("run_scenario");
        Context.world_state = &Database.GetWorldState();
        Context.callterm_binding_context = CallTermBindingContext.get();
        Context.backtracking_mode = HTN_BACKTRACKING_ALL;
        Context.execution_storage = ExecutionStorage;
        Context.prepared_storage = PreparedStorage;
        Call = HTNAtom::sCreateCall(TopLevelMethod);
        HasCall = HTNAtom_IsBound(&Call) != 0;

        if (!inRegistration.decompose_call)
        {
            std::cerr << "Benchmark initialization failed: generated registration has no entry point.\n";
            return false;
        }

        // Warm-up: resolve fact metadata and grow reusable scratch/trails before any measurement.
        HTNAtom WarmupPlan{};
        const HTNDecompositionStatus WarmupResult = HasCall
            ? inRegistration.decompose_call(&Context, &Call, 1, &WarmupPlan)
            : HTN_DECOMPOSITION_OUT_OF_MEMORY;
        if (HasCall)
            HTNAtom::sDestroy(WarmupPlan);
        if (!HasCall || WarmupResult != HTN_DECOMPOSITION_SUCCEEDED)
        {
            std::cerr << "Benchmark initialization failed: generated planner warm-up returned failure.\n";
            return false;
        }

        return true;
    }

    bool Execute(const HTNGeneratedPlannerDefinition& inRegistration)
    {
        return ExecuteEntryPoint(inRegistration);
    }

    bool ExecuteEntryPoint(const HTNGeneratedPlannerDefinition& inRegistration)
    {
        if (!HasCall)
            return false;
        HTNAtom Plan{};
        const HTNDecompositionStatus Result = inRegistration.decompose_call(&Context, &Call, 1, &Plan);
        HTNAtom::sDestroy(Plan);
        return Result == HTN_DECOMPOSITION_SUCCEEDED;
    }
};

struct LifecyclePhase
{
    double Microseconds = 0.0;
    uint64_t Allocations = 0u;
    uint64_t Bytes = 0u;
    std::array<uint64_t, AllocationTracking::PhaseCount> PhaseCalls{}, PhaseBytes{};
    std::array<uint64_t, AllocationTracking::SourceCount> SourceCalls{}, SourceBytes{};
    std::array<uint64_t, AllocationTracking::MaxTrackedSize + 1u> SizeCalls{};
    uint64_t OversizeCalls = 0u, OversizeBytes = 0u;
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    uint64_t StringAllocations = 0u, StringFrees = 0u, NodeAllocations = 0u, NodeFrees = 0u;
#endif
};

template<typename TFunction>
void MeasureLifecyclePhase(LifecyclePhase& ioPhase, bool inAllocationPass, TFunction&& inFunction)
{
    // Counter reset and reporting are never part of the measured operation.
    AllocationTracking::Reset();
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    const auto AtomsBefore = inAllocationPass ? HTNAtomDebug_GetStats() : HTNAtomDebugStats{};
#endif
    AllocationTracking::Enabled.store(inAllocationPass, std::memory_order_release);
    const auto Start = Clock::now();
    inFunction();
    const auto End = Clock::now();
    AllocationTracking::Enabled.store(false, std::memory_order_release);
    if (inAllocationPass)
    {
        ioPhase.Allocations = AllocationTracking::Count.load(std::memory_order_relaxed);
        ioPhase.Bytes = AllocationTracking::Bytes.load(std::memory_order_relaxed);
        for (std::size_t I = 0; I < ioPhase.PhaseCalls.size(); ++I)
        {
            ioPhase.PhaseCalls[I] = AllocationTracking::CountByPhase[I].load(std::memory_order_relaxed);
            ioPhase.PhaseBytes[I] = AllocationTracking::BytesByPhase[I].load(std::memory_order_relaxed);
        }
        for (std::size_t I = 0; I < ioPhase.SourceCalls.size(); ++I)
        {
            ioPhase.SourceCalls[I] = AllocationTracking::CountBySource[I].load(std::memory_order_relaxed);
            ioPhase.SourceBytes[I] = AllocationTracking::BytesBySource[I].load(std::memory_order_relaxed);
        }
        for (std::size_t I = 0; I < ioPhase.SizeCalls.size(); ++I)
            ioPhase.SizeCalls[I] = AllocationTracking::CountBySize[I].load(std::memory_order_relaxed);
        ioPhase.OversizeCalls = AllocationTracking::OversizeCount.load(std::memory_order_relaxed);
        ioPhase.OversizeBytes = AllocationTracking::OversizeBytes.load(std::memory_order_relaxed);
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
        const auto AtomsAfter = HTNAtomDebug_GetStats();
        ioPhase.StringAllocations = AtomsAfter.heap_string_allocations - AtomsBefore.heap_string_allocations;
        ioPhase.StringFrees = AtomsAfter.heap_string_frees - AtomsBefore.heap_string_frees;
        ioPhase.NodeAllocations = AtomsAfter.list_node_allocations - AtomsBefore.list_node_allocations;
        ioPhase.NodeFrees = AtomsAfter.list_node_deallocations - AtomsBefore.list_node_deallocations;
#endif
    }
    else
    {
        ioPhase.Microseconds = std::chrono::duration<double, std::micro>(End - Start).count();
    }
}

uint64_t RunLifecycleSuite(const HTNGeneratedPlannerDefinition& inDefinition,
                          const HTNCallTermRegistry& inRegistry,
                          uint64_t inIterations,
                          bool inIncludeHeavy)
{
    const HtnSymbol* Method = HtnSymbol::sGetSymbol("run_scenario");
    uint64_t Failures = 0u;
    std::cout << "\nGenerated public API lifecycle: entity-cold setup / first plan / warmed plans\n"
              << "Timing and allocation passes are separate; fixture parsing is its own phase.\n"
              << "scenario,backend,phase,timing_ops,allocation_ops,us/op,new_calls/op,new_bytes/op,failures\n";
#if !defined(HTN_PROFILE_DETAILED) && defined(HTN_BENCHMARK_ALLOCATIONS)
    std::cerr << "Phase/source attribution disabled: use ProfileDetailed; size histograms remain available.\n";
#endif
    for (const BenchmarkCase& Case : BenchmarkCases)
    {
        const bool FactHeavy = std::string(Case.Name) == "FactHeavy100";
        if (FactHeavy && !inIncludeHeavy)
        {
            std::cerr << "FactHeavy100 skipped: use --lifecycle-heavy.\n";
            continue;
        }
        const uint64_t CaseIterations = FactHeavy ? std::min<uint64_t>(inIterations, 5u) : inIterations;
        const auto WorldStatePath = ResolveRepositoryPath(Case.WorldStateRelativePath);
        if (WorldStatePath.empty())
        {
            std::cerr << "Lifecycle fixture missing: " << Case.Name << '\n';
            return Failures + 1u;
        }
        LifecyclePhase Fixture, Setup, First, Steady;
        uint64_t CaseFailures = 0u;
        HTNAtomOwner ExpectedPlan;
        for (bool AllocationPass : {false, true})
        {
#ifndef HTN_BENCHMARK_ALLOCATIONS
            if (AllocationPass) continue;
#endif
            HTNDatabaseHook Database;
            bool Loaded = false;
            MeasureLifecyclePhase(Fixture, AllocationPass, [&] { Loaded = Database.ParseWorldStateFile(WorldStatePath.string()); });
            if (!Loaded) return Failures + CaseFailures + 1u;
            std::unique_ptr<HTNPlannerHook> Hook;
            std::unique_ptr<HTNPlanningUnit> Unit;
            bool Initialized = false;
            MeasureLifecyclePhase(Setup, AllocationPass, [&]
            {
                Hook = std::make_unique<HTNPlannerHook>(Database.GetWorldState(), inRegistry);
                Initialized = Hook->SetGeneratedPlannerDefinition(&inDefinition);
                if (Initialized) Unit = std::make_unique<HTNPlanningUnit>(Database, *Hook, Method);
            });
            if (!Initialized) return Failures + CaseFailures + 1u;
            auto Decompose = [&] { return Unit->DecomposeTopLevelMethod(); };
            MeasureLifecyclePhase(First, AllocationPass, [&] { CaseFailures += Decompose() != HTN_DECOMPOSITION_SUCCEEDED; });
            if (!AllocationPass) ExpectedPlan = Unit->GetLastDecomposition().GetResult();
            if (Unit->GetLastDecomposition().GetResult() != ExpectedPlan) ++CaseFailures;
            for (uint32_t I = 0; I < (FactHeavy ? 1u : 16u); ++I) CaseFailures += Decompose() != HTN_DECOMPOSITION_SUCCEEDED;
            const uint64_t Count = AllocationPass ? std::min<uint64_t>(CaseIterations, 1000u) : CaseIterations;
            MeasureLifecyclePhase(Steady, AllocationPass, [&]
            {
                for (uint64_t I = 0; I < Count; ++I) CaseFailures += Decompose() != HTN_DECOMPOSITION_SUCCEEDED;
            });
            if (Unit->GetLastDecomposition().GetResult() != ExpectedPlan) ++CaseFailures;
        }
#ifdef HTN_BENCHMARK_ALLOCATIONS
        const uint64_t ProbeCount = std::min<uint64_t>(CaseIterations, 1000u);
#endif
        const LifecyclePhase Phases[] = {Fixture, Setup, First, Steady};
        const char* Names[] = {"worldstate", "setup", "first", "steady"};
        for (std::size_t Index = 0; Index < std::size(Phases); ++Index)
        {
            const double TimingCount = Index == 3u ? static_cast<double>(CaseIterations) : 1.0;
#ifdef HTN_BENCHMARK_ALLOCATIONS
            const double AllocationCount = Index == 3u ? static_cast<double>(ProbeCount) : 1.0;
#endif
            std::cout << Case.Name << ",generated," << Names[Index] << ',' << std::fixed << std::setprecision(3)
                      << TimingCount << ','
#ifdef HTN_BENCHMARK_ALLOCATIONS
                      << AllocationCount << ',' << Phases[Index].Microseconds / TimingCount << ','
                      << Phases[Index].Allocations / AllocationCount << ',' << Phases[Index].Bytes / AllocationCount << ','
#else
                      << "n/a," << Phases[Index].Microseconds / TimingCount << ",n/a,n/a,"
#endif
                      << CaseFailures << '\n';
        }
#ifdef HTN_BENCHMARK_ALLOCATIONS
        const double Denominator = static_cast<double>(ProbeCount);
        auto Report = [&](const char* View, const char* Name, uint64_t Calls, uint64_t Bytes)
        {
            if (Calls) std::cout << "allocation_detail," << Case.Name << ",generated,steady," << View << ',' << Name << ','
                                 << ProbeCount << ',' << Calls / Denominator << ',' << Bytes / Denominator << '\n';
        };
        uint64_t PhaseCalls=0, PhaseBytes=0, SourceCalls=0, SourceBytes=0, SizeCalls=Steady.OversizeCalls, SizeBytes=Steady.OversizeBytes;
        for (std::size_t I=0; I<Steady.PhaseCalls.size(); ++I) { PhaseCalls+=Steady.PhaseCalls[I]; PhaseBytes+=Steady.PhaseBytes[I]; Report("execution_phase",HTNAllocationTrace::GetPhaseName(static_cast<HTNAllocationTrace::Phase>(I)),Steady.PhaseCalls[I],Steady.PhaseBytes[I]); }
        for (std::size_t I=0; I<Steady.SourceCalls.size(); ++I) { SourceCalls+=Steady.SourceCalls[I]; SourceBytes+=Steady.SourceBytes[I]; Report("source",HTNAllocationTrace::GetSourceName(static_cast<HTNAllocationTrace::Source>(I)),Steady.SourceCalls[I],Steady.SourceBytes[I]); }
        for (std::size_t I=0; I<Steady.SizeCalls.size(); ++I) { SizeCalls+=Steady.SizeCalls[I]; SizeBytes+=I*Steady.SizeCalls[I]; const std::string Size=std::to_string(I); Report("requested_size",Size.c_str(),Steady.SizeCalls[I],I*Steady.SizeCalls[I]); }
        Report("requested_size",">4096",Steady.OversizeCalls,Steady.OversizeBytes);
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
        std::cout << "atom_detail," << Case.Name << ",generated,steady," << ProbeCount << ',' << Steady.StringAllocations/Denominator << ',' << Steady.StringFrees/Denominator << ',' << Steady.NodeAllocations/Denominator << ',' << Steady.NodeFrees/Denominator << '\n';
#endif
        if (PhaseCalls!=Steady.Allocations||PhaseBytes!=Steady.Bytes||SourceCalls!=Steady.Allocations||SourceBytes!=Steady.Bytes||SizeCalls!=Steady.Allocations||SizeBytes!=Steady.Bytes) { std::cerr << "Allocation attribution reconciliation failed: " << Case.Name << '\n'; ++CaseFailures; }
#endif
        Failures += CaseFailures;
    }
    return Failures;
}

struct TimingResult
{
    uint64_t Iterations = 0u;
    uint64_t Failures = 0u;
    double Seconds = 0.0;
};

struct LatencyStats
{
    uint64_t Samples = 0u;
    uint64_t Failures = 0u;
    double P50Microseconds = 0.0;
    double P95Microseconds = 0.0;
    double P99Microseconds = 0.0;
    double MaxMicroseconds = 0.0;
};

struct AllocationStats
{
    uint64_t Iterations = 0u;
    uint64_t Failures = 0u;
    uint64_t Count = 0u;
    uint64_t Bytes = 0u;
};

struct AllocationAttributionStats
{
    uint64_t Iterations = 0u;
    uint64_t Failures = 0u;
    uint64_t ExecutionCount = 0u;
    uint64_t ExecutionBytes = 0u;
};

struct AllocationHistogramEntry
{
    std::size_t Size = 0u;
    uint64_t Count = 0u;
};

struct AllocationHistogramStats
{
    uint64_t Iterations = 0u;
    uint64_t Failures = 0u;
    uint64_t Count = 0u;
    uint64_t Bytes = 0u;
    uint64_t OversizeCount = 0u;
    uint64_t OversizeBytes = 0u;
    std::array<uint64_t, AllocationTracking::SourceCount> CountBySource{};
    std::array<uint64_t, AllocationTracking::SourceCount> BytesBySource{};
    std::vector<AllocationHistogramEntry> Entries;
};

struct WorldStateScalingStats
{
    uint64_t Queries = 0u;
    double NanosecondsPerQuery = 0.0;
    double CandidatesPerQuery = 0.0;
    double AllocationsPerQuery = 0.0;
    double BytesPerQuery = 0.0;
};

TimingResult RunTimed(Runner& ioRunner, const HTNGeneratedPlannerDefinition& inRegistration, const uint64_t inIterations)
{
    const auto Begin = Clock::now();
    uint64_t Failures = 0u;
    for (uint64_t I = 0u; I < inIterations; ++I)
    {
        if (!ioRunner.Execute(inRegistration))
            ++Failures;
    }
    const auto End = Clock::now();

    return {inIterations, Failures, std::chrono::duration<double>(End - Begin).count()};
}

double PercentileMicroseconds(const std::vector<uint64_t>& inSortedNanoseconds, const double inPercentile)
{
    if (inSortedNanoseconds.empty())
        return 0.0;

    const double Rank = std::ceil(inPercentile * static_cast<double>(inSortedNanoseconds.size()));
    const size_t Index = static_cast<size_t>(std::clamp<double>(Rank - 1.0, 0.0,
        static_cast<double>(inSortedNanoseconds.size() - 1u)));
    return static_cast<double>(inSortedNanoseconds[Index]) / 1000.0;
}

LatencyStats RunLatencySamples(
    Runner& ioRunner,
    const HTNGeneratedPlannerDefinition& inRegistration,
    const uint64_t inSamples)
{
    std::vector<uint64_t> Nanoseconds;
    Nanoseconds.reserve(static_cast<size_t>(inSamples));

    uint64_t Failures = 0u;
    for (uint64_t I = 0u; I < inSamples; ++I)
    {
        const auto Begin = Clock::now();
        const bool Succeeded = ioRunner.Execute(inRegistration);
        const auto End = Clock::now();

        if (!Succeeded)
            ++Failures;

        Nanoseconds.emplace_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(End - Begin).count()));
    }

    std::sort(Nanoseconds.begin(), Nanoseconds.end());

    LatencyStats Result;
    Result.Samples = inSamples;
    Result.Failures = Failures;
    Result.P50Microseconds = PercentileMicroseconds(Nanoseconds, 0.50);
    Result.P95Microseconds = PercentileMicroseconds(Nanoseconds, 0.95);
    Result.P99Microseconds = PercentileMicroseconds(Nanoseconds, 0.99);
    Result.MaxMicroseconds = Nanoseconds.empty() ? 0.0 : static_cast<double>(Nanoseconds.back()) / 1000.0;
    return Result;
}

AllocationStats RunAllocationProbe(
    Runner& ioRunner,
    const HTNGeneratedPlannerDefinition& inRegistration,
    const uint64_t inIterations)
{
    AllocationStats Result;
    Result.Iterations = inIterations;

    {
        AllocationTracking::Scope Scope;
        for (uint64_t I = 0u; I < inIterations; ++I)
        {
            if (!ioRunner.Execute(inRegistration))
                ++Result.Failures;
        }
        AllocationTracking::Enabled.store(false, std::memory_order_release);
        Result.Count = AllocationTracking::Count.load(std::memory_order_relaxed);
        Result.Bytes = AllocationTracking::Bytes.load(std::memory_order_relaxed);
    }

    return Result;
}

AllocationAttributionStats RunAllocationAttribution(
    Runner& ioRunner,
    const HTNGeneratedPlannerDefinition& inRegistration,
    const uint64_t inIterations)
{
    AllocationAttributionStats Result;
    Result.Iterations = inIterations;

    for (uint64_t I = 0u; I < inIterations; ++I)
    {
        AllocationTracking::Reset();
        AllocationTracking::Enabled.store(true, std::memory_order_release);
        const bool Succeeded = ioRunner.ExecuteEntryPoint(inRegistration);
        AllocationTracking::Enabled.store(false, std::memory_order_release);
        Result.ExecutionCount += AllocationTracking::Count.load(std::memory_order_relaxed);
        Result.ExecutionBytes += AllocationTracking::Bytes.load(std::memory_order_relaxed);
        if (!Succeeded)
            ++Result.Failures;
    }

    return Result;
}

AllocationHistogramStats RunAllocationHistogram(
    Runner& ioRunner,
    const HTNGeneratedPlannerDefinition& inRegistration,
    const uint64_t inIterations)
{
    AllocationHistogramStats Result;
    Result.Iterations = inIterations;

    AllocationTracking::Reset();
    AllocationTracking::Enabled.store(true, std::memory_order_release);
    for (uint64_t I = 0u; I < inIterations; ++I)
    {
        // Match the normal execution path: reset first, then dispatch. Reset is
        // allocation-free in the current baseline, but keeping it inside this
        // probe makes the histogram describe the complete per-plan operation.
        if (!ioRunner.Execute(inRegistration))
            ++Result.Failures;
    }
    AllocationTracking::Enabled.store(false, std::memory_order_release);

    Result.Count = AllocationTracking::Count.load(std::memory_order_relaxed);
    Result.Bytes = AllocationTracking::Bytes.load(std::memory_order_relaxed);
    Result.OversizeCount = AllocationTracking::OversizeCount.load(std::memory_order_relaxed);
    Result.OversizeBytes = AllocationTracking::OversizeBytes.load(std::memory_order_relaxed);
    for (std::size_t I = 0u; I < AllocationTracking::SourceCount; ++I)
    {
        Result.CountBySource[I] = AllocationTracking::CountBySource[I].load(std::memory_order_relaxed);
        Result.BytesBySource[I] = AllocationTracking::BytesBySource[I].load(std::memory_order_relaxed);
    }

    // Tracking is disabled while the result vector grows, so the probe never
    // measures its own reporting infrastructure.
    for (std::size_t Size = 0u; Size <= AllocationTracking::MaxTrackedSize; ++Size)
    {
        const uint64_t Count = AllocationTracking::CountBySize[Size].load(std::memory_order_relaxed);
        if (Count != 0u)
            Result.Entries.push_back({Size, Count});
    }

    std::sort(Result.Entries.begin(), Result.Entries.end(),
        [](const AllocationHistogramEntry& A, const AllocationHistogramEntry& B)
        {
            if (A.Count != B.Count)
                return A.Count > B.Count;
            return A.Size < B.Size;
        });
    return Result;
}

void PrintAllocationHistogram(const char* inScenario, const AllocationHistogramStats& inStats)
{
    const double Iterations = static_cast<double>(std::max<uint64_t>(1u, inStats.Iterations));
    std::cout << "\nAllocation size histogram: " << inScenario
              << " (" << inStats.Iterations << " plans)\n"
              << "  total: " << std::fixed << std::setprecision(4)
              << (static_cast<double>(inStats.Count) / Iterations) << " alloc/plan, "
              << std::setprecision(2) << (static_cast<double>(inStats.Bytes) / Iterations) << " B/plan\n"
              << std::right << std::setw(10) << "size B"
              << std::setw(14) << "count"
              << std::setw(16) << "alloc/plan"
              << std::setw(16) << "bytes/plan"
              << std::setw(14) << "% allocs" << "\n";

    for (const AllocationHistogramEntry& Entry : inStats.Entries)
    {
        const double PerPlan = static_cast<double>(Entry.Count) / Iterations;
        const double BytesPerPlan = static_cast<double>(Entry.Count * Entry.Size) / Iterations;
        const double Percentage = inStats.Count > 0u
            ? 100.0 * static_cast<double>(Entry.Count) / static_cast<double>(inStats.Count) : 0.0;
        std::cout << std::right << std::setw(10) << Entry.Size
                  << std::setw(14) << Entry.Count
                  << std::setw(16) << std::fixed << std::setprecision(4) << PerPlan
                  << std::setw(16) << std::setprecision(2) << BytesPerPlan
                  << std::setw(13) << std::setprecision(2) << Percentage << "%\n";
    }

    if (inStats.OversizeCount != 0u)
    {
        std::cout << std::right << std::setw(10) << ">4096"
                  << std::setw(14) << inStats.OversizeCount
                  << std::setw(16) << std::fixed << std::setprecision(4)
                  << (static_cast<double>(inStats.OversizeCount) / Iterations)
                  << std::setw(16) << std::setprecision(2)
                  << (static_cast<double>(inStats.OversizeBytes) / Iterations)
                  << std::setw(13) << std::setprecision(2)
                  << (inStats.Count > 0u ? 100.0 * static_cast<double>(inStats.OversizeCount) / static_cast<double>(inStats.Count) : 0.0)
                  << "%\n";
    }
}


void PrintAllocationSourceAttribution(const char* inScenario, const AllocationHistogramStats& inStats)
{
    std::cout << "\nAllocation source attribution: " << inScenario
              << " (" << inStats.Iterations << " plans)\n";
#ifdef HTN_PROFILE_DETAILED
    const double Iterations = static_cast<double>(std::max<uint64_t>(1u, inStats.Iterations));
    std::cout << std::left << std::setw(38) << "source"
              << std::right << std::setw(14) << "count"
              << std::setw(16) << "alloc/plan"
              << std::setw(16) << "bytes/plan"
              << std::setw(14) << "% allocs" << "\n";

    for (std::size_t I = 0u; I < AllocationTracking::SourceCount; ++I)
    {
        const uint64_t Count = inStats.CountBySource[I];
        if (Count == 0u)
            continue;
        const uint64_t Bytes = inStats.BytesBySource[I];
        const auto Source = static_cast<HTNAllocationTrace::Source>(I);
        const double Percentage = inStats.Count > 0u
            ? 100.0 * static_cast<double>(Count) / static_cast<double>(inStats.Count) : 0.0;
        std::cout << std::left << std::setw(38) << HTNAllocationTrace::GetSourceName(Source)
                  << std::right << std::setw(14) << Count
                  << std::setw(16) << std::fixed << std::setprecision(4)
                  << (static_cast<double>(Count) / Iterations)
                  << std::setw(16) << std::setprecision(2)
                  << (static_cast<double>(Bytes) / Iterations)
                  << std::setw(13) << std::setprecision(2) << Percentage << "%\n";
    }
#else
    std::cout << "  source tags are compiled only in ProfileDetailed; rebuild that configuration\n"
              << "  to attribute the histogram without affecting normal Profile measurements.\n";
#endif
}

void PrintSuiteHeader()
{
    std::cout << "\nBaseline suite\n"
              << std::left << std::setw(14) << "scenario"
              << std::setw(25) << "category"
              << std::right << std::setw(13) << "plans/s"
              << std::setw(12) << "us/plan"
              << std::setw(10) << "p50 us"
              << std::setw(10) << "p95 us"
              << std::setw(10) << "p99 us"
              << std::setw(10) << "max us"
              << std::setw(12) << "alloc/plan"
              << std::setw(13) << "bytes/plan"
              << std::setw(11) << "failures" << "\n";
}

void PrintSuiteRow(
    const BenchmarkCase& inCase,
    const TimingResult& inTiming,
    const LatencyStats& inLatency,
    const AllocationStats& inAllocations)
{
    const double PlansPerSecond = inTiming.Seconds > 0.0
        ? static_cast<double>(inTiming.Iterations) / inTiming.Seconds : 0.0;
    const double MicrosecondsPerPlan = inTiming.Iterations > 0u
        ? (inTiming.Seconds * 1'000'000.0) / static_cast<double>(inTiming.Iterations) : 0.0;
    const double AllocationsPerPlan = inAllocations.Iterations > 0u
        ? static_cast<double>(inAllocations.Count) / static_cast<double>(inAllocations.Iterations) : 0.0;
    const double BytesPerPlan = inAllocations.Iterations > 0u
        ? static_cast<double>(inAllocations.Bytes) / static_cast<double>(inAllocations.Iterations) : 0.0;
    const uint64_t Failures = inTiming.Failures + inLatency.Failures + inAllocations.Failures;

    std::cout << std::left << std::setw(14) << inCase.Name
              << std::setw(25) << inCase.Category
              << std::right << std::setw(13) << std::fixed << std::setprecision(2) << PlansPerSecond
              << std::setw(12) << std::setprecision(3) << MicrosecondsPerPlan
              << std::setw(10) << inLatency.P50Microseconds
              << std::setw(10) << inLatency.P95Microseconds
              << std::setw(10) << inLatency.P99Microseconds
              << std::setw(10) << inLatency.MaxMicroseconds
              << std::setw(12) << std::setprecision(4) << AllocationsPerPlan
              << std::setw(13) << std::setprecision(2) << BytesPerPlan
              << std::setw(11) << Failures << "\n";
}

void PrintAllocationAttribution(
    const char* inScenario,
    const AllocationAttributionStats& inStats)
{
    const double Iterations = static_cast<double>(std::max<uint64_t>(1u, inStats.Iterations));
    std::cout << "  " << std::left << std::setw(14) << inScenario
              << "execution=" << std::right << std::fixed << std::setprecision(4)
              << (static_cast<double>(inStats.ExecutionCount) / Iterations) << " alloc, "
              << std::setprecision(2) << (static_cast<double>(inStats.ExecutionBytes) / Iterations) << " B"
              << "  failures=" << inStats.Failures << "\n";
}

struct GeneratedWorldStateScenario
{
    const char* Name;
    size RowsPerFact;
    uint64_t Iterations;
};

constexpr GeneratedWorldStateScenario GeneratedWorldStateScenarios[] = {
    {"TypicalGameAI",       5u,   10000u},
    {"CrowdAI",            50u,   5000u},
    {"LargeFactDatabase", 500u,    500u},
};

void RegisterGeneratedWorldStateScenarioFacts(HTNFactRegistry& ioRegistry)
{
    static constexpr const char* FactNames[] = {
        "active_entity",
        "entity_state",
        "entity_target",
        "entity_squad",
        "entity_weapon",
        "entity_cover",
        "entity_route",
        "target_status",
        "squad_order",
        "weapon_status",
        "cover_quality",
        "route_status",
        "ammo_available",
    };

    for (const char* FactName : FactNames)
        ioRegistry.Register(HtnSymbol::sGetSymbol(FactName));
}

bool RebuildGeneratedWorldStateScenario(HTNWorldState& ioWorldState, const size inRowsPerFact)
{
    ioWorldState.RemoveAllFacts();

    const HtnSymbol* ActiveEntity = HtnSymbol::sGetSymbol("active_entity");
    const HtnSymbol* EntityState = HtnSymbol::sGetSymbol("entity_state");
    const HtnSymbol* EntityTarget = HtnSymbol::sGetSymbol("entity_target");
    const HtnSymbol* EntitySquad = HtnSymbol::sGetSymbol("entity_squad");
    const HtnSymbol* EntityWeapon = HtnSymbol::sGetSymbol("entity_weapon");
    const HtnSymbol* EntityCover = HtnSymbol::sGetSymbol("entity_cover");
    const HtnSymbol* EntityRoute = HtnSymbol::sGetSymbol("entity_route");
    const HtnSymbol* TargetStatus = HtnSymbol::sGetSymbol("target_status");
    const HtnSymbol* SquadOrder = HtnSymbol::sGetSymbol("squad_order");
    const HtnSymbol* WeaponStatus = HtnSymbol::sGetSymbol("weapon_status");
    const HtnSymbol* CoverQuality = HtnSymbol::sGetSymbol("cover_quality");
    const HtnSymbol* RouteStatus = HtnSymbol::sGetSymbol("route_status");
    const HtnSymbol* AmmoAvailable = HtnSymbol::sGetSymbol("ammo_available");

    // The selected entity is deliberately the last row. This avoids accidentally
    // flattering linear lookup by always placing the bound match at row zero.
    const int32 ActiveEntityId = static_cast<int32>(inRowsPerFact - 1u);
    if (!ioWorldState.WriteFact(ActiveEntity, ActiveEntityId))
        return false;

    for (size Row = 0u; Row < inRowsPerFact; ++Row)
    {
        const int32 Id = static_cast<int32>(Row);
        if (!ioWorldState.WriteFact(EntityState, Id, static_cast<int32>(Row % 4u)) ||
            !ioWorldState.WriteFact(EntityTarget, Id, Id) ||
            !ioWorldState.WriteFact(EntitySquad, Id, Id) ||
            !ioWorldState.WriteFact(EntityWeapon, Id, Id) ||
            !ioWorldState.WriteFact(EntityCover, Id, Id) ||
            !ioWorldState.WriteFact(EntityRoute, Id, Id) ||
            !ioWorldState.WriteFact(TargetStatus, Id, int32(1)) ||
            !ioWorldState.WriteFact(SquadOrder, Id, int32(2)) ||
            !ioWorldState.WriteFact(WeaponStatus, Id, int32(1)) ||
            !ioWorldState.WriteFact(CoverQuality, Id, int32(3)) ||
            !ioWorldState.WriteFact(RouteStatus, Id, int32(1)) ||
            !ioWorldState.WriteFact(AmmoAvailable, Id))
            return false;
    }

    return true;
}

void RunGeneratedWorldStateScenarioBenchmark(
    const HTNGeneratedPlannerDefinition& inRegistration,
    const HTNCallTermRegistry& inRegistry)
{
    const std::filesystem::path InitialWorldStatePath =
        ResolveRepositoryPath("WorldStates/Test/worldstate_lookup_scenarios.worldstate");
    if (InitialWorldStatePath.empty())
    {
        std::cout << "\nGenerated WorldState scenarios: SKIPPED (world state not found)\n";
        return;
    }

    std::cout << "\nGenerated WorldState gameplay scenarios (real generated decomposition)\n"
              << "  each frame performs RemoveAllFacts -> full WriteFact snapshot -> generated Decompose\n"
              << "  domain performs hierarchical method/axiom decomposition with bound fact lookups\n"
              << "  active entity is the final row so linear lookup must traverse the table for bound matches\n"
              << "  scenario              rows/fact  fact types    iterations      us/frame      frames/s   failures\n";

    for (const GeneratedWorldStateScenario& Scenario : GeneratedWorldStateScenarios)
    {
        Runner ScenarioRunner;
        if (!ScenarioRunner.Initialize(InitialWorldStatePath.string(), inRegistration, inRegistry))
        {
            std::cout << "  " << std::left << std::setw(20) << Scenario.Name
                      << " initialization failed\n";
            continue;
        }

        HTNFactRegistry FactRegistry;
        RegisterGeneratedWorldStateScenarioFacts(FactRegistry);
        HTNWorldState& WorldState = ScenarioRunner.Database.GetWorldState();
        WorldState.SetFactRegistry(&FactRegistry);

        if (!RebuildGeneratedWorldStateScenario(WorldState, Scenario.RowsPerFact) ||
            !ScenarioRunner.Execute(inRegistration))
        {
            std::cout << "  " << std::left << std::setw(20) << Scenario.Name
                      << " warm-up failed\n";
            continue;
        }

        uint64_t Failures = 0u;
        const auto Begin = Clock::now();
        for (uint64_t Iteration = 0u; Iteration < Scenario.Iterations; ++Iteration)
        {
            if (!RebuildGeneratedWorldStateScenario(WorldState, Scenario.RowsPerFact) ||
                !ScenarioRunner.Execute(inRegistration))
                ++Failures;
        }
        const double Seconds = std::chrono::duration<double>(Clock::now() - Begin).count();
        const double MicrosecondsPerFrame = Seconds * 1.0e6 / static_cast<double>(Scenario.Iterations);
        const double FramesPerSecond = static_cast<double>(Scenario.Iterations) / Seconds;

        std::cout << "  " << std::left << std::setw(20) << Scenario.Name
                  << std::right << std::setw(10) << Scenario.RowsPerFact
                  << std::setw(12) << 13
                  << std::setw(14) << Scenario.Iterations
                  << std::setw(14) << std::fixed << std::setprecision(3) << MicrosecondsPerFrame
                  << std::setw(14) << std::fixed << std::setprecision(2) << FramesPerSecond
                  << std::setw(11) << Failures << "\n";
    }
}

const char* ProfileCategoryName(const uint32_t inCategory)
{
    static const char* Names[] = {
        "fact", "worldstate_count", "worldstate_check", "axiom", "callterm",
        "and", "or", "alt", "not", "resolve_value", "metadata_lookup",
        "environment_copy", "binding_trail", "enter_compound", "pending_push",
        "pending_pop", "generated_method", "generated_branch", "generated_task",
        "generated_dispatch", "continuation_trail", "branch_setup",
        "branch_condition_cfg", "branch_task_scheduling", "branch_commit",
        "branch_retry", "fact_cursor_setup", "fact_scan_unify",
        "condition_choice_backtrack", "axiom_control", "call_control"
    };
    static_assert(sizeof(Names) / sizeof(Names[0]) == HTN_GENERATED_PROFILE_CATEGORY_COUNT);
    return inCategory < HTN_GENERATED_PROFILE_CATEGORY_COUNT ? Names[inCategory] : "unknown";
}

void PrintDetailedProfile(Runner& ioRunner, const HTNGeneratedPlannerDefinition& inRegistration, const uint64_t inIterations)
{
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    HTNGeneratedProfiling_SetEnabled(ioRunner.Definition->get_execution_profiling(ioRunner.ExecutionStorage), 1);
    HTNGeneratedProfiling_ResetProfile(ioRunner.Definition->get_execution_profiling(ioRunner.ExecutionStorage));

#ifdef HTN_GENERATED_EXECUTION_PROFILING
    uint64_t StructuralEventTotals[HTN_GENERATED_STRUCTURAL_EVENT_COUNT]{};
#endif

    for (uint64_t I = 0u; I < inIterations; ++I)
    {
        (void)ioRunner.Execute(inRegistration);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
        HTNGeneratedStructuralCounters CurrentStructural{};
        if (HTNGeneratedProfiling_GetStructuralCounters(ioRunner.Definition->get_execution_profiling(ioRunner.ExecutionStorage), &CurrentStructural))
        {
            for (uint32_t Event = 0u; Event < HTN_GENERATED_STRUCTURAL_EVENT_COUNT; ++Event)
                StructuralEventTotals[Event] += CurrentStructural.events[Event];
        }
#endif
    }

    std::cout << "\nDetailed generated-execution profile: FactHeavy100 (" << inIterations << " plans)\n";
    std::cout << std::left << std::setw(30) << "category"
              << std::right << std::setw(14) << "calls"
              << std::setw(16) << "self ms"
              << std::setw(16) << "inclusive ms" << "\n";

    for (uint32_t Category = 0u; Category < HTN_GENERATED_PROFILE_CATEGORY_COUNT; ++Category)
    {
        HTNGeneratedProfileSample Sample{};
        if (!HTNGeneratedProfiling_GetProfileSample(ioRunner.Definition->get_execution_profiling(ioRunner.ExecutionStorage), Category, &Sample) || Sample.calls == 0u)
            continue;

        std::cout << std::left << std::setw(30) << ProfileCategoryName(Category)
                  << std::right << std::setw(14) << Sample.calls
                  << std::setw(16) << std::fixed << std::setprecision(3)
                  << static_cast<double>(Sample.self_nanoseconds) / 1'000'000.0
                  << std::setw(16)
                  << static_cast<double>(Sample.nanoseconds) / 1'000'000.0 << "\n";
    }

#ifdef HTN_GENERATED_EXECUTION_PROFILING
    static const char* EventNames[] = {
        "fact_queries",
        "fact_rows_tested",
        "fact_rows_matched",
        "fact_choice_points",
        "fact_choice_retries",
        "checkpoint_pushes",
        "checkpoint_rollbacks",
        "checkpoint_commits"
    };
    static_assert(sizeof(EventNames) / sizeof(EventNames[0]) == HTN_GENERATED_STRUCTURAL_EVENT_COUNT);

    std::cout << "\nStructural events (total / per plan)\n";
    for (uint32_t Event = 0u; Event < HTN_GENERATED_STRUCTURAL_EVENT_COUNT; ++Event)
    {
        const uint64_t Total = StructuralEventTotals[Event];
        const double PerPlan = inIterations > 0u
            ? static_cast<double>(Total) / static_cast<double>(inIterations) : 0.0;
        std::cout << "  " << std::left << std::setw(24) << EventNames[Event]
                  << std::right << std::setw(12) << Total
                  << "  " << std::fixed << std::setprecision(2) << PerPlan << "/plan\n";
    }
#endif

    HTNGeneratedProfiling_SetEnabled(ioRunner.Definition->get_execution_profiling(ioRunner.ExecutionStorage), 0);
#else
    (void)ioRunner;
    (void)inRegistration;
    (void)inIterations;
    std::cout << "\nDetailed category profiling is disabled in this configuration.\n"
                 "Build HTNBenchmark with ProfileDetailed for generated-execution categories and structural counters.\n";
#endif
}

void RunThreadScaling(
    const std::string& inWorldStatePath,
    const HTNGeneratedPlannerDefinition& inRegistration,
    const HTNCallTermRegistry& inRegistry,
    const uint64_t inIterationsPerThread,
    const uint32_t inMaxThreads)
{
    std::cout << "\nParallel scaling: FactHeavy100 (independent WorldState + execution storage per worker)\n";

    std::vector<uint32_t> ThreadCounts{1u, 2u, 4u, 8u};
    ThreadCounts.erase(
        std::remove_if(ThreadCounts.begin(), ThreadCounts.end(),
            [inMaxThreads](const uint32_t Count) { return Count > inMaxThreads; }),
        ThreadCounts.end());

    double BaselineThroughput = 0.0;
    for (const uint32_t ThreadCount : ThreadCounts)
    {
        std::vector<std::unique_ptr<Runner>> Runners;
        Runners.reserve(ThreadCount);
        for (uint32_t I = 0u; I < ThreadCount; ++I)
        {
            auto Worker = std::make_unique<Runner>();
            if (!Worker->Initialize(inWorldStatePath, inRegistration, inRegistry))
            {
                std::cerr << "Failed to initialize benchmark worker " << I << "\n";
                return;
            }
            Runners.emplace_back(std::move(Worker));
        }

        std::atomic<uint32_t> Ready{0u};
        std::atomic<bool> Start{false};
        std::atomic<uint64_t> Failures{0u};
        std::vector<std::thread> Threads;
        Threads.reserve(ThreadCount);

        for (uint32_t I = 0u; I < ThreadCount; ++I)
        {
            Threads.emplace_back([&, I]()
            {
                Ready.fetch_add(1u, std::memory_order_release);
                while (!Start.load(std::memory_order_acquire))
                    std::this_thread::yield();

                uint64_t LocalFailures = 0u;
                for (uint64_t Iteration = 0u; Iteration < inIterationsPerThread; ++Iteration)
                {
                    if (!Runners[I]->Execute(inRegistration))
                        ++LocalFailures;
                }
                Failures.fetch_add(LocalFailures, std::memory_order_relaxed);
            });
        }

        while (Ready.load(std::memory_order_acquire) != ThreadCount)
            std::this_thread::yield();

        const auto Begin = Clock::now();
        Start.store(true, std::memory_order_release);
        for (std::thread& Thread : Threads)
            Thread.join();
        const auto End = Clock::now();

        const double Seconds = std::chrono::duration<double>(End - Begin).count();
        const uint64_t TotalPlans = inIterationsPerThread * ThreadCount;
        const double Throughput = Seconds > 0.0 ? static_cast<double>(TotalPlans) / Seconds : 0.0;
        if (ThreadCount == 1u)
            BaselineThroughput = Throughput;

        std::cout << "  " << ThreadCount << " thread(s): "
                  << std::fixed << std::setprecision(2) << Throughput << " plans/s"
                  << "  speedup=" << std::setprecision(2)
                  << (BaselineThroughput > 0.0 ? Throughput / BaselineThroughput : 0.0)
                  << "x  failures=" << Failures.load(std::memory_order_relaxed) << "\n";
    }
}
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--allocation-self-test")
    {
#ifdef HTN_BENCHMARK_ALLOCATIONS
        bool Valid = false;
        {
            AllocationTracking::Scope Probe;
            void* Zero = ::operator new(0u);
            void* Array;
            void* Aligned;
            {
                HTNAllocationTrace::PhaseScope Outer(HTNAllocationTrace::Phase::CallConstruction);
                {
                    HTNAllocationTrace::PhaseScope Inner(HTNAllocationTrace::Phase::ActivePlanCopy);
                    HTNAllocationTrace::Scope Source(HTNAllocationTrace::Source::ListMaterialization);
                    Array = ::operator new[](23u, std::nothrow);
                }
                Aligned = ::operator new(97u, std::align_val_t(64u), std::nothrow);
            }
            Valid = Array && Aligned && reinterpret_cast<std::uintptr_t>(Aligned) % 64u == 0u &&
                    AllocationTracking::Count.load() == 3u && AllocationTracking::Bytes.load() == 120u;
#ifdef HTN_PROFILE_DETAILED
            Valid = Valid && HTNAllocationTrace::GetCurrentPhase() == HTNAllocationTrace::Phase::None &&
                    HTNAllocationTrace::GetCurrentSource() == HTNAllocationTrace::Source::None &&
                    AllocationTracking::BytesByPhase[static_cast<std::size_t>(HTNAllocationTrace::Phase::ActivePlanCopy)].load() == 23u &&
                    AllocationTracking::BytesByPhase[static_cast<std::size_t>(HTNAllocationTrace::Phase::CallConstruction)].load() == 97u &&
                    AllocationTracking::BytesBySource[static_cast<std::size_t>(HTNAllocationTrace::Source::ListMaterialization)].load() == 23u;
#endif
            ::operator delete(Zero);
            ::operator delete[](Array, std::nothrow);
            ::operator delete(Aligned, std::align_val_t(64u), std::nothrow);
        }
        std::cout << "Allocation probe self-test: " << (Valid ? "PASS" : "FAIL") << '\n';
        return Valid ? 0 : 5;
#else
        std::cerr << "Allocation probe disabled: define HTN_BENCHMARK_ALLOCATIONS.\n";
        return 5;
#endif
    }
    const uint64_t Iterations = argc > 1 ? std::max<uint64_t>(1u, std::strtoull(argv[1], nullptr, 10)) : 10000u;
    const uint64_t LatencySamples = std::min<uint64_t>(Iterations, 5000u);
    const uint64_t ProfileIterations = std::min<uint64_t>(Iterations, 1000u);
    const uint64_t AllocationIterations = std::min<uint64_t>(Iterations, 1000u);

    const uint32_t HardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const uint32_t MaxThreads = argc > 2
        ? std::max<uint32_t>(1u, static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)))
        : std::min<uint32_t>(8u, HardwareThreads);

    const HTNGeneratedPlannerDefinition* Registration = CreateComplexScenarioHTN_GetDefinition();
    const HTNGeneratedPlannerDefinition* WorldStateScenarioRegistration = CreateWorldstateLookupScenariosHTN_GetDefinition();
    if (!Registration || !Registration->decompose_call)
    {
        std::cerr << "Generated domain 'ComplexScenario' was not registered.\n";
        return 1;
    }
    if (!WorldStateScenarioRegistration || !WorldStateScenarioRegistration->decompose_call)
    {
        std::cerr << "Generated domain 'WorldstateLookupScenarios' was not registered.\n";
        return 1;
    }

    HTNCallTermRegistry CallTermRegistry;
    BindBenchmarkCallTerms(CallTermRegistry);
#ifndef HTN_BENCHMARK_ALLOCATIONS
    std::cout << "Allocation probes compiled out; legacy allocation columns are unavailable.\n";
#endif
    // Optional focused run: existing direct-entry and WorldState experiments remain available.
    if (argc > 3 && (std::string(argv[3]) == "--lifecycle" ||
                    std::string(argv[3]) == "--lifecycle-heavy"))
    {
        return RunLifecycleSuite(*Registration, CallTermRegistry, Iterations,
                                 std::string(argv[3]) == "--lifecycle-heavy") == 0u ? 0 : 4;
    }
    std::cout << "HTN generated planner baseline benchmark\n"
              << "  domain:            ComplexScenario\n"
              << "  iterations/case:   " << Iterations << "\n"
              << "  latency samples:   " << LatencySamples << "\n"
              << "  allocation probes: " << AllocationIterations << "\n"
              << "  max threads:       " << MaxThreads << "\n"
#ifdef HTN_PROFILE_DETAILED
              << "  build:             ProfileDetailed\n";
#elif defined(HTN_PROFILE)
              << "  build:             Profile\n";
#elif defined(HTN_RELEASE)
              << "  build:             Release\n";
#else
              << "  build:             Debug/other (timings are NOT representative)\n";
#endif

    std::cout << "  fact lookup:       linear rows\n";


    PrintSuiteHeader();

    uint64_t TotalFailures = 0u;
    std::filesystem::path FactHeavyWorldStatePath;
    AllocationAttributionStats IdleAllocationAttribution{};
    AllocationAttributionStats FactHeavyAllocationAttribution{};
    AllocationHistogramStats IdleAllocationHistogram{};
    AllocationHistogramStats FactHeavyAllocationHistogram{};

    for (const BenchmarkCase& Case : BenchmarkCases)
    {
        const std::filesystem::path WorldStatePath = ResolveRepositoryPath(Case.WorldStateRelativePath);
        if (WorldStatePath.empty())
        {
            std::cerr << "Could not locate " << Case.WorldStateRelativePath << ".\n"
                      << "Current working directory: " << std::filesystem::current_path().string() << "\n";
            return 2;
        }

        Runner CaseRunner;
        if (!CaseRunner.Initialize(WorldStatePath.string(), *Registration, CallTermRegistry))
        {
            std::cerr << "Failed to initialize benchmark scenario '" << Case.Name << "'.\n";
            return 3;
        }

        const TimingResult Timing = RunTimed(CaseRunner, *Registration, Iterations);
        const LatencyStats Latency = RunLatencySamples(CaseRunner, *Registration, LatencySamples);
        const AllocationStats Allocations = RunAllocationProbe(CaseRunner, *Registration, AllocationIterations);
        PrintSuiteRow(Case, Timing, Latency, Allocations);

        TotalFailures += Timing.Failures + Latency.Failures + Allocations.Failures;

        if (std::string(Case.Name) == "Idle")
        {
            IdleAllocationAttribution = RunAllocationAttribution(
                CaseRunner, *Registration, AllocationIterations);
            IdleAllocationHistogram = RunAllocationHistogram(
                CaseRunner, *Registration, AllocationIterations);
            TotalFailures += IdleAllocationAttribution.Failures + IdleAllocationHistogram.Failures;
        }
        else if (std::string(Case.Name) == "FactHeavy100")
        {
            FactHeavyAllocationAttribution = RunAllocationAttribution(
                CaseRunner, *Registration, AllocationIterations);
            FactHeavyAllocationHistogram = RunAllocationHistogram(
                CaseRunner, *Registration, AllocationIterations);
            TotalFailures += FactHeavyAllocationAttribution.Failures + FactHeavyAllocationHistogram.Failures;
            FactHeavyWorldStatePath = WorldStatePath;
            PrintDetailedProfile(CaseRunner, *Registration, ProfileIterations);
        }
    }

    std::cout << "\nAllocation attribution (per plan, after warm-up)\n"
                 "  execution = generated entry point, including direct execution reset and cache preparation\n";
    PrintAllocationAttribution("Idle", IdleAllocationAttribution);
    PrintAllocationAttribution("FactHeavy100", FactHeavyAllocationAttribution);
    PrintAllocationHistogram("Idle", IdleAllocationHistogram);
    PrintAllocationSourceAttribution("Idle", IdleAllocationHistogram);
    PrintAllocationHistogram("FactHeavy100", FactHeavyAllocationHistogram);
    PrintAllocationSourceAttribution("FactHeavy100", FactHeavyAllocationHistogram);

    RunGeneratedWorldStateScenarioBenchmark(*WorldStateScenarioRegistration, CallTermRegistry);

    if (!FactHeavyWorldStatePath.empty())
    {
        RunThreadScaling(
            FactHeavyWorldStatePath.string(),
            *Registration,
            CallTermRegistry,
            std::max<uint64_t>(100u, Iterations / 10u),
            MaxThreads);
    }

    std::cout << "\nBaseline result: " << (TotalFailures == 0u ? "PASS" : "FAIL")
              << "  total_failures=" << TotalFailures << "\n";

    return TotalFailures == 0u ? 0 : 4;
}
