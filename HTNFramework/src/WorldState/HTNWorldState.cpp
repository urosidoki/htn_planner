// Copyright (c) 2023 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "WorldState/HTNWorldState.h"
#include "WorldState/HTNGeneratedWorldState.h"
#include <utility>

void HTNFactArgumentsTable::RemoveFactArguments(const size inFactArgumentsIndex)
{
    if (inFactArgumentsIndex >= mFactArgumentsCollection.size())
    {
        HTN_LOG_ERROR("Fact arguments index [{}] outside of bounds [{}]", inFactArgumentsIndex, mFactArgumentsCollection.size());
        return;
    }

    auto It = mFactArgumentsCollection.cbegin();
    std::advance(It, inFactArgumentsIndex);
    mFactArgumentsCollection.erase(It);
}

void HTNWorldState::RemoveFact(const std::string& inFactID, const size inFactArgumentsSize, const size inFactArgumentsIndex)
{
    const auto It = mFacts.find(HtnSymbol::sGetSymbol(inFactID));
    if (It == mFacts.cend())
    {
        // Removing an absent fact is an idempotent no-op.
        return;
    }

    HTNFactArgumentsTables& FactArgumentsTables = It->second;
    assert(inFactArgumentsSize < HTNWorldStateHelpers::kFactArgumentsSize);

    HTNFactArgumentsTable& FactArgumentsTable = FactArgumentsTables[inFactArgumentsSize];
    FactArgumentsTable.RemoveFactArguments(inFactArgumentsIndex);
}

size HTNWorldState::GetFactArgumentsTablesSize(const std::string& inFactID) const
{
    const auto It = mFacts.find(HtnSymbol::sGetSymbol(inFactID));
    if (It == mFacts.cend())
    {
        // Missing facts are a normal query result. Conditions frequently probe facts
        // that are not present in the current world state, which semantically means
        // that the corresponding table contains zero rows. Do not report this as an
        // error; callers that require a fact to exist must validate that contract
        // explicitly.
        return 0;
    }

    size FactArgumentsTablesSize = 0;

    const HTNFactArgumentsTables& FactArgumentsTables = It->second;
    for (const HTNFactArgumentsTable& FactArgumentsTable : FactArgumentsTables)
    {
        const size FactArgumentsCollectionSize = FactArgumentsTable.GetFactArgumentsCollectionSize();
        if (FactArgumentsCollectionSize > 0)
        {
            ++FactArgumentsTablesSize;
        }
    }

    return FactArgumentsTablesSize;
}

bool HTNWorldState::ContainsFactArgumentsTable(const std::string& inFactID, const size inFactArgumentsSize) const
{
    const HTNFactArgumentsTable* FactArgumentsTable = FindFactArgumentsTable(inFactID, inFactArgumentsSize);
    return FactArgumentsTable && FactArgumentsTable->GetFactArgumentsCollectionSize() > 0;
}

size HTNWorldState::GetFactArgumentsCollectionSize(const std::string& inFactID, const size inFactArgumentsSize) const
{
    const auto It = mFacts.find(HtnSymbol::sGetSymbol(inFactID));
    if (It == mFacts.cend())
    {
        // Missing facts are a normal query result: an absent table has zero rows.
        // Do not emit an error here; callers that require existence must validate it explicitly.
        return 0;
    }

    const HTNFactArgumentsTables& FactArgumentsTables = It->second;
    assert(inFactArgumentsSize < HTNWorldStateHelpers::kFactArgumentsSize);

    const HTNFactArgumentsTable& FactArgumentsTable = FactArgumentsTables[inFactArgumentsSize];
    return FactArgumentsTable.GetFactArgumentsCollectionSize();
}


const HTNFactArgumentsTable* HTNWorldState::FindFactArgumentsTable(const std::string& inFactID, const size inFactArgumentsSize) const
{
    return FindFactArgumentsTable(HtnSymbol::sGetSymbol(inFactID), inFactArgumentsSize);
}

const HTNFactArgumentsTable* HTNWorldState::FindFactArgumentsTable(const HtnSymbol* inFact, const size inFactArgumentsSize) const
{
    if (!inFact)
        return nullptr;
    const auto It = mFacts.find(inFact);
    if (It == mFacts.cend())
        return nullptr;
    assert(inFactArgumentsSize < HTNWorldStateHelpers::kFactArgumentsSize);
    return &It->second[inFactArgumentsSize];
}

const HTNFactArgumentsTables* HTNWorldState::FindFactArgumentsTables(const HtnSymbol* inFact) const
{
    if (!inFact)
        return nullptr;
    const auto It = mFacts.find(inFact);
    return It != mFacts.end() ? &It->second : nullptr;
}

HTNFactArgumentsTables* HTNWorldState::FindFactArgumentsTables(const HtnSymbol* inFact)
{
    return const_cast<HTNFactArgumentsTables*>(std::as_const(*this).FindFactArgumentsTables(inFact));
}

HTNFactArgumentsTables* HTNWorldState::FindOrAddFactArgumentsTables(const std::string& inFactID)
{
    return FindOrAddFactArgumentsTables(HtnSymbol::sGetSymbol(inFactID));
}

HTNFactArgumentsTables& HTNWorldState::FindOrCreateFactArgumentsTables(const HtnSymbol* inFact)
{
    const auto [It, Inserted] = mFacts.try_emplace(inFact);
    if (Inserted)
    {
        ++mFactStorageGeneration;
        if (mFactStorageGeneration == 0u)
            mFactStorageGeneration = 1u;
    }
    return It->second;
}

HTNFactArgumentsTables* HTNWorldState::FindOrAddFactArgumentsTables(const HtnSymbol* inFact)
{
    return inFact ? &FindOrCreateFactArgumentsTables(inFact) : nullptr;
}


extern "C" std::uint64_t HTNWorldState_GetFactStorageGeneration(const HTNWorldState* inWorldState)
{
    return inWorldState ? inWorldState->GetFactStorageGeneration() : 0u;
}

extern "C" const void* HTNWorldState_ResolveGeneratedFactTables(HTNWorldState* inWorldState,
                                                                 const HtnSymbol* inFactSymbol)
{
    if (!inWorldState || !inFactSymbol)
        return nullptr;

    const HTNFactArgumentsTables* Tables = inWorldState->FindFactArgumentsTables(inFactSymbol);
    if (Tables)
        return Tables;

    // Generated read-only queries treat all absent predicates identically: zero
    // rows for every arity. Keep that representation here with the WorldState
    // lookup instead of teaching generated planner code about missing predicates.
    static const HTNFactArgumentsTables EmptyFactArgumentsTables{};
    return &EmptyFactArgumentsTables;
}

extern "C" void HTNWorldState_BeginGeneratedFactRowCursor(
    const void* inFactTables,
    const std::uint32_t inArgumentCount,
    HTNGeneratedFactRowCursor* outCursor)
{
    const auto* Tables = static_cast<const HTNFactArgumentsTables*>(inFactTables);
    const HTNFactArgumentsTable* Table = &(*Tables)[inArgumentCount];

    outCursor->table = Table;
    outCursor->next = 0u;
    outCursor->argument_count = inArgumentCount;
    outCursor->row_count = static_cast<std::uint64_t>(Table->GetFactArgumentsCollectionSize());
}

extern "C" int HTNWorldState_NextGeneratedFactRow(
    HTNGeneratedFactRowCursor* ioCursor,
    const HTNAtom** outArguments)
{
    if (ioCursor->next >= ioCursor->row_count)
        return 0;

    const size RowIndex = static_cast<size>(ioCursor->next++);

    if (ioCursor->argument_count != 0u)
    {
        const auto* Table = static_cast<const HTNFactArgumentsTable*>(ioCursor->table);
        const HTNFactArguments& Row = Table->GetFactArgumentsCollection()[RowIndex];
        for (std::uint32_t Argument = 0u; Argument < ioCursor->argument_count; ++Argument)
            outArguments[Argument] = Row[Argument].Get();
    }

    return 1;
}
