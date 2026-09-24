// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.
#include "Translator/HTNRuntimeBridge.h"

// Force real import-library references to every entry of the canonical table.
// Volatile pointers keep optimized builds from eliminating the link probe.
extern "C" __declspec(dllexport) int HTNBridgeCoverageProbe()
{
#define CHECK_ENTRY(result, name, parameters, arguments) \
    { auto volatile Pointer = &name; if (!Pointer) return 0; }
    HTN_RUNTIME_BRIDGE_FUNCTIONS(CHECK_ENTRY)
#undef CHECK_ENTRY
    return 1;
}
