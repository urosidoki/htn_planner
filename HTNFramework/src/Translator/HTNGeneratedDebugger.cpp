// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNGeneratedDebugger.h"
#include "Translator/HTNGeneratedPlanner.h"

#ifdef HTN_DEBUG_DECOMPOSITION

namespace
{
void GetGeneratedEventDebugVariables(const HTNGeneratedVariableStorage* inVariables,
                                     const HTNAtom*& outValues,
                                     const std::uint64_t*& outBoundMask,
                                     std::uint32_t& outSlotCount)
{
    outValues = nullptr;
    outBoundMask = nullptr;
    outSlotCount = 0u;
    if (!inVariables)
        return;

    outValues = inVariables->values;
    outBoundMask = inVariables->bound_mask;
    outSlotCount = inVariables->value_count;
}
}

extern "C" void HTNGeneratedEventDebug_BeginPlan(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->BeginPlan(domain, index, v, m, c); }
extern "C" void HTNGeneratedEventDebug_EndPlan(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->EndPlan(domain, v, m, c, result != 0); }
extern "C" void HTNGeneratedEventDebug_BeginMethod(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->BeginMethod(domain, index, v, m, c); }
extern "C" void HTNGeneratedEventDebug_EndMethod(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->EndMethod(domain, v, m, c, result != 0); }
extern "C" void HTNGeneratedEventDebug_BeginBranch(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->BeginBranch(domain, index, v, m, c); }
extern "C" void HTNGeneratedEventDebug_EndBranch(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->EndBranch(domain, v, m, c, result != 0); }
extern "C" void HTNGeneratedEventDebug_CapturePendingTask(HTNGeneratedDebugger* d, uint32_t index) { if (d) d->CapturePendingTask(index); }
extern "C" void HTNGeneratedEventDebug_BeginTask(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->BeginTask(domain, index, v, m, c); }
extern "C" void HTNGeneratedEventDebug_EndTask(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->EndTask(domain, v, m, c, result != 0); }
extern "C" void HTNGeneratedEventDebug_BeginCondition(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->BeginCondition(domain, index, v, m, c); }
extern "C" void HTNGeneratedEventDebug_EndCondition(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->EndCondition(domain, v, m, c, result != 0); }
extern "C" void HTNGeneratedEventDebug_BeginAxiom(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, uint32_t index) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->BeginAxiom(domain, index, v, m, c); }
extern "C" void HTNGeneratedEventDebug_EndAxiom(HTNGeneratedDebugger* d, const HTNGeneratedVariableStorage* variables, const HTNGeneratedPlannerDefinition* domain, int result) { const HTNAtom* v; const std::uint64_t* m; std::uint32_t c; GetGeneratedEventDebugVariables(variables, v, m, c); if (d) d->EndAxiom(domain, v, m, c, result != 0); }

#endif
