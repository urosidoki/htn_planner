// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNRuntimeBridge.h"

namespace
{
HTNHostRuntimeAPI Runtime{};
bool RuntimeBound = false;
}

extern "C" HTN_RUNTIME_BRIDGE_EXPORT int HTNRuntimeBridge_Bind(const HTNHostRuntimeAPI* inAPI)
{
    if (!inAPI || inAPI->abi_version != HTN_RUNTIME_BRIDGE_ABI_VERSION || inAPI->size != sizeof(Runtime))
        return 0;

    if (RuntimeBound)
    {
#define HTN_RUNTIME_BRIDGE_COMPARE(return_type, name, parameters, arguments) if (Runtime.name != inAPI->name) return 0;
        HTN_RUNTIME_BRIDGE_FUNCTIONS(HTN_RUNTIME_BRIDGE_COMPARE)
#undef HTN_RUNTIME_BRIDGE_COMPARE
        return 1;
    }

#define HTN_RUNTIME_BRIDGE_VALIDATE(return_type, name, parameters, arguments) if (!inAPI->name) return 0;
    HTN_RUNTIME_BRIDGE_FUNCTIONS(HTN_RUNTIME_BRIDGE_VALIDATE)
#undef HTN_RUNTIME_BRIDGE_VALIDATE

    Runtime = *inAPI;
    RuntimeBound = true;
    return 1;
}

#ifdef _MSC_VER
#define HTN_RUNTIME_BRIDGE_EXPORT_SYMBOL(return_type, name, parameters, arguments) \
    __pragma(comment(linker, "/export:" #name))
HTN_RUNTIME_BRIDGE_FUNCTIONS(HTN_RUNTIME_BRIDGE_EXPORT_SYMBOL)
#undef HTN_RUNTIME_BRIDGE_EXPORT_SYMBOL
#define HTN_RUNTIME_BRIDGE_FORWARD_EXPORT
#else
#define HTN_RUNTIME_BRIDGE_FORWARD_EXPORT HTN_RUNTIME_BRIDGE_EXPORT
#endif

#define HTN_RUNTIME_BRIDGE_FORWARD(return_type, name, parameters, arguments) \
    extern "C" HTN_RUNTIME_BRIDGE_FORWARD_EXPORT return_type name parameters { return Runtime.name arguments; }
HTN_RUNTIME_BRIDGE_FUNCTIONS(HTN_RUNTIME_BRIDGE_FORWARD)
#undef HTN_RUNTIME_BRIDGE_FORWARD
#undef HTN_RUNTIME_BRIDGE_FORWARD_EXPORT
