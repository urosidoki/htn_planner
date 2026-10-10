// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNPlanner.h"
#include "Translator/HTNRuntimeBridge.h"
#include "Core/HTNAtomListAllocator.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
using ModuleHandle = HMODULE;
static ModuleHandle OpenModule(const char* inPath)
{
    return LoadLibraryExA(inPath, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
}
static auto FindSymbol(ModuleHandle inModule, const char* inName) { return GetProcAddress(inModule, inName); }
static bool CloseModule(ModuleHandle inModule) { return FreeLibrary(inModule) != 0; }
#else
#include <dlfcn.h>
using ModuleHandle = void*;
static ModuleHandle OpenModule(const char* inPath)
{
    ModuleHandle Module = dlopen(inPath, RTLD_NOW | RTLD_LOCAL);
    if (!Module) std::fprintf(stderr, "Cannot load %s: %s\n", inPath, dlerror());
    return Module;
}
static auto FindSymbol(ModuleHandle inModule, const char* inName) { return dlsym(inModule, inName); }
static bool CloseModule(ModuleHandle inModule) { return dlclose(inModule) == 0; }
#endif

// Reuse the core consumer's plan and debugger assertions through a loaded entry point.
using GetDefinitionFn = const HTNGeneratedPlannerDefinition* (*)();
static GetDefinitionFn LoadedDefinition = nullptr;
extern "C" const HTNGeneratedPlannerDefinition* CreatePackageCoreConsumerHTN_GetDefinition()
{
    return LoadedDefinition();
}
#define main RunCoreConsumer
#include "../CoreConsumer/main.cpp"
#undef main

static bool RunCoverage(GetDefinitionFn inGet)
{
    HTNSafePooledAtomListAllocator Allocator(4, HTNNewDeleteAtomListAllocator::Get());
    const auto* Definition = inGet();
    if (!HTNGeneratedPlanner_ValidateDefinition(Definition)) return false;
    HTNFactRegistry Facts;
    for (uint32_t I = 0; I < Definition->fact_count; ++I)
        Facts.Register(HtnSymbol::sGetSymbol(Definition->fact_names[I]));
    HTNWorldState World;
    World.SetFactRegistry(&Facts);
    if (!World.WriteFact(HtnSymbol::sGetSymbol("candidate"), 1) ||
        !World.WriteFact(HtnSymbol::sGetSymbol("candidate"), 2)) return false;
    int Calls = 0;
    HTNCallTermRegistry Registry;
    Registry.Bind("probe", [&Calls](const HTNCallTermArguments&) { ++Calls; return HTNAtomOwner(true); });
    Registry.Bind("identity", [](const HTNCallTermArguments&) { return HTNAtomOwner(2); });
    Registry.Bind("boolean_value", [](const HTNCallTermArguments& inArguments) {
        return HTNAtomOwner(HTNAtomGetValue<int32>(inArguments[0]) != 0);
    });
    Registry.Bind("empty_list", [](const HTNCallTermArguments&) {
        HTNAtomOwner Result;
        HTNAtom_SetEmptyList(Result.Get());
        return Result;
    });
    HTNCallTermBindingContext Bindings(Registry);
    void* Prepared = ::operator new(Definition->prepared_storage_size, std::nothrow);
    void* Execution = ::operator new(Definition->execution_storage_size, std::nothrow);
    const bool PreparedReady = Prepared && Definition->initialize_prepared_storage(Prepared);
    const bool ExecutionReady = Execution && Definition->initialize_execution_storage(Execution);
    bool Valid = false;
    if (PreparedReady && ExecutionReady)
    {
        HTNGeneratedPlannerContext Context{};
        Context.world_state = &World;
        Context.callterm_binding_context = &Bindings;
        Context.callterm_error_policy = HTNCallTermErrorPolicy::FailSilently;
        Context.backtracking_mode = HTN_BACKTRACKING_ALL;
        Context.prepared_storage = Prepared;
        Context.execution_storage = Execution;
        Context.list_allocator = &Allocator;
#ifdef HTN_DEBUG_DECOMPOSITION
        HTNGeneratedDebugger Debugger;
        Debugger.SetEnabled(true);
        Context.debugger = &Debugger;
#endif
        HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("run")));
        HTNAtomOwner Plan;
        Valid = Definition->decompose_call(&Context, Call.Get(), 1, Plan.Get()) == HTN_DECOMPOSITION_SUCCEEDED &&
            HTNAtom_GetListSize(Plan.Get()) == 3 && Calls == 1 &&
            Plan.Get()->value.list_value.allocator == &Allocator && Allocator.GetFallbackNodeCount() > 0;
        if (Valid)
        {
            const auto* Values = HTNAtom_GetListElement(Plan.Get(), 1);
            const auto* Integer = HTNAtom_GetListElement(Values, 11);
            const auto* Float = HTNAtom_GetListElement(Values, 12);
            Valid = Integer && Integer->type == HTN_ATOM_TYPE_INT && Integer->value.int_value == 3 &&
                Float && Float->type == HTN_ATOM_TYPE_FLOAT && Float->value.float_value == 3.5f;
            const auto* Negative = HTNAtom_GetListElement(Values, 2);
            const auto* Boolean = HTNAtom_GetListElement(Values, 4);
            const auto* Symbol = HTNAtom_GetListElement(Values, 6);
            const auto* Text = HTNAtom_GetListElement(Values, 7);
            const auto* Empty = HTNAtom_GetListElement(Values, 8);
            const auto* List = HTNAtom_GetListElement(Values, 9);
            const auto* Tail = HTNAtom_GetListElement(Values, 10);
            const auto* Deferred = HTNAtom_GetListElement(Plan.Get(), 2);
            const auto* DeferredArgument = HTNAtom_GetListElement(Deferred, 1);
            Valid = Valid && Negative && Negative->type == HTN_ATOM_TYPE_INT && Negative->value.int_value == -3 &&
                Boolean && Boolean->type == HTN_ATOM_TYPE_BOOL && Boolean->value.bool_value &&
                Symbol && Symbol->type == HTN_ATOM_TYPE_SYMBOL && Symbol->value.symbol_value == HtnSymbol::sGetSymbol("marker") &&
                Text && Text->type == HTN_ATOM_TYPE_STRING && HTNAtom_GetStringSize(Text) > HTN_ATOM_STRING_INLINE_CAPACITY &&
                Empty && Empty->type == HTN_ATOM_TYPE_LIST && HTNAtom_GetListSize(Empty) == 0 &&
                List && HTNAtom_GetListSize(List) == 4 && Tail && HTNAtom_GetListSize(Tail) == 3 &&
                DeferredArgument && DeferredArgument->type == HTN_ATOM_TYPE_INT && DeferredArgument->value.int_value == 3;
        }
#ifdef HTN_DEBUG_DECOMPOSITION
        Valid = Valid && !Debugger.GetNodes().empty() && Debugger.GetNodes().front().Completed;
#endif
        HTNAtomOwner Later(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("later"), 3));
        HTNAtomOwner LaterPlan;
        Valid = Valid && Definition->decompose_call(&Context, Later.Get(), 0, LaterPlan.Get()) == HTN_DECOMPOSITION_SUCCEEDED &&
            HTNAtom_GetListSize(LaterPlan.Get()) == 1;
    }
    if (ExecutionReady) Definition->destroy_execution_storage(Execution);
    if (PreparedReady) Definition->destroy_prepared_storage(Prepared);
    ::operator delete(Execution);
    ::operator delete(Prepared);
    return Valid && Allocator.GetPooledNodeCount() == 0 && Allocator.GetFallbackNodeCount() == 0;
}

int main(int argc, char** argv)
{
#ifdef HTN_EXPECTED_BRIDGE_ABI
    if (HTN_RUNTIME_BRIDGE_ABI_VERSION != HTN_EXPECTED_BRIDGE_ABI ||
        HTN_GENERATED_PLANNER_ABI_VERSION != HTN_EXPECTED_PLANNER_ABI) return 18;
#endif
    const bool ExpectIncompatible = argc == 4 && std::strcmp(argv[3], "--expect-incompatible") == 0;
    if (argc != 3 && !ExpectIncompatible) return 10;
    ModuleHandle bridge = OpenModule(argv[2]);
    if (!bridge) return 11;
    auto bind = reinterpret_cast<HTNRuntimeBridgeBindFn>(FindSymbol(bridge, "HTNRuntimeBridge_Bind"));
    if (!bind) { CloseModule(bridge); return 12; }
#define CHECK_EXPORT(result, name, parameters, arguments) \
    if (!FindSymbol(bridge, #name)) { std::fprintf(stderr, "Missing bridge export: %s\n", #name); CloseModule(bridge); return 16; }
    HTN_RUNTIME_BRIDGE_FUNCTIONS(CHECK_EXPORT)
#undef CHECK_EXPORT
    HTNHostRuntimeAPI api = HTNCreateHostRuntimeAPI();
    HTNHostRuntimeAPI invalid = api;
    invalid.abi_version -= 1u;
    if (bind(&invalid) || !bind(&api)) { CloseModule(bridge); return 13; }
    ModuleHandle domain = OpenModule(argv[1]);
    if (!domain) { CloseModule(bridge); return 14; }
    LoadedDefinition = reinterpret_cast<GetDefinitionFn>(FindSymbol(domain, "CreatePackageCoreConsumerHTN_GetDefinition"));
    int result = !LoadedDefinition ? 15 : ExpectIncompatible
        ? (HTNGeneratedPlanner_ValidateDefinition(LoadedDefinition()) ? 21 : 0)
        : RunCoreConsumer();
    auto Probe = reinterpret_cast<int(*)()>(FindSymbol(domain, "HTNBridgeCoverageProbe"));
    auto Coverage = reinterpret_cast<GetDefinitionFn>(FindSymbol(domain, "CreateBridgeCoverageHTN_GetDefinition"));
    if (!result && !ExpectIncompatible && (!Probe || !Probe() || !Coverage || !RunCoverage(Coverage))) result = 17;
    LoadedDefinition = nullptr;
    if (!CloseModule(domain)) result = 19;
    if (!CloseModule(bridge)) result = 20;
    if (result == 0) std::puts(ExpectIncompatible ? "PASS: incompatible domain ABI rejected before execution"
        : "Dynamic domain consumer: PASS (bridge rejects incompatible ABI)");
    return result;
}
