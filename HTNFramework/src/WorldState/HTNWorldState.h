// Copyright (c) 2023 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtom.h"
#include "HTNCoreMinimal.h"
#include "WorldState/HTNWorldStateFwd.h"
#include "WorldState/HTNWorldStateHelpers.h"
#include "WorldState/HTNFactRegistry.h"

#include <cassert>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * Collection of fact arguments
 * - The table has a fixed number of fact arguments
 * - Each row represents a group of fact arguments, and each column represents a single fact argument within its group
 * - Duplicate rows are allowed; each write appends one row
 */
class HTNFactArgumentsTable
{
public:
    // Removes the row at the given index from the table
    void RemoveFactArguments(const size inFactArgumentsIndex);

    // Removes all rows from the table
    void RemoveAllFactArguments();

    // Checks if the given fact arguments match the fact arguments of the row at the given index
    // Binds the fact arguments that are unbound
    template<typename T>
    HTN_NODISCARD bool Check(const size inFactArgumentsIndex, T& ioFactArguments) const;

    // Checks if the given fact arguments match the fact arguments of any row
    template<typename T>
    HTN_NODISCARD bool ContainsFactArguments(const T& inFactArguments) const;

    // Returns the fact arguments collection
    HTN_NODISCARD const HTNFactArgumentsCollection& GetFactArgumentsCollection() const;

    // Returns the number of entries in the table
    HTN_NODISCARD size GetFactArgumentsCollectionSize() const;


private:
    friend class HTNWorldState;

    // Adds a new row with the given fact arguments to the table
    template<typename T>
    void AddFactArguments(const T& inFactArguments);

    HTNFactArgumentsCollection mFactArgumentsCollection;
};

/**
 * Database of facts that represent the knowledge about the world
 * - Each fact ID can be mapped to multiple fact arguments tables, but it can only contain one fact argument table for each number of fact arguments
 */
class HTNWorldState
{
public:
    // Associates this runtime world state with the fact slots of one parsed domain.
    // The registry is not owned by the world state and must outlive it.
    void SetFactRegistry(const HTNFactRegistry* inFactRegistry);

    // Resolves a symbol to the slot assigned by the current domain.
    // Symbols not used by the domain return HTN_INVALID_FACT_SLOT.
    HTN_NODISCARD HTNFactSlot FindFactSlot(const HtnSymbol* inFact) const;

    // Appends one fact row using an interned symbol. If the fact is not referenced
    // by the current domain, nothing is written and false is returned. Duplicate
    // rows are intentionally preserved.
    template<typename... TArgs>
    bool WriteFact(const HtnSymbol* inFact, TArgs&&... inArguments);

    // Clears all rows for one fact/arity. Intended for daemons that rebuild a
    // dynamic fact table deterministically each tick/phase.
    bool ClearFact(const HtnSymbol* inFact, size inFactArgumentsSize);

    // Appends a row with the given fact arguments to the corresponding table
    template<typename T>
    void AddFact(const std::string& inFactID, const T& inFactArguments);

    // Removes the row at the given index from the corresponding table
    void RemoveFact(const std::string& inFactID, const size inFactArgumentsSize, const size inFactArgumentsIndex);

    // Removes all fact rows while retaining the fact/table storage so a snapshot can be rebuilt
    // without recreating the outer WorldState containers.
    void RemoveAllFacts();

    // If all fact arguments are bound, returns 1
    // Otherwise, returns the number of rows of the corresponding table
    template<typename T>
    HTN_NODISCARD size Query(const std::string& inFactID, const T& inFactArguments) const;

    // If all fact arguments are bound, returns true
    // Otherwise, checks the given fact arguments with the fact arguments of the row at the given index of the corresponding table
    template<typename T>
    HTN_NODISCARD bool QueryIndex(const std::string& inFactID, const size inFactArgumentsIndex, T& ioFactArguments) const;

    // Checks the given fact arguments with the fact arguments of the row at the given index of the corresponding table
    template<typename T>
    HTN_NODISCARD bool CheckIndex(const std::string& inFactID, const size inFactArgumentsIndex, T& ioFactArguments) const;

    // Returns the number of tables associated to the given fact ID
    HTN_NODISCARD size GetFactArgumentsTablesSize(const std::string& inFactID) const;

    // Returns whether the table of the given number of fact arguments is associated to the given fact ID
    HTN_NODISCARD bool ContainsFactArgumentsTable(const std::string& inFactID, const size inFactArgumentsSize) const;

    // Returns the number of rows of the given table
    HTN_NODISCARD size GetFactArgumentsCollectionSize(const std::string& inFactID, const size inFactArgumentsSize) const;

    // Returns the fact table directly so generated planner code can perform one fact-id lookup per query
    // instead of one lookup per candidate row. Returns nullptr if the fact/table does not exist.
    HTN_NODISCARD const HTNFactArgumentsTable* FindFactArgumentsTable(const std::string& inFactID, const size inFactArgumentsSize) const;
    HTN_NODISCARD const HTNFactArgumentsTable* FindFactArgumentsTable(const HtnSymbol* inFact, const size inFactArgumentsSize) const;

    // Returns the complete arity table set for one fact without creating it.
    HTN_NODISCARD const HTNFactArgumentsTables* FindFactArgumentsTables(const HtnSymbol* inFact) const;
    HTN_NODISCARD HTNFactArgumentsTables* FindFactArgumentsTables(const HtnSymbol* inFact);

    // Returns the complete arity table set for one fact, creating an empty entry when necessary.
    // Creating a new fact-table entry advances the storage generation so generated execution storage
    // that currently points at the shared empty table can resolve the new table on the next plan.
    HTN_NODISCARD HTNFactArgumentsTables* FindOrAddFactArgumentsTables(const std::string& inFactID);
    HTN_NODISCARD HTNFactArgumentsTables* FindOrAddFactArgumentsTables(const HtnSymbol* inFact);

    // Changes whenever the set of fact-table entries changes. Generated execution storage uses it
    // to invalidate cached slots when a previously-missing fact is created and must replace a
    // shared empty-table slot.
    HTN_NODISCARD std::uint64_t GetFactStorageGeneration() const;

    // Returns whether the given fact arguments are contained by the table associated to the given fact ID
    template<typename T>
    HTN_NODISCARD bool ContainsFactArguments(const std::string& inFactID, const T& inFactArguments) const;

    // Returns the facts
    HTN_NODISCARD const HTNFacts& GetFacts() const;

private:
    HTNFactArgumentsTables& FindOrCreateFactArgumentsTables(const HtnSymbol* inFact);

    HTNFacts mFacts;
    std::uint64_t mFactStorageGeneration = 1u;
    const HTNFactRegistry* mFactRegistry = nullptr;
};

inline void HTNFactArgumentsTable::RemoveAllFactArguments()
{
    mFactArgumentsCollection.clear();
}

template<typename T>
bool HTNFactArgumentsTable::Check(const size inFactArgumentsIndex, T& ioFactArguments) const
{
    if (inFactArgumentsIndex >= mFactArgumentsCollection.size())
    {
        HTN_LOG_ERROR("Fact arguments index [{}] outside of bounds [{}]", inFactArgumentsIndex, mFactArgumentsCollection.size());
        return false;
    }

    const HTNFactArguments& FactArguments     = mFactArgumentsCollection[inFactArgumentsIndex];
    const typename T::iterator       ItBegin           = std::begin(ioFactArguments);
    const size              FactArgumentsSize = std::distance(ItBegin, std::end(ioFactArguments));
    assert(FactArgumentsSize < HTNWorldStateHelpers::kFactArgumentsSize);

    // Check bound arguments
    for (size i = 0; i < FactArgumentsSize; ++i)
    {
        auto It = ItBegin;
        std::advance(It, i);

        const auto& ioFactArgument = *It;
        if (!ioFactArgument.IsBound())
        {
            continue;
        }

        const HTNAtom& FactArgument = FactArguments[i];
        if (ioFactArgument != FactArgument)
        {
            return false;
        }
    }

    // Bind unbound arguments
    for (size i = 0; i < FactArgumentsSize; ++i)
    {
        auto It = ItBegin;
        std::advance(It, i);

        auto& ioFactArgument = *It;
        if (ioFactArgument.IsBound())
        {
            continue;
        }

        const HTNAtom& FactArgument = FactArguments[i];
        ioFactArgument              = FactArgument;
    }

    return true;
}

template<typename T>
bool HTNFactArgumentsTable::ContainsFactArguments(const T& inFactArguments) const
{
    const typename T::const_iterator ItBegin           = std::cbegin(inFactArguments);
    const size              FactArgumentsSize = std::distance(ItBegin, std::cend(inFactArguments));
    assert(FactArgumentsSize < HTNWorldStateHelpers::kFactArgumentsSize);

    for (const HTNFactArguments& FactArguments : mFactArgumentsCollection)
    {
        bool Result = true;
        for (size i = 0; i < FactArgumentsSize; ++i)
        {
            const HTNAtom& FactArgument = FactArguments[i];

            auto It = ItBegin;
            std::advance(It, i);

            const HTNAtom& inFactArgument = *It;
            if (FactArgument != inFactArgument)
            {
                Result = false;
                break;
            }
        }

        if (Result)
        {
            return true;
        }
    }

    return false;
}

inline const HTNFactArgumentsCollection& HTNFactArgumentsTable::GetFactArgumentsCollection() const
{
    return mFactArgumentsCollection;
}

inline size HTNFactArgumentsTable::GetFactArgumentsCollectionSize() const
{
    return mFactArgumentsCollection.size();
}

template<typename T>
void HTNFactArgumentsTable::AddFactArguments(const T& inFactArguments)
{
    const typename T::const_iterator ItBegin           = std::cbegin(inFactArguments);
    const size              FactArgumentsSize = std::distance(ItBegin, std::cend(inFactArguments));
    assert(FactArgumentsSize < HTNWorldStateHelpers::kFactArgumentsSize);

    HTNFactArguments FactArguments;
    for (size i = 0; i < FactArgumentsSize; ++i)
    {
        auto It = ItBegin;
        std::advance(It, i);

        FactArguments[i] = *It;
    }

    mFactArgumentsCollection.emplace_back(FactArguments);
}

namespace HTNWorldStateWriteHelpers
{
inline HTNAtomOwner MakeAtom(const HTNAtom& inValue) { return HTNAtomOwner(inValue); }
inline HTNAtomOwner MakeAtom(HTNAtom&& inValue) { return HTNAtomOwner(std::move(inValue)); }
inline HTNAtomOwner MakeAtom(const char* inValue) { return HTNAtomOwner(std::string(inValue ? inValue : "")); }
inline HTNAtomOwner MakeAtom(char* inValue) { return HTNAtomOwner(std::string(inValue ? inValue : "")); }

template<typename T>
HTNAtomOwner MakeAtom(T&& inValue)
{
    return HTNAtomOwner(std::forward<T>(inValue));
}
}

inline void HTNWorldState::SetFactRegistry(const HTNFactRegistry* inFactRegistry)
{
    mFactRegistry = inFactRegistry;
}

inline HTNFactSlot HTNWorldState::FindFactSlot(const HtnSymbol* inFact) const
{
    return mFactRegistry ? mFactRegistry->FindSlot(inFact) : HTN_INVALID_FACT_SLOT;
}

template<typename... TArgs>
bool HTNWorldState::WriteFact(const HtnSymbol* inFact, TArgs&&... inArguments)
{
    const HTNFactSlot FactSlot = FindFactSlot(inFact);
    if (FactSlot == HTN_INVALID_FACT_SLOT || !inFact)
        return false;

    static_assert(sizeof...(TArgs) < HTNWorldStateHelpers::kFactArgumentsSize,
                  "Too many arguments for an HTN fact");
    std::array<HTNAtomOwner, sizeof...(TArgs)> Arguments{
        HTNWorldStateWriteHelpers::MakeAtom(std::forward<TArgs>(inArguments))...
    };

    HTNFactArgumentsTables& Tables = FindOrCreateFactArgumentsTables(inFact);
    HTNFactArgumentsTable& Table = Tables[sizeof...(TArgs)];
    Table.AddFactArguments(Arguments);
    return true;
}

inline bool HTNWorldState::ClearFact(const HtnSymbol* inFact, const size inFactArgumentsSize)
{
    const HTNFactSlot FactSlot = FindFactSlot(inFact);
    if (FactSlot == HTN_INVALID_FACT_SLOT || !inFact || inFactArgumentsSize >= HTNWorldStateHelpers::kFactArgumentsSize)
        return false;

    const auto It = mFacts.find(inFact);
    if (It == mFacts.end())
        return true;
    It->second[inFactArgumentsSize].RemoveAllFactArguments();
    return true;
}

template<typename T>
void HTNWorldState::AddFact(const std::string& inFactID, const T& inFactArguments)
{
    const HtnSymbol* FactSymbol = HtnSymbol::sGetSymbol(inFactID);
    HTNFactArgumentsTables& FactArgumentsTables = FindOrCreateFactArgumentsTables(FactSymbol);
    const size              FactArgumentsSize   = std::distance(std::cbegin(inFactArguments), std::cend(inFactArguments));
    assert(FactArgumentsSize < HTNWorldStateHelpers::kFactArgumentsSize);

    HTNFactArgumentsTable& FactArgumentsTable = FactArgumentsTables[FactArgumentsSize];
    FactArgumentsTable.AddFactArguments(inFactArguments);
}

inline void HTNWorldState::RemoveAllFacts()
{
    for (auto& Fact : mFacts)
    {
        for (HTNFactArgumentsTable& Table : Fact.second)
            Table.RemoveAllFactArguments();
    }
}

template<typename T>
size HTNWorldState::Query(const std::string& inFactID, const T& inFactArguments) const
{
    const uint32 FactArgumentsBoundNum = HTNWorldStateHelpers::CountFactArgumentsBound(inFactArguments);
    const size   FactArgumentsSize     = std::distance(std::cbegin(inFactArguments), std::cend(inFactArguments));
    if (FactArgumentsBoundNum == FactArgumentsSize)
    {
        // A fully-bound query has exactly one possible candidate, but it only
        // succeeds when that row is actually present in the world state.
        return ContainsFactArguments(inFactID, inFactArguments) ? 1u : 0u;
    }

    return GetFactArgumentsCollectionSize(inFactID, FactArgumentsSize);
}

template<typename T>
bool HTNWorldState::QueryIndex(const std::string& inFactID, const size inFactArgumentsIndex, T& ioFactArguments) const
{
    const uint32 FactArgumentsBoundNum = HTNWorldStateHelpers::CountFactArgumentsBound(ioFactArguments);
    const size   FactArgumentsSize     = std::distance(std::cbegin(ioFactArguments), std::cend(ioFactArguments));
    if (FactArgumentsBoundNum == FactArgumentsSize)
    {
        // Keep QueryIndex consistent with Query: a fully-bound fact is not an
        // unconditional success; it must exist in the world state.
        return ContainsFactArguments(inFactID, ioFactArguments);
    }

    return CheckIndex(inFactID, inFactArgumentsIndex, ioFactArguments);
}

template<typename T>
bool HTNWorldState::CheckIndex(const std::string& inFactID, const size inFactArgumentsIndex, T& ioFactArguments) const
{
    const size FactArgumentsSize = std::distance(std::cbegin(ioFactArguments), std::cend(ioFactArguments));
    assert(FactArgumentsSize < HTNWorldStateHelpers::kFactArgumentsSize);

    const HTNFactArgumentsTable* FactArgumentsTable = FindFactArgumentsTable(inFactID, FactArgumentsSize);
    if (!FactArgumentsTable)
        return false;

    return FactArgumentsTable->Check(inFactArgumentsIndex, ioFactArguments);
}

template<typename T>
bool HTNWorldState::ContainsFactArguments(const std::string& inFactID, const T& inFactArguments) const
{
    const size FactArgumentsSize = std::distance(std::cbegin(inFactArguments), std::cend(inFactArguments));
    assert(FactArgumentsSize < HTNWorldStateHelpers::kFactArgumentsSize);

    const HTNFactArgumentsTable* FactArgumentsTable = FindFactArgumentsTable(inFactID, FactArgumentsSize);
    return FactArgumentsTable ? FactArgumentsTable->ContainsFactArguments(inFactArguments) : false;
}

inline std::uint64_t HTNWorldState::GetFactStorageGeneration() const
{
    return mFactStorageGeneration;
}

inline const HTNFacts& HTNWorldState::GetFacts() const
{
    return mFacts;
}
