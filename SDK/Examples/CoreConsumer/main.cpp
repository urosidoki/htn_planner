// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "HTNPlanner.h"
#include "Core/HTNPlannerExecutionContext.h"
#include "Core/HTNAtomListAllocator.h"

#include <cstdio>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>

extern "C" const HTNGeneratedPlannerDefinition* CreatePackageCoreConsumerHTN_GetDefinition(void);

// The client owns the marker. Deallocate only tracks releases; it does not reset
// the arena. This also exercises callbacks across the dynamic-domain bridge.
struct PackageBacktrackingScratch
{
    alignas(std::max_align_t) std::array<std::byte, 128 * 1024> Buffer;
    size_t Used = 0, Allocations = 0, Releases = 0, RequestedBytes = 0;
    static void* Allocate(void* inUser, size_t inSize, size_t inAlignment)
    {
        auto& Self = *static_cast<PackageBacktrackingScratch*>(inUser);
        void* Memory = Self.Buffer.data() + Self.Used;
        size_t Space = Self.Buffer.size() - Self.Used;
        if (!std::align(inAlignment, inSize, Memory, Space)) return nullptr;
        Self.Used = static_cast<std::byte*>(Memory) - Self.Buffer.data() + inSize;
        Self.RequestedBytes += inSize;
        ++Self.Allocations;
        return Memory;
    }
    static void Deallocate(void* inUser, void*, size_t, size_t)
    {
        ++static_cast<PackageBacktrackingScratch*>(inUser)->Releases;
    }
};

struct PackageFactValue { int32 Value; bool Fail; };
template<> struct HTNTypeTraits<PackageFactValue> : HTNTypeTraits<int32> {};
template<> struct HTNTypeConverter<PackageFactValue>
{
    static bool ToAtom(void* inContext, const PackageFactValue& inValue, HTNAtom& outAtom)
    {
        if (inValue.Fail || !inContext) return false;
        return HTNTryToAtom(inContext, inValue.Value + *static_cast<int32*>(inContext), outAtom);
    }
};

static bool ValidateFactWrites()
{
    HTNFactRegistry Registry;
    const auto* Fact = HtnSymbol::sGetSymbol("package_fact_write");
    Registry.Register(Fact);
    HTNWorldState World;
    World.SetFactRegistry(&Registry);
    int32 Offset = 10;
    if (!World.WriteFact(Fact, "native", 1) ||
        !World.WriteFactWithContext(&Offset, Fact, PackageFactValue{2, false}) ||
        World.WriteFactWithContext(&Offset, Fact, "temporary", PackageFactValue{3, true}, 4) ||
        World.WriteFact(Fact, PackageFactValue{3, false})) return false;
    const std::array<HTNAtomOwner, 1> Expected{HTNAtomOwner(int32{12})};
    return World.Query("package_fact_write", Expected) == 1u &&
        World.GetFactArgumentsCollectionSize("package_fact_write", 1u) == 1u &&
        World.GetFactArgumentsCollectionSize("package_fact_write", 2u) == 1u &&
        World.GetFactArgumentsCollectionSize("package_fact_write", 3u) == 0u;
}

static bool ValidateBooleanCompatibility()
{
    HTNFactRegistry Registry;
    const auto* Fact = HtnSymbol::sGetSymbol("package_boolean");
    Registry.Register(Fact);
    HTNWorldState World;
    World.SetFactRegistry(&Registry);
    bool Parsed = false;
    if (!HTNTryParseType(HTNAtomOwner(1), Parsed) || !Parsed ||
        !HTNTryParseType(HTNAtomOwner(0), Parsed) || Parsed ||
        HTNTryParseType(HTNAtomOwner(2), Parsed) || HTNTryParseType(HTNAtomOwner(1.0f), Parsed)) return false;
    if (!World.WriteFact(Fact, true, false) ||
        !World.ContainsFactArguments("package_boolean", std::array<HTNAtomOwner, 2>{1, 0})) return false;
    const auto& Row = World.FindFactArgumentsTables(Fact)->at(2).GetFactArgumentsCollection().front();
    return Row[0].IsType<bool>() && Row[1].IsType<bool>();
}

#ifdef HTN_DEBUG_DECOMPOSITION
static bool ValidateUnregisteredFactWrites()
{
    HTNFactRegistry Registry;
    HTNWorldState World;
    World.SetFactRegistry(&Registry);
    const auto* Fact = HtnSymbol::sGetSymbol("package_unregistered_fact");
    int32 Offset = 10;
    if (World.WriteFact(Fact) || World.WriteFactWithContext(&Offset, Fact, PackageFactValue{2, false})) return false;
    const auto It = World.GetUnregisteredFacts().find(Fact);
    if (It == World.GetUnregisteredFacts().end() || !World.GetFacts().empty() ||
        World.FindFactArgumentsTables(Fact) || World.GetFactArgumentsCollectionSize("package_unregistered_fact", 1u) != 0u ||
        It->second[0].GetFactArgumentsCollectionSize() != 1u || It->second[1].GetFactArgumentsCollectionSize() != 1u ||
        It->second[1].GetFactArgumentsCollection().front()[0].GetValue<int32>() != 12) return false;
    if (World.ClearFact(Fact, 1u) || It->second[1].GetFactArgumentsCollectionSize() != 0u ||
        It->second[0].GetFactArgumentsCollectionSize() != 1u) return false;
    World.RemoveAllFacts();
    return It->second[0].GetFactArgumentsCollectionSize() == 0u;
}
#endif

struct MissingReport
{
    int Count = 0;
    HTNCallTermErrorReason Reason{};
    std::string Name;
    uint32_t ActualType = UINT32_MAX;
    uint32_t ExpectedType = UINT32_MAX;
    std::string File;
    uint32_t Line = 0, Column = 0;
};

static void ReportMissing(void* inClient, const HTNCallTermErrorInfo* inInfo)
{
    auto& Report = *static_cast<MissingReport*>(inClient);
    ++Report.Count;
    Report.Reason = inInfo->Reason;
    Report.Name = inInfo->Name;
    Report.ActualType = inInfo->ActualAtomType;
    Report.ExpectedType = inInfo->ExpectedAtomType;
    Report.File = inInfo->Source.file ? inInfo->Source.file : "";
    Report.Line = inInfo->Source.line;
    Report.Column = inInfo->Source.column;
}

static bool ValidateMissingCallTerms()
{
    HTNCallTermRegistry Registry;
    if (!Registry.BindMember("empty", "agent", {}, {}) ||
        !Registry.BindMember("member", "agent", [](void*, const HTNCallTermArguments&) { return HTNAtomOwner(true); }, {}))
        return false;
    Registry.Bind("valid", [](const HTNCallTermArguments&) { return HTNAtomOwner(true); });
    HTNCallTermBindingContext Bindings(Registry);
    MissingReport Report;
    HTNPlannerExecutionContext Context{};
    Context.CallTermBindingContext = &Bindings;
    Context.ClientContext = &Report;
    const char* Names[] = {"absent", "empty", "member"};
    const HTNCallTermErrorReason Reasons[] = {HTNCallTermErrorReason::NotRegistered,
        HTNCallTermErrorReason::MissingBinding, HTNCallTermErrorReason::MissingInstance};
    const std::vector<HTNAtomOwner> Arguments;
    const auto Check = [&](HTNCallTermErrorPolicy inPolicy, HTNCallTermErrorCallback inCallback)
    {
        Context.CallTermErrorPolicy = inPolicy;
        Context.CallTermErrorCallback = inCallback;
        HTNGeneratedPlannerContext Generated{};
        Generated.callterm_binding_context = &Bindings;
        Generated.client_context = &Report;
        Generated.callterm_error_policy = inPolicy;
        Generated.callterm_error_callback = inCallback;
        for (size_t I = 0; I < 3; ++I)
        {
            const int Before = Report.Count;
            if (Registry.Execute(Names[I], Context, Arguments).IsBound()) return false;
            HTNAtomOwner Result;
            const auto Call = HTNCallTermRegistry_ResolveGeneratedCallTerm(&Bindings, Names[I]);
            if (HTNCallTermRegistry_InvokeGeneratedCallTermWithSource(&Generated, &Call, nullptr, 0, Result.Get(), nullptr) ||
                Result.IsBound()) return false;
            const bool Reports = inPolicy == HTNCallTermErrorPolicy::Report && inCallback;
            if (Report.Count != Before + (Reports ? 2 : 0)) return false;
            if (Reports && (Report.Reason != Reasons[I] || Report.Name != Names[I])) return false;
        }
        const int Before = Report.Count;
        HTNAtomOwner Result;
        const auto Call = HTNCallTermRegistry_ResolveGeneratedCallTerm(&Bindings, "valid");
        return Registry.Execute("valid", Context, Arguments).IsBound() &&
            HTNCallTermRegistry_InvokeGeneratedCallTermWithSource(&Generated, &Call, nullptr, 0, Result.Get(), nullptr) &&
            Result.IsBound() && Report.Count == Before;
    };
    if (!Check(HTNCallTermErrorPolicy::FailSilently, ReportMissing) ||
        !Check(HTNCallTermErrorPolicy::Report, ReportMissing)) return false;
#ifdef NDEBUG
    // Use return values, not assert: both this consumer and the Release SDK must
    // exercise the production fallback with assertions compiled out.
    if (!Check(HTNCallTermErrorPolicy::Unset, ReportMissing) ||
        !Check(static_cast<HTNCallTermErrorPolicy>(99), ReportMissing) ||
        !Check(HTNCallTermErrorPolicy::Report, nullptr)) return false;
    std::puts("Missing callterm production fallbacks: PASS (NDEBUG)");
#endif
    return true;
}

int main()
{
#ifdef HTN_DEBUG_DECOMPOSITION
    if (!ValidateUnregisteredFactWrites()) return 15;
    std::puts("Unregistered fact inspection: PASS");
#endif
    if (!ValidateMissingCallTerms()) return 11;
    if (!ValidateFactWrites()) return 10;
    if (!ValidateBooleanCompatibility()) return 16;
    std::puts("Boolean and binary integer compatibility: PASS");
    const HTNGeneratedPlannerDefinition* Definition = CreatePackageCoreConsumerHTN_GetDefinition();
    if (!HTNGeneratedPlanner_ValidateDefinition(Definition))
        return 1;

    HTNCallTermRegistry Registry;
    HTNCallTermBindingContext BindingContext(Registry);
    HTNAtomOwner ConditionValue(42);
    int ConditionCalls = 0;
    Registry.Bind("condition_value", [&](const HTNCallTermArguments&) { ++ConditionCalls; return ConditionValue; });
    HTNWorldState WorldState;
    void* PreparedStorage = ::operator new(Definition->prepared_storage_size, std::nothrow);
    void* ExecutionStorage = ::operator new(Definition->execution_storage_size, std::nothrow);
    bool PreparedInitialized = false;
    bool ExecutionInitialized = false;

    const auto Finish = [&](const int inResult)
    {
        if (ExecutionInitialized)
            Definition->destroy_execution_storage(ExecutionStorage);
        if (PreparedInitialized)
            Definition->destroy_prepared_storage(PreparedStorage);
        ::operator delete(ExecutionStorage);
        ::operator delete(PreparedStorage);
        return inResult;
    };

    if (!PreparedStorage || !ExecutionStorage)
        return Finish(2);

    PreparedInitialized = Definition->initialize_prepared_storage(PreparedStorage) != 0;
    ExecutionInitialized = Definition->initialize_execution_storage(ExecutionStorage) != 0;
    if (!PreparedInitialized || !ExecutionInitialized)
        return Finish(3);

    HTNGeneratedPlannerContext Context{};
    Context.callterm_error_policy = HTNCallTermErrorPolicy::FailSilently;
    Context.world_state = &WorldState;
    Context.callterm_binding_context = &BindingContext;
    Context.backtracking_mode = HTN_BACKTRACKING_ALL;
    Context.prepared_storage = PreparedStorage;
    Context.execution_storage = ExecutionStorage;
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNGeneratedDebugger Debugger;
    Debugger.SetEnabled(true);
    Context.debugger = &Debugger;
#endif

    HTNAtom Call = HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("run"));
    HTNAtom Candidate;
    const HTNDecompositionStatus Status = Definition->decompose_call(&Context, &Call, 1, &Candidate);
    bool Valid = Status == HTN_DECOMPOSITION_SUCCEEDED && HTNAtom_GetListSize(&Candidate) == 3;
#ifdef HTN_DEBUG_DECOMPOSITION
    Valid = Valid && !Debugger.GetNodes().empty() && Debugger.GetNodes().front().Completed;
#endif
    HTNAtom::sDestroy(Candidate);
    HTNAtom::sDestroy(Call);

    if (!Valid)
        return Finish(4);

    // Minimal per-instance pool example; the plan is destroyed before its pool.
    {
        HTNPooledAtomListAllocator Pool(256);
        HTNGeneratedPlannerContext PooledContext = Context;
        PooledContext.list_allocator = &Pool;
        {
            HTNAtomOwner PooledCall(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("run")));
            HTNAtomOwner PooledPlan;
            if (Definition->decompose_call(&PooledContext, PooledCall.Get(), 1, PooledPlan.Get()) != HTN_DECOMPOSITION_SUCCEEDED ||
                PooledPlan.Get()->value.list_value.allocator != &Pool || Pool.GetAllocatedNodeCount() == 0)
                return Finish(17);
        }
        if (Pool.GetAllocatedNodeCount() != 0) return Finish(18);
    }
    std::puts("Per-instance fixed list allocator: PASS");

    {
        PackageBacktrackingScratch Scratch;
        HTNBacktrackingAllocator Backtracking{
            &Scratch, PackageBacktrackingScratch::Allocate, PackageBacktrackingScratch::Deallocate};
        HTNGeneratedPlannerContext ScratchContext = Context;
        ScratchContext.backtracking_allocator = &Backtracking;
        HTNAtomOwner ScratchCall(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("run_backtracking"), 80));
        HTNAtomOwner RetainedPlan;
        if (Definition->decompose_call(&ScratchContext, ScratchCall.Get(), 1, RetainedPlan.Get()) != HTN_DECOMPOSITION_SUCCEEDED)
            return Finish(19);
        const auto Stats = Definition->get_execution_info(ExecutionStorage)->backtracking_allocations;
        if (Stats.allocation_count == 0 || Stats.allocation_count != Scratch.Allocations ||
            Scratch.Releases != Scratch.Allocations || Stats.current_bytes != 0 ||
            Stats.peak_bytes != Scratch.RequestedBytes || Stats.failed_allocation_count != 0) return Finish(20);
        // Only the client resets the scratch, after checking the retained metrics.
        std::memset(Scratch.Buffer.data(), 0xcd, Scratch.Used);
        Scratch.Used = 0;
        if (RetainedPlan.GetListSize() != 161) return Finish(21);
    }
    std::puts("Backtracking scratch allocator and retained usage statistics: PASS");

    MissingReport Report;
    Context.client_context = &Report;
    const auto CheckCondition = [&](HTNCallTermErrorPolicy Policy, HTNCallTermErrorCallback Callback, bool ExpectSuccess)
    {
        Context.callterm_error_policy = Policy;
        Context.callterm_error_callback = Callback;
        const int Before = Report.Count;
        const int BeforeCalls = ConditionCalls;
        HTNAtomOwner ConditionCall(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("boolean_condition")));
        HTNAtomOwner Result;
        const auto Status = Definition->decompose_call(&Context, ConditionCall.Get(), 1, Result.Get());
        if (Status != (ExpectSuccess ? HTN_DECOMPOSITION_SUCCEEDED : HTN_DECOMPOSITION_NO_PLAN) || ConditionCalls != BeforeCalls + 1)
            return false;
        if (!ExpectSuccess && (Result.Get()->type != HTN_ATOM_TYPE_LIST || Result.GetListSize() != 0)) return false;
        const bool ShouldReport = ConditionValue.Get()->type != HTN_ATOM_TYPE_BOOL && Policy == HTNCallTermErrorPolicy::Report && Callback;
        if (Report.Count != Before + (ShouldReport ? 1 : 0)) return false;
        return !ShouldReport || (Report.Reason == HTNCallTermErrorReason::NonBooleanConditionResult &&
            Report.Name == "condition_value" && Report.ActualType == HTN_ATOM_TYPE_INT && Report.ExpectedType == HTN_ATOM_TYPE_BOOL &&
            Report.File.find("example.domain") != std::string::npos && Report.Line != 0 && Report.Column != 0);
    };
    if (!CheckCondition(HTNCallTermErrorPolicy::Report, ReportMissing, false) ||
        !CheckCondition(HTNCallTermErrorPolicy::FailSilently, ReportMissing, false)) return Finish(11);
#ifdef NDEBUG
    if (!CheckCondition(HTNCallTermErrorPolicy::Unset, nullptr, false) ||
        !CheckCondition(HTNCallTermErrorPolicy::Report, nullptr, false)) return Finish(12);
#endif
    ConditionValue = HTNAtomOwner(false);
    if (!CheckCondition(HTNCallTermErrorPolicy::Report, ReportMissing, false)) return Finish(13);
    ConditionValue = HTNAtomOwner(true);
    if (!CheckCondition(HTNCallTermErrorPolicy::Report, ReportMissing, true)) return Finish(14);
    std::puts("Standalone callterm condition validation: PASS");

#ifdef HTN_DEBUG_DECOMPOSITION
    std::puts("Core-only external package consumer: PASS (debug decomposition captured)");
#else
    std::puts("Core-only external package consumer: PASS (release)");
#endif
    return Finish(0);
}
