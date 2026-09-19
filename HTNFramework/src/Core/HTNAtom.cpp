// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNAtom.h"
#include "Core/HTNAtomList.h"
#include "Core/HTNAtomListAllocator.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <format>
#include <iomanip>
#include <ios>
#include <new>
#include <sstream>
#include <utility>

namespace
{
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
std::atomic<uint64_t> GHTNLiveHeapStrings{0};
std::atomic<uint64_t> GHTNLiveHeapStringBytes{0};
std::atomic<uint64_t> GHTNPeakHeapStringBytes{0};
std::atomic<uint64_t> GHTNHeapStringAllocations{0};
std::atomic<uint64_t> GHTNHeapStringFrees{0};

void UpdatePeak(std::atomic<uint64_t>& ioPeak, const uint64_t inValue)
{
    uint64_t Peak = ioPeak.load(std::memory_order_relaxed);
    while (Peak < inValue && !ioPeak.compare_exchange_weak(Peak, inValue, std::memory_order_relaxed)) {}
}

void TrackHeapStringAllocated(const uint32_t inCapacity)
{
    GHTNHeapStringAllocations.fetch_add(1u, std::memory_order_relaxed);
    GHTNLiveHeapStrings.fetch_add(1u, std::memory_order_relaxed);
    const uint64_t Bytes = GHTNLiveHeapStringBytes.fetch_add(static_cast<uint64_t>(inCapacity) + 1u, std::memory_order_relaxed) + static_cast<uint64_t>(inCapacity) + 1u;
    UpdatePeak(GHTNPeakHeapStringBytes, Bytes);
}

void TrackHeapStringFreed(const uint32_t inCapacity)
{
    GHTNHeapStringFrees.fetch_add(1u, std::memory_order_relaxed);
    GHTNLiveHeapStrings.fetch_sub(1u, std::memory_order_relaxed);
    GHTNLiveHeapStringBytes.fetch_sub(static_cast<uint64_t>(inCapacity) + 1u, std::memory_order_relaxed);
}

#endif

char* AllocateStringBuffer(const uint32_t inCapacity)
{
    char* Data = static_cast<char*>(::operator new(static_cast<size_t>(inCapacity) + 1u, std::nothrow));
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    if (Data) TrackHeapStringAllocated(inCapacity);
#endif
    return Data;
}

void FreeStringBuffer(char* inData, const uint32_t inCapacity)
{
    if (!inData) return;
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    TrackHeapStringFreed(inCapacity);
#else
    (void)inCapacity;
#endif
    ::operator delete(inData);
}

void InitializeStringStorage(HTNAtomStringStorage& outString)
{
    outString.data = outString.inline_data;
    outString.size = 0u;
    outString.capacity = HTN_ATOM_STRING_INLINE_CAPACITY;
    outString.inline_data[0] = '\0';
}

void DestroyAtomValue(HTNAtom& ioAtom)
{
    if (ioAtom.type == HTN_ATOM_TYPE_STRING)
    {
        HTNAtomStringStorage& String = ioAtom.value.string_value;
        if (String.data && String.data != String.inline_data &&
            String.capacity != HTN_ATOM_STRING_STATIC_CAPACITY)
            FreeStringBuffer(String.data, String.capacity);
    }
    else if (ioAtom.type == HTN_ATOM_TYPE_LIST)
    {
        HTNAtomList_Destroy(&ioAtom.value.list_value);
    }

    ioAtom.type = HTN_ATOM_TYPE_UNBOUND;
}

int SetAtomString(HTNAtom& ioAtom, const char* inData, const uint32_t inSize)
{
    if (inSize > 0u && !inData)
        return 0;

    if (ioAtom.type != HTN_ATOM_TYPE_STRING)
    {
        DestroyAtomValue(ioAtom);
        InitializeStringStorage(ioAtom.value.string_value);
        ioAtom.type = HTN_ATOM_TYPE_STRING;
    }

    HTNAtomStringStorage& String = ioAtom.value.string_value;
    if (String.capacity == HTN_ATOM_STRING_STATIC_CAPACITY)
    {
        if (inSize <= HTN_ATOM_STRING_INLINE_CAPACITY)
        {
            InitializeStringStorage(String);
        }
        else
        {
            char* NewData = AllocateStringBuffer(inSize);
            if (!NewData)
                return 0;
            String.data = NewData;
            String.capacity = inSize;
        }
    }
    else if (inSize > String.capacity)
    {
        char* NewData = AllocateStringBuffer(inSize);
        if (!NewData)
            return 0;
        if (String.data != String.inline_data)
            FreeStringBuffer(String.data, String.capacity);
        String.data = NewData;
        String.capacity = inSize;
    }

    if (inSize > 0u)
        std::memcpy(String.data, inData, inSize);
    String.data[inSize] = '\0';
    String.size = inSize;
    return 1;
}

void InitializeAtomStorage(HTNAtom& outAtom)
{
    outAtom.type = HTN_ATOM_TYPE_UNBOUND;
}

int CopyAtomValue(HTNAtom& outAtom, const HTNAtom& inAtom)
{
    switch (inAtom.type)
    {
    case HTN_ATOM_TYPE_BOOL: outAtom.value.bool_value = inAtom.value.bool_value; outAtom.type = HTN_ATOM_TYPE_BOOL; return 1;
    case HTN_ATOM_TYPE_INT: outAtom.value.int_value = inAtom.value.int_value; outAtom.type = HTN_ATOM_TYPE_INT; return 1;
    case HTN_ATOM_TYPE_FLOAT: outAtom.value.float_value = inAtom.value.float_value; outAtom.type = HTN_ATOM_TYPE_FLOAT; return 1;
    case HTN_ATOM_TYPE_STRING:
        if (SetAtomString(outAtom, inAtom.value.string_value.data, inAtom.value.string_value.size))
            return 1;
        DestroyAtomValue(outAtom);
        return 0;
    case HTN_ATOM_TYPE_SYMBOL: outAtom.value.symbol_value = inAtom.value.symbol_value; outAtom.type = HTN_ATOM_TYPE_SYMBOL; return 1;
    case HTN_ATOM_TYPE_LIST:
        HTNAtomList_InitWithAllocator(&outAtom.value.list_value, inAtom.value.list_value.allocator);
        if (HTNAtomList_Copy(&outAtom.value.list_value, &inAtom.value.list_value))
        {
            outAtom.type = HTN_ATOM_TYPE_LIST;
            return 1;
        }
        HTNAtomList_Destroy(&outAtom.value.list_value);
        outAtom.type = HTN_ATOM_TYPE_UNBOUND;
        return 0;
    case HTN_ATOM_TYPE_UNBOUND:
    default:
        outAtom.type = HTN_ATOM_TYPE_UNBOUND;
        return 1;
    }
}

void MoveAtomValue(HTNAtom& outAtom, HTNAtom& inAtom)
{
    switch (inAtom.type)
    {
    case HTN_ATOM_TYPE_STRING:
    {
        HTNAtomStringStorage& Source = inAtom.value.string_value;
        InitializeStringStorage(outAtom.value.string_value);
        if (Source.data == Source.inline_data)
        {
            outAtom.value.string_value.size = Source.size;
            std::memcpy(outAtom.value.string_value.inline_data, Source.inline_data, static_cast<size_t>(Source.size) + 1u);
        }
        else
        {
            outAtom.value.string_value.data = Source.data;
            outAtom.value.string_value.size = Source.size;
            outAtom.value.string_value.capacity = Source.capacity;
            InitializeStringStorage(Source);
        }
        outAtom.type = HTN_ATOM_TYPE_STRING;
        inAtom.type = HTN_ATOM_TYPE_UNBOUND;
        break;
    }
    case HTN_ATOM_TYPE_LIST:
        HTNAtomList_InitWithAllocator(&outAtom.value.list_value, inAtom.value.list_value.allocator);
        HTNAtomList_Move(&outAtom.value.list_value, &inAtom.value.list_value);
        outAtom.type = HTN_ATOM_TYPE_LIST;
        inAtom.type = HTN_ATOM_TYPE_UNBOUND;
        break;
    case HTN_ATOM_TYPE_BOOL: outAtom.value.bool_value = inAtom.value.bool_value; outAtom.type = HTN_ATOM_TYPE_BOOL; inAtom.type = HTN_ATOM_TYPE_UNBOUND; break;
    case HTN_ATOM_TYPE_INT: outAtom.value.int_value = inAtom.value.int_value; outAtom.type = HTN_ATOM_TYPE_INT; inAtom.type = HTN_ATOM_TYPE_UNBOUND; break;
    case HTN_ATOM_TYPE_FLOAT: outAtom.value.float_value = inAtom.value.float_value; outAtom.type = HTN_ATOM_TYPE_FLOAT; inAtom.type = HTN_ATOM_TYPE_UNBOUND; break;
    case HTN_ATOM_TYPE_SYMBOL: outAtom.value.symbol_value = inAtom.value.symbol_value; outAtom.type = HTN_ATOM_TYPE_SYMBOL; inAtom.type = HTN_ATOM_TYPE_UNBOUND; break;
    case HTN_ATOM_TYPE_UNBOUND:
    default: outAtom.type = HTN_ATOM_TYPE_UNBOUND; break;
    }
}

void DestroyAtomStorage(HTNAtom& ioAtom)
{
    DestroyAtomValue(ioAtom);
}
}

std::string HTNAtomGetString(const HTNAtom& inAtom)
{
    const char* Data = HTNAtom_GetStringData(&inAtom);
    const uint32_t Size = HTNAtom_GetStringSize(&inAtom);
    return Data ? std::string(Data, Size) : std::string();
}

std::string HTNAtomToString(const HTNAtom& inAtom, const bool inShouldDoubleQuoteString)
{
    switch (inAtom.type)
    {
    case HTN_ATOM_TYPE_BOOL: return std::format("{}", inAtom.value.bool_value != 0u);
    case HTN_ATOM_TYPE_INT: return std::to_string(inAtom.value.int_value);
    case HTN_ATOM_TYPE_FLOAT:
    {
        std::ostringstream Buffer;
        Buffer << std::fixed << std::setprecision(1) << inAtom.value.float_value;
        return Buffer.str();
    }
    case HTN_ATOM_TYPE_STRING:
    {
        const std::string StringValue = HTNAtomGetString(inAtom);
        return inShouldDoubleQuoteString ? std::format("\"{}\"", StringValue) : StringValue;
    }
    case HTN_ATOM_TYPE_SYMBOL:
    {
        const HtnSymbol* Symbol = static_cast<const HtnSymbol*>(inAtom.value.symbol_value);
        return Symbol ? Symbol->GetString() : std::string();
    }
    case HTN_ATOM_TYPE_LIST: return HTNAtomListToString(inAtom.value.list_value, inShouldDoubleQuoteString);
    case HTN_ATOM_TYPE_UNBOUND:
    default: return "";
    }
}

extern "C" HTNAtomDebugStats HTNAtomDebug_GetStats(void)
{
    HTNAtomDebugStats Stats{};
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    Stats.live_heap_strings = GHTNLiveHeapStrings.load(std::memory_order_relaxed);
    Stats.live_heap_string_bytes = GHTNLiveHeapStringBytes.load(std::memory_order_relaxed);
    const HTNAtomListAllocatorDebugStats ListStats = HTNAtomListAllocatorDebug_GetStats();
    Stats.live_list_nodes = ListStats.live_nodes;
    Stats.peak_heap_string_bytes = GHTNPeakHeapStringBytes.load(std::memory_order_relaxed);
    Stats.peak_live_list_nodes = ListStats.peak_live_nodes;
    Stats.heap_string_allocations = GHTNHeapStringAllocations.load(std::memory_order_relaxed);
    Stats.heap_string_frees = GHTNHeapStringFrees.load(std::memory_order_relaxed);
    Stats.list_node_allocations = ListStats.node_allocations;
    Stats.list_node_deallocations = ListStats.node_deallocations;
#endif
    return Stats;
}

extern "C" int HTNAtomDebug_HasLeaks(void)
{
    const HTNAtomDebugStats Stats = HTNAtomDebug_GetStats();
    return Stats.live_heap_strings != 0u || Stats.live_heap_string_bytes != 0u ||
           Stats.live_list_nodes != 0u;
}

extern "C" void HTNAtomDebug_ReportLeaks(void)
{
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    const HTNAtomDebugStats Stats = HTNAtomDebug_GetStats();
    if (Stats.live_heap_strings == 0u && Stats.live_heap_string_bytes == 0u &&
        Stats.live_list_nodes == 0u)
        return;

    std::fprintf(stderr,
        "[HTNAtom] owned-resource leak detected: heap strings=%llu, heap string bytes=%llu, list nodes=%llu "
        "(peaks: string bytes=%llu, list nodes=%llu)\n",
        static_cast<unsigned long long>(Stats.live_heap_strings),
        static_cast<unsigned long long>(Stats.live_heap_string_bytes),
        static_cast<unsigned long long>(Stats.live_list_nodes),
        static_cast<unsigned long long>(Stats.peak_heap_string_bytes),
        static_cast<unsigned long long>(Stats.peak_live_list_nodes));

#endif
}

extern "C" void HTNAtomDebug_ResetPeaks(void)
{
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    GHTNPeakHeapStringBytes.store(GHTNLiveHeapStringBytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
    HTNAtomListAllocatorDebug_ResetPeaks();
#endif
}

extern "C" void HTNAtom_Init(HTNAtom* ioAtom)
{
    if (ioAtom)
        InitializeAtomStorage(*ioAtom);
}

extern "C" int HTNAtom_CreateCall(HTNAtom* outAtom, const void* inHeadSymbol, const HTNAtom* inArguments, const uint32_t inArgumentCount)
{
    if (!outAtom)
        return 0;

    HTNAtom_Init(outAtom);
    if (!inHeadSymbol || (inArgumentCount > 0u && !inArguments))
        return 0;

    HTNAtom Head;
    HTNAtom_Init(&Head);
    HTNAtom_SetSymbol(&Head, inHeadSymbol);

    if (!HTNAtom_PushBackListElement(outAtom, &Head))
    {
        HTNAtom_Destroy(&Head);
        HTNAtom_Unbind(outAtom);
        return 0;
    }
    HTNAtom_Destroy(&Head);

    for (uint32_t ArgumentIndex = 0u; ArgumentIndex < inArgumentCount; ++ArgumentIndex)
    {
        if (!HTNAtom_IsBound(&inArguments[ArgumentIndex]) ||
            !HTNAtom_PushBackListElement(outAtom, &inArguments[ArgumentIndex]))
        {
            HTNAtom_Unbind(outAtom);
            return 0;
        }
    }

    return 1;
}


extern "C" int HTNAtom_CreateCallFromPointers(HTNAtom* outAtom, const void* inHeadSymbol, const HTNAtom* const* inArguments, const uint32_t inArgumentCount)
{
    if (!outAtom)
        return 0;

    HTNAtom_Init(outAtom);
    if (!inHeadSymbol || (inArgumentCount > 0u && !inArguments))
        return 0;

    HTNAtom Head;
    HTNAtom_Init(&Head);
    HTNAtom_SetSymbol(&Head, inHeadSymbol);
    if (!HTNAtom_PushBackListElement(outAtom, &Head))
    {
        HTNAtom_Destroy(&Head);
        HTNAtom_Unbind(outAtom);
        return 0;
    }
    HTNAtom_Destroy(&Head);

    for (uint32_t ArgumentIndex = 0u; ArgumentIndex < inArgumentCount; ++ArgumentIndex)
    {
        const HTNAtom* Argument = inArguments[ArgumentIndex];
        if (!Argument || !HTNAtom_IsBound(Argument) || !HTNAtom_PushBackListElement(outAtom, Argument))
        {
            HTNAtom_Unbind(outAtom);
            return 0;
        }
    }
    return 1;
}

extern "C" void HTNAtom_SetEmptyList(HTNAtom* ioAtom)
{
    if (!ioAtom)
        return;
    DestroyAtomValue(*ioAtom);
    HTNAtomList_Init(&ioAtom->value.list_value);
    ioAtom->type = HTN_ATOM_TYPE_LIST;
}

extern "C" void HTNAtom_Destroy(HTNAtom* ioAtom)
{
    if (ioAtom)
        DestroyAtomStorage(*ioAtom);
}

extern "C" int HTNAtom_Copy(HTNAtom* outAtom, const HTNAtom* inAtom)
{
    if (!outAtom || !inAtom)
        return 0;
    InitializeAtomStorage(*outAtom);
    return CopyAtomValue(*outAtom, *inAtom);
}

extern "C" void HTNAtom_Move(HTNAtom* outAtom, HTNAtom* inAtom)
{
    if (!outAtom || !inAtom)
        return;
    InitializeAtomStorage(*outAtom);
    MoveAtomValue(*outAtom, *inAtom);
}

extern "C" int HTNAtom_AssignCopy(HTNAtom* ioAtom, const HTNAtom* inAtom)
{
    if (!ioAtom || !inAtom)
        return 0;
    if (ioAtom == inAtom)
        return 1;
    DestroyAtomValue(*ioAtom);
    return CopyAtomValue(*ioAtom, *inAtom);
}

extern "C" void HTNAtom_AssignMove(HTNAtom* ioAtom, HTNAtom* inAtom)
{
    if (!ioAtom || !inAtom || ioAtom == inAtom)
        return;
    DestroyAtomValue(*ioAtom);
    MoveAtomValue(*ioAtom, *inAtom);
}

extern "C" void HTNAtom_InitRange(HTNAtom* ioAtoms, const uint32_t inCount)
{
    if (!ioAtoms) return;
    for (uint32_t I = 0u; I < inCount; ++I) HTNAtom_Init(&ioAtoms[I]);
}

extern "C" void HTNAtom_DestroyRange(HTNAtom* ioAtoms, const uint32_t inCount)
{
    if (!ioAtoms) return;
    for (uint32_t I = 0u; I < inCount; ++I) HTNAtom_Destroy(&ioAtoms[I]);
}

extern "C" int HTNAtom_CopyRange(HTNAtom* outAtoms, const HTNAtom* inAtoms, const uint32_t inCount)
{
    if ((!outAtoms || !inAtoms) && inCount != 0u) return 0;
    for (uint32_t I = 0u; I < inCount; ++I)
    {
        if (!HTNAtom_Copy(&outAtoms[I], &inAtoms[I]))
        {
            HTNAtom_DestroyRange(outAtoms, I + 1u);
            return 0;
        }
    }
    return 1;
}

extern "C" void HTNAtom_MoveRange(HTNAtom* outAtoms, HTNAtom* inAtoms, const uint32_t inCount)
{
    if (!outAtoms || !inAtoms) return;
    for (uint32_t I = 0u; I < inCount; ++I)
    {
        HTNAtom_Move(&outAtoms[I], &inAtoms[I]);
    }
}

extern "C" void HTNAtom_SetBool(HTNAtom* ioAtom, const uint8_t inValue)
{
    if (!ioAtom)
        return;
    DestroyAtomValue(*ioAtom);
    ioAtom->value.bool_value = inValue ? 1u : 0u;
    ioAtom->type = HTN_ATOM_TYPE_BOOL;
}

extern "C" void HTNAtom_SetInt(HTNAtom* ioAtom, const int32_t inValue)
{
    if (!ioAtom)
        return;
    DestroyAtomValue(*ioAtom);
    ioAtom->value.int_value = inValue;
    ioAtom->type = HTN_ATOM_TYPE_INT;
}

extern "C" void HTNAtom_SetFloat(HTNAtom* ioAtom, const float inValue)
{
    if (!ioAtom)
        return;
    DestroyAtomValue(*ioAtom);
    ioAtom->value.float_value = inValue;
    ioAtom->type = HTN_ATOM_TYPE_FLOAT;
}

extern "C" void HTNAtom_SetSymbol(HTNAtom* ioAtom, const void* inSymbol)
{
    if (!ioAtom)
        return;
    DestroyAtomValue(*ioAtom);
    ioAtom->value.symbol_value = inSymbol;
    ioAtom->type = HTN_ATOM_TYPE_SYMBOL;
}

extern "C" int HTNAtom_SetString(HTNAtom* ioAtom, const char* inData, const uint32_t inSize)
{
    return ioAtom ? SetAtomString(*ioAtom, inData, inSize) : 0;
}

extern "C" int HTNAtom_SetListCopy(HTNAtom* ioAtom, const HTNAtomList* inList)
{
    if (!ioAtom || !inList)
        return 0;

    DestroyAtomValue(*ioAtom);
    HTNAtomList_InitWithAllocator(&ioAtom->value.list_value, inList->allocator);
    if (!HTNAtomList_Copy(&ioAtom->value.list_value, inList))
    {
        HTNAtomList_Destroy(&ioAtom->value.list_value);
        ioAtom->type = HTN_ATOM_TYPE_UNBOUND;
        return 0;
    }

    ioAtom->type = HTN_ATOM_TYPE_LIST;
    return 1;
}

extern "C" void HTNAtom_SetListMove(HTNAtom* ioAtom, HTNAtomList* inList)
{
    if (!ioAtom || !inList)
        return;

    DestroyAtomValue(*ioAtom);
    HTNAtomList_InitWithAllocator(&ioAtom->value.list_value, inList->allocator);
    HTNAtomList_Move(&ioAtom->value.list_value, inList);
    ioAtom->type = HTN_ATOM_TYPE_LIST;
}

extern "C" void HTNAtom_Unbind(HTNAtom* ioAtom)
{
    if (ioAtom)
        DestroyAtomValue(*ioAtom);
}

extern "C" int HTNAtom_Equals(const HTNAtom* inLeft, const HTNAtom* inRight)
{
    if (!inLeft || !inRight)
        return inLeft == inRight;
    if (inLeft->type != inRight->type)
        return 0;

    switch (inLeft->type)
    {
    case HTN_ATOM_TYPE_UNBOUND: return 1;
    case HTN_ATOM_TYPE_BOOL: return inLeft->value.bool_value == inRight->value.bool_value;
    case HTN_ATOM_TYPE_INT: return inLeft->value.int_value == inRight->value.int_value;
    case HTN_ATOM_TYPE_FLOAT: return inLeft->value.float_value == inRight->value.float_value;
    case HTN_ATOM_TYPE_STRING:
        return inLeft->value.string_value.size == inRight->value.string_value.size &&
               (inLeft->value.string_value.size == 0u ||
                std::memcmp(inLeft->value.string_value.data, inRight->value.string_value.data, inLeft->value.string_value.size) == 0);
    case HTN_ATOM_TYPE_SYMBOL: return inLeft->value.symbol_value == inRight->value.symbol_value;
    case HTN_ATOM_TYPE_LIST: return HTNAtomList_Equals(&inLeft->value.list_value, &inRight->value.list_value);
    default: return 0;
    }
}

extern "C" HTNAtomType HTNAtom_GetType(const HTNAtom* inAtom)
{
    return inAtom ? inAtom->type : HTN_ATOM_TYPE_UNBOUND;
}

extern "C" int HTNAtom_IsBound(const HTNAtom* inAtom)
{
    return inAtom && inAtom->type != HTN_ATOM_TYPE_UNBOUND;
}

extern "C" const char* HTNAtom_GetStringData(const HTNAtom* inAtom)
{
    return inAtom && inAtom->type == HTN_ATOM_TYPE_STRING ? inAtom->value.string_value.data : nullptr;
}

extern "C" uint32_t HTNAtom_GetStringSize(const HTNAtom* inAtom)
{
    return inAtom && inAtom->type == HTN_ATOM_TYPE_STRING ? inAtom->value.string_value.size : 0u;
}

extern "C" int HTNAtom_PushBackListElement(HTNAtom* ioAtom, const HTNAtom* inValue)
{
    if (!ioAtom || !inValue)
        return 0;

    if (!HTNAtom_IsBound(ioAtom))
    {
        HTNAtomList_Init(&ioAtom->value.list_value);
        ioAtom->type = HTN_ATOM_TYPE_LIST;
    }
    if (ioAtom->type != HTN_ATOM_TYPE_LIST)
        return 0;
    return HTNAtomList_PushBack(&ioAtom->value.list_value, inValue);
}

extern "C" int HTNAtom_PushBackListElementMove(HTNAtom* ioAtom, HTNAtom* ioValue)
{
    if (!ioAtom || !ioValue)
        return 0;

    if (!HTNAtom_IsBound(ioAtom))
    {
        HTNAtomList_Init(&ioAtom->value.list_value);
        ioAtom->type = HTN_ATOM_TYPE_LIST;
    }
    if (ioAtom->type != HTN_ATOM_TYPE_LIST)
        return 0;
    return HTNAtomList_PushBackMove(&ioAtom->value.list_value, ioValue);
}

extern "C" const HTNAtom* HTNAtom_GetListElement(const HTNAtom* inAtom, const uint32_t inIndex)
{
    return inAtom && inAtom->type == HTN_ATOM_TYPE_LIST ? HTNAtomList_Get(&inAtom->value.list_value, inIndex) : nullptr;
}

extern "C" int32_t HTNAtom_GetListSize(const HTNAtom* inAtom)
{
    return inAtom && inAtom->type == HTN_ATOM_TYPE_LIST
        ? static_cast<int32_t>(HTNAtomList_GetSize(&inAtom->value.list_value))
        : -1;
}

extern "C" int HTNAtom_IsListEmpty(const HTNAtom* inAtom)
{
    return !inAtom || inAtom->type != HTN_ATOM_TYPE_LIST || HTNAtomList_IsEmpty(&inAtom->value.list_value);
}
