// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "HTNIntegration.h"

#include <cstdio>

extern "C" const HTNGeneratedPlannerDefinition* CreatePackageIntegrationConsumerHTN_GetDefinition(void);

int main()
{
    HTNDatabaseHook DatabaseHook;
    HTNCallTermRegistry Registry;
    HTNPlannerHook PlannerHook(DatabaseHook.GetWorldState(), Registry);

    if (!PlannerHook.SetGeneratedPlannerDefinition(CreatePackageIntegrationConsumerHTN_GetDefinition()))
        return 1;

    HTNPlanningUnit PlanningUnit(DatabaseHook, PlannerHook, HtnSymbol::sGetSymbol("run"));
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNGeneratedDebugger Debugger;
    Debugger.SetEnabled(true);
    PlanningUnit.SetGeneratedDebugger(&Debugger);
#endif
    if (PlanningUnit.DecomposeTopLevelMethod() != HTN_DECOMPOSITION_SUCCEEDED)
        return 2;
#ifdef HTN_DEBUG_DECOMPOSITION
    if (Debugger.GetNodes().empty() || !Debugger.GetNodes().front().Completed)
        return 5;
#endif

    int PrimitiveCount = 0;
    for (;;)
    {
        const HTNPrimitiveTaskResolution Resolution = PlanningUnit.ResolveCurrentPrimitiveTask();
        if (Resolution == HTNPrimitiveTaskResolution::PlanCompleted)
            break;
        if (Resolution != HTNPrimitiveTaskResolution::TaskReady || !PlanningUnit.GetCurrentPrimitiveTask())
            return 3;

        ++PrimitiveCount;
        PlanningUnit.CompleteCurrentPrimitiveTask();
    }

    if (PrimitiveCount != 3)
        return 4;

#ifdef HTN_DEBUG_DECOMPOSITION
    std::puts("Optional-integration external package consumer: PASS (debug decomposition captured)");
#else
    std::puts("Optional-integration external package consumer: PASS (release)");
#endif
    return 0;
}
