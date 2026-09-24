// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNGeneratedPlanner.h"

// This test module reuses valid generated lifecycle callbacks but exports a
// malformed name table. It must be rejected before any storage is initialized.
#undef CreateWandererHotReloadHTN_GetDefinition
extern "C" const HTNGeneratedPlannerDefinition* GetValidWandererDefinition(void);

#ifdef _WIN32
#define HTN_TEST_EXPORT __declspec(dllexport)
#else
#define HTN_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" HTN_TEST_EXPORT const HTNGeneratedPlannerDefinition* CreateWandererHotReloadHTN_GetDefinition(void)
{
    static const char* Names[] = {nullptr};
    static const HTNGeneratedPlannerDefinition Definition = [] {
        auto Result = *GetValidWandererDefinition();
        Result.fact_count = 1;
        Result.fact_names = Names;
        return Result;
    }();
    return &Definition;
}
