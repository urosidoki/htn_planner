// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#ifdef __cplusplus
class HtnSymbol;
extern "C" {
#else
typedef struct HtnSymbol HtnSymbol;
#endif

/* C bridge used by generated planners to resolve compile-time symbol text once. */
const HtnSymbol* HtnSymbol_InternGenerated(const char* text);

#ifdef __cplusplus
}
#endif
