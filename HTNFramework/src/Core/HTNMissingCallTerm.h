// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <stdint.h>

#ifdef __cplusplus
enum class HTNMissingCallTermPolicy : uint32_t
{
    Unset,
    FailSilently,
    Report
};
enum class HTNMissingCallTermReason : uint32_t
{
    NotRegistered,
    MissingBinding,
    MissingInstance
};
#else
typedef uint32_t HTNMissingCallTermPolicy;
#define HTN_MISSING_CALLTERM_UNSET UINT32_C(0)
#define HTN_MISSING_CALLTERM_FAIL_SILENTLY UINT32_C(1)
#define HTN_MISSING_CALLTERM_REPORT UINT32_C(2)
typedef uint32_t HTNMissingCallTermReason;
#define HTN_MISSING_CALLTERM_NOT_REGISTERED UINT32_C(0)
#define HTN_MISSING_CALLTERM_MISSING_BINDING UINT32_C(1)
#define HTN_MISSING_CALLTERM_MISSING_INSTANCE UINT32_C(2)
#endif

/* Borrowed invocation provenance, independent of debug instrumentation. */
typedef struct HTNCallTermSource
{
    const char* domain;
    const char* file;
    uint32_t line;
    uint32_t column;
} HTNCallTermSource;

/* Borrowed data valid only during the callback; source strings may be null. */
typedef struct HTNMissingCallTermInfo
{
    const char* Name;
    HTNMissingCallTermReason Reason;
    const char* DaemonID;
    HTNCallTermSource Source;
} HTNMissingCallTermInfo;

/* The client owns the context. If this returns, invocation fails normally. */
typedef void (*HTNMissingCallTermCallback)(void* client_context, const HTNMissingCallTermInfo* info);
