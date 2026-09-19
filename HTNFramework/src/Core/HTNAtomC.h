// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

/* Detailed diagnostics are a strict superset of the basic owned-resource diagnostics.
 * Keep this implication in the public C-compatible atom header so every translation
 * unit observes the same configuration. Build modes never enable either macro. */
#if defined(HTN_MEMORY_ATOM_DIAGNOSTICS_DETAILED) && !defined(HTN_MEMORY_ATOM_DIAGNOSTICS)
#define HTN_MEMORY_ATOM_DIAGNOSTICS
#endif

#include <stddef.h>
#include <stdint.h>

typedef enum HTNAtomType
{
    HTN_ATOM_TYPE_UNBOUND = 0,
    HTN_ATOM_TYPE_BOOL,
    HTN_ATOM_TYPE_INT,
    HTN_ATOM_TYPE_FLOAT,
    HTN_ATOM_TYPE_STRING,
    HTN_ATOM_TYPE_SYMBOL,
    HTN_ATOM_TYPE_LIST
} HTNAtomType;

#define HTN_ATOM_STRING_INLINE_CAPACITY 15u
/* Generated immutable atoms may borrow a C string literal. The sentinel marks
 * string_value.data as non-owning; ordinary mutable strings never use it. */
#define HTN_ATOM_STRING_STATIC_CAPACITY UINT32_MAX

typedef struct HTNAtomStringStorage
{
    char*    data;
    uint32_t size;
    uint32_t capacity;
    char     inline_data[HTN_ATOM_STRING_INLINE_CAPACITY + 1u];
} HTNAtomStringStorage;

#ifdef __cplusplus
class HtnSymbol;
#endif

typedef struct HTNAtom HTNAtom;
typedef struct HTNAtomNode HTNAtomNode;

typedef enum HTNAtomListSplitDirection
{
    // Extract the first element and return every following element as the remainder.
    HTN_ATOM_LIST_SPLIT_FRONT = 0,

    // Extract the last element and return every preceding element as the remainder.
    HTN_ATOM_LIST_SPLIT_BACK
} HTNAtomListSplitDirection;

typedef struct HTNAtomList
{
    void*        allocator;
    HTNAtomNode* head_node;
    HTNAtomNode* tail_node;
    uint32_t     size;

} HTNAtomList;

typedef union HTNAtomValue
{
    uint8_t              bool_value;
    int32_t              int_value;
    float                float_value;
    HTNAtomStringStorage string_value;
    const void*          symbol_value;
    HTNAtomList          list_value;
} HTNAtomValue;

struct HTNAtom
{
    HTNAtomType  type;
    HTNAtomValue value;

#ifdef __cplusplus
    /* C++ convenience API. Static functions do not alter the C-compatible layout.
       sCreateCall returns an owning atom; the caller must pair it with sDestroy. */
    template<typename... TArguments>
    static HTNAtom sCreateCall(const HtnSymbol* inHead, TArguments&&... inArguments);
    static void sDestroy(HTNAtom& ioAtom);
#endif
};

typedef struct HTNAtomNode
{
    HTNAtom      data;
    HTNAtomNode* next_node;
    void*        allocation_cookie;
} HTNAtomNode;

/* C ABI lifecycle helpers. These make the representation usable from C without
 * requiring C code to know about C++ constructors/destructors. */
#ifdef __cplusplus
extern "C" {
#endif

/* Shared HTNAtomList API. The representation and operational API are identical in C and C++. */
void HTNAtomList_Init(HTNAtomList* ioList);
/* Selects the allocation policy used for nodes subsequently created by this list.
 * A non-null allocator is borrowed and must outlive every list that references it. */
void HTNAtomList_InitWithAllocator(HTNAtomList* ioList, void* inAllocator);
void HTNAtomList_Destroy(HTNAtomList* ioList);
/* Copies values into nodes allocated by the destination allocator. The destination
 * therefore keeps its allocation policy; the source remains unchanged. */
int  HTNAtomList_Copy(HTNAtomList* outList, const HTNAtomList* inList);
/* Transfers the source nodes to the destination in O(1). The source allocator must
 * travel with those nodes so they are later deallocated by the allocator that created
 * them. The destination's previous contents are destroyed and the source is left empty. */
void HTNAtomList_Move(HTNAtomList* outList, HTNAtomList* inList);
int  HTNAtomList_Equals(const HTNAtomList* inLeft, const HTNAtomList* inRight);
int  HTNAtomList_PushBack(HTNAtomList* ioList, const HTNAtom* inValue);
int  HTNAtomList_PushBackMove(HTNAtomList* ioList, HTNAtom* ioValue);
int  HTNAtomList_PopFrontMove(HTNAtomList* ioList, HTNAtom* outValue);
int  HTNAtomList_RemoveAt(HTNAtomList* ioList, uint32_t inIndex);
void HTNAtomList_Clear(HTNAtomList* ioList);
const HTNAtom* HTNAtomList_Get(const HTNAtomList* inList, uint32_t inIndex);
uint32_t HTNAtomList_GetSize(const HTNAtomList* inList);
int HTNAtomList_IsEmpty(const HTNAtomList* inList);
int HTNAtomList_Split(const HTNAtomList* inList, HTNAtomListSplitDirection inDirection,
                      HTNAtom* outElement, HTNAtom* outRemainder);
void HTNAtom_Init(HTNAtom* ioAtom);
/* Creates an owning call atom: (head arg0 ... argN). outAtom must not contain a
 * live atom on entry. Arguments are deep-copied and remain caller-owned. On
 * failure outAtom remains a valid, destroyable unbound atom. */
int  HTNAtom_CreateCall(HTNAtom* outAtom, const void* inHeadSymbol, const HTNAtom* inArguments, uint32_t inArgumentCount);
/* Same ownership semantics as HTNAtom_CreateCall, but accepts borrowed argument
 * pointers so generated code can append values directly from variable/prepared storage. */
int  HTNAtom_CreateCallFromPointers(HTNAtom* outAtom, const void* inHeadSymbol, const HTNAtom* const* inArguments, uint32_t inArgumentCount);
void HTNAtom_SetEmptyList(HTNAtom* ioAtom);
void HTNAtom_Destroy(HTNAtom* ioAtom);
/* Construct-style copy/move. outAtom must not contain a live atom on entry.
 * Copy failure leaves outAtom initialized, unbound and destroyable. */
int  HTNAtom_Copy(HTNAtom* outAtom, const HTNAtom* inAtom);
void HTNAtom_Move(HTNAtom* outAtom, HTNAtom* inAtom);
int  HTNAtom_AssignCopy(HTNAtom* ioAtom, const HTNAtom* inAtom);
void HTNAtom_AssignMove(HTNAtom* ioAtom, HTNAtom* inAtom);
void HTNAtom_InitRange(HTNAtom* ioAtoms, uint32_t inCount);
void HTNAtom_DestroyRange(HTNAtom* ioAtoms, uint32_t inCount);
/* outAtoms must refer to raw/non-live storage. On failure every output atom
 * initialized by this call is destroyed before returning. */
int  HTNAtom_CopyRange(HTNAtom* outAtoms, const HTNAtom* inAtoms, uint32_t inCount);
void HTNAtom_MoveRange(HTNAtom* outAtoms, HTNAtom* inAtoms, uint32_t inCount);
void HTNAtom_SetBool(HTNAtom* ioAtom, uint8_t inValue);
void HTNAtom_SetInt(HTNAtom* ioAtom, int32_t inValue);
void HTNAtom_SetFloat(HTNAtom* ioAtom, float inValue);
void HTNAtom_SetSymbol(HTNAtom* ioAtom, const void* inSymbol);
int  HTNAtom_SetString(HTNAtom* ioAtom, const char* inData, uint32_t inSize);
int  HTNAtom_SetListCopy(HTNAtom* ioAtom, const HTNAtomList* inList);
void HTNAtom_SetListMove(HTNAtom* ioAtom, HTNAtomList* inList);
void HTNAtom_Unbind(HTNAtom* ioAtom);
int  HTNAtom_Equals(const HTNAtom* inLeft, const HTNAtom* inRight);
HTNAtomType HTNAtom_GetType(const HTNAtom* inAtom);
int  HTNAtom_IsBound(const HTNAtom* inAtom);
const char* HTNAtom_GetStringData(const HTNAtom* inAtom);
uint32_t HTNAtom_GetStringSize(const HTNAtom* inAtom);
int  HTNAtom_PushBackListElement(HTNAtom* ioAtom, const HTNAtom* inValue);
int  HTNAtom_PushBackListElementMove(HTNAtom* ioAtom, HTNAtom* ioValue);
const HTNAtom* HTNAtom_GetListElement(const HTNAtom* inAtom, uint32_t inIndex);
int32_t HTNAtom_GetListSize(const HTNAtom* inAtom);
int  HTNAtom_IsListEmpty(const HTNAtom* inAtom);

/* Owned-resource diagnostics are explicit opt-in and have zero tracking cost unless
 * enabled by preprocessor definition. HTNAtom values are relocatable C values, so
 * their addresses are intentionally not treated as lifetime identities. */
typedef struct HTNAtomDebugStats
{
    uint64_t live_heap_strings;
    uint64_t live_heap_string_bytes;
    uint64_t live_list_nodes;
    uint64_t peak_heap_string_bytes;
    uint64_t peak_live_list_nodes;
    uint64_t heap_string_allocations;
    uint64_t heap_string_frees;
    uint64_t list_node_allocations;
    uint64_t list_node_deallocations;
} HTNAtomDebugStats;

HTNAtomDebugStats HTNAtomDebug_GetStats(void);
int HTNAtomDebug_HasLeaks(void);
void HTNAtomDebug_ReportLeaks(void);
void HTNAtomDebug_ResetPeaks(void);

#ifdef __cplusplus
}
#endif
