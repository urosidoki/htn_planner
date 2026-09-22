// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com
#include "HTNPlanner.h"
#include "Core/HTNTask.h"
#include <cstdio>
#include <new>
#include <string_view>
#include <vector>

extern "C" const HTNGeneratedPlannerDefinition* CreateLinuxSmokeHTN_GetDefinition(void);

int main()
{
    const auto* Definition = CreateLinuxSmokeHTN_GetDefinition();
    if (!HTNGeneratedPlanner_ValidateDefinition(Definition)) return 1;
    HTNWorldState WorldState;
    WorldState.AddFact("candidate", std::vector<HTNAtomOwner>{HTNAtomOwner(int32{1})});
    WorldState.AddFact("candidate", std::vector<HTNAtomOwner>{HTNAtomOwner(int32{2})});
    HTNCallTermRegistry Registry;
    HTNCallTermBindingContext Bindings(Registry);
    void* Prepared = ::operator new(Definition->prepared_storage_size, std::nothrow);
    void* Execution = ::operator new(Definition->execution_storage_size, std::nothrow);
    const bool PreparedReady = Prepared && Definition->initialize_prepared_storage(Prepared);
    const bool ExecutionReady = Execution && Definition->initialize_execution_storage(Execution);
    int Result = 2;
    if (PreparedReady && ExecutionReady)
    {
        HTNGeneratedPlannerContext Context{};
        Context.world_state = &WorldState;
        Context.callterm_binding_context = &Bindings;
        Context.missing_callterm_policy = HTNMissingCallTermPolicy::FailSilently;
        Context.backtracking_mode = HTN_BACKTRACKING_ALL;
        Context.prepared_storage = Prepared;
        Context.execution_storage = Execution;
        Result = 0;
        for (const char* Entry : {"run", "no_plan", "run"})
        {
            HTNAtom Call = HTNAtom::sCreateCall(HtnSymbol::sGetSymbol(Entry));
            HTNAtom Plan;
            const auto Status = Definition->decompose_call(&Context, &Call, 1, &Plan);
            const bool ExpectPlan = std::string_view(Entry) == "run";
            bool Valid = ExpectPlan
                ? Status == HTN_DECOMPOSITION_SUCCEEDED && HTNAtom_GetListSize(&Plan) == 1
                : Status == HTN_DECOMPOSITION_NO_PLAN;
            if (Valid && ExpectPlan)
            {
                const HTNAtom* Step = HTNAtom_GetListElement(&Plan, 0u);
                const auto* Head = HTNGetCallHead(Step);
                Valid = Head && Head->GetString() == "!selected" && HTNGetCallArgumentCount(Step) == 1u;
                if (Valid)
                {
                    const HTNAtom* Argument = HTNFindCallArgument(Step, 0u);
                    Valid = Argument && HTNAtomIsType<int32>(*Argument) && HTNAtomGetValue<int32>(*Argument) == 2;
                }
            }
            if (!Valid)
            {
                std::fprintf(stderr, "Entry %s: unexpected status %d or plan (size %d)\n",
                    Entry, static_cast<int>(Status), HTNAtom_GetListSize(&Plan));
                Result = 3;
            }
            HTNAtom::sDestroy(Plan);
            HTNAtom::sDestroy(Call);
        }
    }
    if (ExecutionReady) Definition->destroy_execution_storage(Execution);
    if (PreparedReady) Definition->destroy_prepared_storage(Prepared);
    ::operator delete(Execution);
    ::operator delete(Prepared);
    if (Result == 0) std::puts("PASS: generated C arithmetic, axiom backtracking, failure and planner reuse");
    return Result;
}
