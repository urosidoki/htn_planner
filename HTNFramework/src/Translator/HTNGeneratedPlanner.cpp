// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNGeneratedPlanner.h"
#include "HTNCoreMinimal.h"

extern "C" int HTNGeneratedPlanner_ValidateDefinition(const HTNGeneratedPlannerDefinition* inDefinition)
{
    if (!inDefinition)
        return 0;

    // Check the first word before accessing configuration-dependent fields.
    if (inDefinition->abi_version != HTN_GENERATED_PLANNER_ABI_VERSION)
    {
        HTN_LOG_ERROR("Generated planner ABI mismatch: runtime [{}], domain [{}]",
            HTN_GENERATED_PLANNER_ABI_VERSION, inDefinition->abi_version);
        return 0;
    }

    if (inDefinition->prepared_storage_size == 0u ||
        !inDefinition->initialize_prepared_storage ||
        !inDefinition->destroy_prepared_storage ||
        inDefinition->execution_storage_size == 0u ||
        !inDefinition->initialize_execution_storage ||
        !inDefinition->destroy_execution_storage ||
        !inDefinition->decompose_call ||
        (inDefinition->fact_count != 0u && !inDefinition->fact_names))
    {
        HTN_LOG_ERROR("Generated planner definition has an invalid storage/lifecycle contract");
        return 0;
    }
    for (uint32_t Index = 0u; Index < inDefinition->fact_count; ++Index)
    {
        if (!inDefinition->fact_names[Index])
            return 0;
    }
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    if (!inDefinition->get_execution_profiling)
    {
        HTN_LOG_ERROR("Generated planner definition has no profiling accessor");
        return 0;
    }
#endif
    return 1;
}
