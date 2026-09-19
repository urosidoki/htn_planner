// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNPlanner.h"
#include <cstdio>
#include <new>

extern "C" const HTNGeneratedPlannerDefinition* CreateCoreConsumerHTN_GetDefinition(void);

// Standalone consumer: compile with ONLY the Framework include root and library.
// No hooks, planning units, daemon base, Integration headers or DLL management.
int main()
{
    const auto* Definition = CreateCoreConsumerHTN_GetDefinition();
    if (!HTNGeneratedPlanner_ValidateDefinition(Definition))
        return 1;
    auto Invalid = *Definition;
    Invalid.abi_version = 0u;
    if (HTNGeneratedPlanner_ValidateDefinition(nullptr) ||
        HTNGeneratedPlanner_ValidateDefinition(&Invalid)) return 1;
    Invalid = *Definition;
    Invalid.decompose_call = nullptr;
    if (HTNGeneratedPlanner_ValidateDefinition(&Invalid)) return 1;
    HTNCallTermRegistry Registry;
    HTNWorldState Worlds[2];
    HTNCallTermBindingContext Bindings[2] = {
        HTNCallTermBindingContext(Registry), HTNCallTermBindingContext(Registry)};
    void* Prepared[2]{};
    void* Execution[2]{};
    bool PreparedInitialized[2]{};
    bool ExecutionInitialized[2]{};
    const auto Finish = [&](int Code)
    {
        for (int Index = 0; Index < 2; ++Index)
        {
            if (ExecutionInitialized[Index]) Definition->destroy_execution_storage(Execution[Index]);
            if (PreparedInitialized[Index]) Definition->destroy_prepared_storage(Prepared[Index]);
            ::operator delete(Execution[Index]);
            ::operator delete(Prepared[Index]);
        }
        return Code;
    };
    for (int Index = 0; Index < 2; ++Index)
    {
        Prepared[Index] = ::operator new(Definition->prepared_storage_size, std::nothrow);
        Execution[Index] = ::operator new(Definition->execution_storage_size, std::nothrow);
        if (!Prepared[Index] || !Execution[Index]) return Finish(2);
        PreparedInitialized[Index] = Definition->initialize_prepared_storage(Prepared[Index]) != 0;
        ExecutionInitialized[Index] = Definition->initialize_execution_storage(Execution[Index]) != 0;
        if (!PreparedInitialized[Index] || !ExecutionInitialized[Index]) return Finish(3);
    }
    for (int Iteration = 0; Iteration < 64; ++Iteration)
    {
        for (int Index = 0; Index < 2; ++Index)
        {
            HTNGeneratedPlannerContext Context{};
            Context.world_state = &Worlds[Index];
            Context.callterm_binding_context = &Bindings[Index];
            Context.backtracking_mode = HTN_BACKTRACKING_ALL;
            Context.prepared_storage = Prepared[Index];
            Context.execution_storage = Execution[Index];
            const bool Small = ((Iteration + Index) % 2) != 0;
            HTNAtom Call = HTNAtom::sCreateCall(HtnSymbol::sGetSymbol(Small ? "run_small" : "run"));
            HTNAtom Candidate;
            const auto Status = Definition->decompose_call(&Context, &Call, 1, &Candidate);
            // The CLIENT decides whether to adopt this candidate or keep its active plan.
            const bool Valid = Status == HTN_DECOMPOSITION_SUCCEEDED &&
                HTNAtom_GetListSize(&Candidate) == (Small ? 2 : 3);
            HTNAtom::sDestroy(Candidate);
            HTNAtom::sDestroy(Call);
            if (!Valid) return Finish(4);
        }
    }
    std::puts("Core-only consumer: PASS (two entities, 128 plans, no HTNIntegration)");
    return Finish(0);
}
