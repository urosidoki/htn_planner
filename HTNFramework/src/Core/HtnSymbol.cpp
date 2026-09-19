// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HtnSymbol.h"
#include "Core/HtnSymbolGenerated.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
std::uint64_t HashSymbolText(const std::string& inText)
{
    // Stable 64-bit FNV-1a. The hash is an acceleration/debug value only;
    // symbol identity is the interned object (or its collision-free unique ID).
    std::uint64_t Hash = 14695981039346656037ull;
    for (const unsigned char Character : inText)
    {
        Hash ^= static_cast<std::uint64_t>(Character);
        Hash *= 1099511628211ull;
    }
    return Hash;
}

struct HtnSymbolTable
{
    std::mutex Mutex;
    std::unordered_map<std::string, std::unique_ptr<HtnSymbol>> Symbols;
    std::uint64_t NextID = 1u;
};

HtnSymbolTable& GetSymbolTable()
{
    static HtnSymbolTable Table;
    return Table;
}
}

const HtnSymbol* HtnSymbol::sGetSymbol(const char* inText)
{
    return sGetSymbol(inText ? std::string(inText) : std::string());
}

const HtnSymbol* HtnSymbol::sGetSymbol(const std::string& inText)
{
    HtnSymbolTable& Table = GetSymbolTable();
    std::lock_guard<std::mutex> Lock(Table.Mutex);

    const auto Existing = Table.Symbols.find(inText);
    if (Existing != Table.Symbols.end())
        return Existing->second.get();

    auto Symbol = std::unique_ptr<HtnSymbol>(new HtnSymbol(inText, HashSymbolText(inText), Table.NextID++));
    const HtnSymbol* Result = Symbol.get();
    Table.Symbols.emplace(inText, std::move(Symbol));
    return Result;
}

extern "C" const HtnSymbol* HtnSymbol_InternGenerated(const char* inText)
{
    return inText ? HtnSymbol::sGetSymbol(inText) : nullptr;
}
