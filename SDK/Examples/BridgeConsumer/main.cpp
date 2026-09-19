// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNPlanner.h"
#include "Translator/HTNRuntimeBridge.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>

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

int main(int argc, char** argv)
{
    if (argc != 3) return 10;
    HMODULE bridge = LoadLibraryA(argv[2]);
    if (!bridge) return 11;
    auto bind = reinterpret_cast<HTNRuntimeBridgeBindFn>(GetProcAddress(bridge, "HTNRuntimeBridge_Bind"));
    if (!bind) { FreeLibrary(bridge); return 12; }
    HTNHostRuntimeAPI api = HTNCreateHostRuntimeAPI();
    HTNHostRuntimeAPI invalid = api;
    invalid.abi_version ^= 1;
    if (bind(&invalid) || !bind(&api)) { FreeLibrary(bridge); return 13; }
    HMODULE domain = LoadLibraryA(argv[1]);
    if (!domain) { FreeLibrary(bridge); return 14; }
    LoadedDefinition = reinterpret_cast<GetDefinitionFn>(GetProcAddress(domain, "CreatePackageCoreConsumerHTN_GetDefinition"));
    int result = LoadedDefinition ? RunCoreConsumer() : 15;
    FreeLibrary(domain);
    FreeLibrary(bridge);
    if (result == 0) std::puts("Domain DLL consumer: PASS (bridge rejects incompatible ABI)");
    return result;
}
