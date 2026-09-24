// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "HTNPlanner.h"
#include "Core/HTNPlannerExecutionContext.h"

#include <cstdio>
#include <new>

extern "C" const HTNGeneratedPlannerDefinition* CreatePackageCoreConsumerHTN_GetDefinition(void);

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

struct MissingReport
{
    int Count = 0;
    HTNMissingCallTermReason Reason{};
    std::string Name;
};

static void ReportMissing(void* inClient, const HTNMissingCallTermInfo* inInfo)
{
    auto& Report = *static_cast<MissingReport*>(inClient);
    ++Report.Count;
    Report.Reason = inInfo->Reason;
    Report.Name = inInfo->Name;
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
    const HTNMissingCallTermReason Reasons[] = {HTNMissingCallTermReason::NotRegistered,
        HTNMissingCallTermReason::MissingBinding, HTNMissingCallTermReason::MissingInstance};
    const std::vector<HTNAtomOwner> Arguments;
    const auto Check = [&](HTNMissingCallTermPolicy inPolicy, HTNMissingCallTermCallback inCallback)
    {
        Context.MissingCallTermPolicy = inPolicy;
        Context.MissingCallTermCallback = inCallback;
        HTNGeneratedPlannerContext Generated{};
        Generated.callterm_binding_context = &Bindings;
        Generated.client_context = &Report;
        Generated.missing_callterm_policy = inPolicy;
        Generated.missing_callterm_callback = inCallback;
        for (size_t I = 0; I < 3; ++I)
        {
            const int Before = Report.Count;
            if (Registry.Execute(Names[I], Context, Arguments).IsBound()) return false;
            HTNAtomOwner Result;
            const auto Call = HTNCallTermRegistry_ResolveGeneratedCallTerm(&Bindings, Names[I]);
            if (HTNCallTermRegistry_InvokeGeneratedCallTermWithSource(&Generated, &Call, nullptr, 0, Result.Get(), nullptr) ||
                Result.IsBound()) return false;
            const bool Reports = inPolicy == HTNMissingCallTermPolicy::Report && inCallback;
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
    if (!Check(HTNMissingCallTermPolicy::FailSilently, ReportMissing) ||
        !Check(HTNMissingCallTermPolicy::Report, ReportMissing)) return false;
#ifdef NDEBUG
    // Use return values, not assert: both this consumer and the Release SDK must
    // exercise the production fallback with assertions compiled out.
    if (!Check(HTNMissingCallTermPolicy::Unset, ReportMissing) ||
        !Check(static_cast<HTNMissingCallTermPolicy>(99), ReportMissing) ||
        !Check(HTNMissingCallTermPolicy::Report, nullptr)) return false;
    std::puts("Missing callterm production fallbacks: PASS (NDEBUG)");
#endif
    return true;
}

int main()
{
    if (!ValidateMissingCallTerms()) return 11;
    if (!ValidateFactWrites()) return 10;
    const HTNGeneratedPlannerDefinition* Definition = CreatePackageCoreConsumerHTN_GetDefinition();
    if (!HTNGeneratedPlanner_ValidateDefinition(Definition))
        return 1;

    HTNCallTermRegistry Registry;
    HTNCallTermBindingContext BindingContext(Registry);
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
    Context.missing_callterm_policy = HTNMissingCallTermPolicy::FailSilently;
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

#ifdef HTN_DEBUG_DECOMPOSITION
    std::puts("Core-only external package consumer: PASS (debug decomposition captured)");
#else
    std::puts("Core-only external package consumer: PASS (release)");
#endif
    return Finish(0);
}
