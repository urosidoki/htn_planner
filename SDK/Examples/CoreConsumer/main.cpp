// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "HTNPlanner.h"

#include <cstdio>
#include <new>

extern "C" const HTNGeneratedPlannerDefinition* CreatePackageCoreConsumerHTN_GetDefinition(void);

int main()
{
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
