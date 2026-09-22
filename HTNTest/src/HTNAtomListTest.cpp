// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "Core/HTNAtom.h"
#include "Core/HTNAtomList.h"
#include "Core/HTNAtomOwner.h"
#include "Core/HTNAtomListOwner.h"
#include "Core/HTNAtomListAllocator.h"
#include "Core/HTNAtomNode.h"
#include "Core/HTNTypeConversion.h"
#include "Core/HtnSymbol.h"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>


struct HTNTestVector3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

template<>
struct HTNTypeTraits<HTNTestVector3>
{
    static constexpr bool IsSupported      = true;
    static constexpr bool HasFixedAtomType = true;
    static constexpr HTNAtomType AtomType  = HTN_ATOM_TYPE_LIST;
    static constexpr const char* Name      = "HTNTestVector3";
};

template<>
struct HTNTypeConverter<HTNTestVector3>
{
    static bool FromAtom([[maybe_unused]] void* inClientContext, const HTNAtom& inAtom, HTNTestVector3& outValue)
    {
        if (HTNAtom_GetType(&inAtom) != HTN_ATOM_TYPE_LIST || HTNAtom_GetListSize(&inAtom) != 3)
            return false;

        const HTNAtom* X = HTNAtom_GetListElement(&inAtom, 0u);
        const HTNAtom* Y = HTNAtom_GetListElement(&inAtom, 1u);
        const HTNAtom* Z = HTNAtom_GetListElement(&inAtom, 2u);
        if (!X || !Y || !Z || HTNAtom_GetType(X) != HTN_ATOM_TYPE_FLOAT ||
            HTNAtom_GetType(Y) != HTN_ATOM_TYPE_FLOAT || HTNAtom_GetType(Z) != HTN_ATOM_TYPE_FLOAT)
            return false;

        outValue.X = HTNAtomGetValue<float>(*X);
        outValue.Y = HTNAtomGetValue<float>(*Y);
        outValue.Z = HTNAtomGetValue<float>(*Z);
        return true;
    }

    static bool ToAtom([[maybe_unused]] void* inClientContext, const HTNTestVector3& inValue, HTNAtom& outAtom)
    {
        HTNAtom Values[3];
        HTNAtom_InitRange(Values, 3u);
        HTNAtom_SetFloat(&Values[0], inValue.X);
        HTNAtom_SetFloat(&Values[1], inValue.Y);
        HTNAtom_SetFloat(&Values[2], inValue.Z);

        HTNAtomList List;
        HTNAtomList_Init(&List);
        const bool Success = HTNAtomList_PushBack(&List, &Values[0]) &&
                             HTNAtomList_PushBack(&List, &Values[1]) &&
                             HTNAtomList_PushBack(&List, &Values[2]) &&
                             HTNAtom_SetListCopy(&outAtom, &List);
        HTNAtomList_Destroy(&List);
        HTNAtom_DestroyRange(Values, 3u);
        return Success;
    }
};

struct HTNTestFailingCustomType {};

template<>
struct HTNTypeTraits<HTNTestFailingCustomType>
{
    static constexpr bool IsSupported      = true;
    static constexpr bool HasFixedAtomType = true;
    static constexpr HTNAtomType AtomType  = HTN_ATOM_TYPE_STRING;
    static constexpr const char* Name      = "HTNTestFailingCustomType";
};

template<>
struct HTNTypeConverter<HTNTestFailingCustomType>
{
    static bool FromAtom([[maybe_unused]] void* inClientContext, const HTNAtom&, HTNTestFailingCustomType&) { return false; }

    static bool ToAtom([[maybe_unused]] void* inClientContext, const HTNTestFailingCustomType&, HTNAtom& outAtom)
    {
        // Deliberately create owned storage before reporting failure. sCreateCall
        // must still destroy this temporary argument and leave no leak behind.
        HTNAtom_SetString(&outAtom, "custom-conversion-failed", 24u);
        return false;
    }
};

namespace
{
bool PushListValue(HTNAtomList& ioList, const HTNAtom& inValue)
{
    return HTNAtomList_PushBack(&ioList, &inValue) != 0;
}

class HTNCountingNewDeleteAtomListAllocator final : public HTNAtomListAllocator
{
public:
    HTNAtomNode* Allocate() final
    {
        ++mAllocationCount;
        auto* Node = static_cast<HTNAtomNode*>(::operator new(sizeof(HTNAtomNode)));
        Node->next_node = nullptr;
        Node->allocation_cookie = nullptr;
        return Node;
    }

    void Deallocate(HTNAtomNode* inNode) final
    {
        ++mDeallocationCount;
        HTNAtom_Destroy(&inNode->data);
        ::operator delete(inNode);
    }

    uint32 mAllocationCount   = 0;
    uint32 mDeallocationCount = 0;
};

class HTNToggleFailAtomListAllocator final : public HTNAtomListAllocator
{
public:
    HTNAtomNode* Allocate() final
    {
        if (mFailAllocations)
            return nullptr;

        auto* Node = static_cast<HTNAtomNode*>(::operator new(sizeof(HTNAtomNode)));
        Node->next_node = nullptr;
        Node->allocation_cookie = nullptr;
        return Node;
    }

    void Deallocate(HTNAtomNode* inNode) final
    {
        HTNAtom_Destroy(&inNode->data);
        ::operator delete(inNode);
    }

    bool mFailAllocations = false;
};

template<typename TAllocator>
std::int64_t MeasureListAllocator(TAllocator& ioAllocator, const uint32 inIterationCount, const uint32 inElementCount)
{
    const auto Start = std::chrono::steady_clock::now();
    std::int64_t Checksum = 0;

    for (uint32 Iteration = 0; Iteration < inIterationCount; ++Iteration)
    {
        HTNAtomListOwner List(ioAllocator);
        for (uint32 ElementIndex = 0; ElementIndex < inElementCount; ++ElementIndex)
        {
            if (!PushListValue(*List.Get(), HTNAtomOwner(static_cast<int32>(ElementIndex))))
                return -1;
        }

        Checksum += HTNAtomGetValue<int32>(*(HTNAtomList_Get(List.Get(), inElementCount - 1)));
    }

    if (Checksum <= 0)
        return -1;

    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - Start).count();
}
} // namespace



TEST(HTNAtomOwnerTest, SupportsOwningStdContainersWithoutChangingTheCAtomType)
{
    const HTNAtomDebugStats Before = HTNAtomDebug_GetStats();

    {
        const std::string HeapString(64u, 'x');

        std::vector<HTNAtomOwner> DynamicAtoms;
        DynamicAtoms.reserve(3u);
        DynamicAtoms.emplace_back(int32_t{7});
        DynamicAtoms.emplace_back(HeapString);
        DynamicAtoms.emplace_back(HtnSymbol::sGetSymbol("patrol"));

        ASSERT_EQ(3u, DynamicAtoms.size());
        EXPECT_EQ(7, HTNAtomGetValue<int32>(DynamicAtoms[0]));
        EXPECT_EQ(HeapString, HTNAtomGetValue<std::string>(DynamicAtoms[1]));
        EXPECT_TRUE(HTNAtomIsType<const HtnSymbol*>(DynamicAtoms[2]));

        std::array<HTNAtomOwner, 2u> FixedAtoms;
        FixedAtoms[0] = DynamicAtoms[1];
        FixedAtoms[1] = std::move(DynamicAtoms[0]);

        EXPECT_EQ(HeapString, HTNAtomGetValue<std::string>(FixedAtoms[0]));
        EXPECT_EQ(7, HTNAtomGetValue<int32>(FixedAtoms[1]));

        HTNAtomListOwner List({HTNAtomOwner(1), HTNAtomOwner(2), HTNAtomOwner(3)});
        HTNAtomOwner ListOwner(List);
        EXPECT_EQ(3, HTNAtomGetListSize(ListOwner));
    }

#ifdef HTN_DEBUG
    const HTNAtomDebugStats After = HTNAtomDebug_GetStats();
    EXPECT_EQ(Before.live_heap_strings, After.live_heap_strings);
    EXPECT_EQ(Before.live_heap_string_bytes, After.live_heap_string_bytes);
    EXPECT_EQ(Before.live_list_nodes, After.live_list_nodes);
#else
#ifdef HTN_PROFILE
    const HTNAtomDebugStats After = HTNAtomDebug_GetStats();
    EXPECT_EQ(Before.live_heap_strings, After.live_heap_strings);
    EXPECT_EQ(Before.live_heap_string_bytes, After.live_heap_string_bytes);
    EXPECT_EQ(Before.live_list_nodes, After.live_list_nodes);
#endif
#endif
}

TEST(HTNAtomCApiTest, OperationalApiMatchesCppCompatibilityLayer)
{
    HTNAtom StringAtom;
    HTNAtom_Init(&StringAtom);
    ASSERT_TRUE(HTNAtom_SetString(&StringAtom, "abcdefghijklmnop", 16u));
    EXPECT_EQ(HTN_ATOM_TYPE_STRING, HTNAtom_GetType(&StringAtom));
    EXPECT_TRUE(HTNAtom_IsBound(&StringAtom));
    EXPECT_EQ(16u, HTNAtom_GetStringSize(&StringAtom));
    ASSERT_NE(nullptr, HTNAtom_GetStringData(&StringAtom));
    EXPECT_EQ(std::string("abcdefghijklmnop"), std::string(HTNAtom_GetStringData(&StringAtom), HTNAtom_GetStringSize(&StringAtom)));
    EXPECT_EQ(HTNAtomGetType(StringAtom), HTNAtom_GetType(&StringAtom));
    EXPECT_EQ(HTNAtomIsBound(StringAtom), HTNAtom_IsBound(&StringAtom) != 0);

    HTNAtom ListAtom;
    HTNAtom_Init(&ListAtom);
    HTNAtomOwner First(int32_t{10});
    HTNAtomOwner Second(int32_t{20});
    ASSERT_TRUE(HTNAtom_PushBackListElement(&ListAtom, First.Get()));
    ASSERT_TRUE(HTNAtom_PushBackListElement(&ListAtom, Second.Get()));
    EXPECT_EQ(2, HTNAtom_GetListSize(&ListAtom));
    EXPECT_FALSE(HTNAtom_IsListEmpty(&ListAtom));
    ASSERT_NE(nullptr, HTNAtom_GetListElement(&ListAtom, 1u));
    EXPECT_EQ(20, HTNAtomGetValue<int32>(*(HTNAtom_GetListElement(&ListAtom, 1u))));
    EXPECT_EQ(nullptr, HTNAtom_GetListElement(&ListAtom, 2u));
    EXPECT_EQ(HTNAtomGetListSize(ListAtom), HTNAtom_GetListSize(&ListAtom));

    HTNAtomOwner Copy(ListAtom);
    EXPECT_TRUE(HTNAtom_Equals(&ListAtom, Copy.Get()));
    EXPECT_TRUE(ListAtom == Copy);

    HTNAtom_Unbind(&ListAtom);
    EXPECT_EQ(HTN_ATOM_TYPE_UNBOUND, HTNAtom_GetType(&ListAtom));
    EXPECT_FALSE(HTNAtom_IsBound(&ListAtom));
    EXPECT_EQ(-1, HTNAtom_GetListSize(&ListAtom));
    EXPECT_TRUE(HTNAtom_IsListEmpty(&ListAtom));
    HTNAtom_Destroy(&ListAtom);
    HTNAtom_Destroy(&StringAtom);
}

TEST(HtnSymbolTest, InternsByIdentityAndKeepsStableMetadata)
{
    const HtnSymbol* PatrolA = HtnSymbol::sGetSymbol("patrol");
    const HtnSymbol* PatrolB = HtnSymbol::sGetSymbol(std::string("patrol"));
    const HtnSymbol* Attack = HtnSymbol::sGetSymbol("attack");

    ASSERT_NE(nullptr, PatrolA);
    EXPECT_EQ(PatrolA, PatrolB);
    EXPECT_NE(PatrolA, Attack);
    EXPECT_EQ(PatrolA->GetHash(), PatrolB->GetHash());
    EXPECT_EQ(PatrolA->GetID(), PatrolB->GetID());
    EXPECT_NE(PatrolA->GetID(), Attack->GetID());
}

TEST(HTNAtomTypeTest, ReportsSemanticTypeAndUnboundState)
{
    const HtnSymbol* Patrol = HtnSymbol::sGetSymbol("patrol");

    EXPECT_EQ(HTNAtomOwner().GetType(), HTNAtomType::HTN_ATOM_TYPE_UNBOUND);
    EXPECT_EQ(HTNAtomOwner(true).GetType(), HTNAtomType::HTN_ATOM_TYPE_BOOL);
    EXPECT_EQ(HTNAtomOwner(int32(7)).GetType(), HTNAtomType::HTN_ATOM_TYPE_INT);
    EXPECT_EQ(HTNAtomOwner(1.0f).GetType(), HTNAtomType::HTN_ATOM_TYPE_FLOAT);
    EXPECT_EQ(HTNAtomOwner(std::string("text")).GetType(), HTNAtomType::HTN_ATOM_TYPE_STRING);
    EXPECT_EQ(HTNAtomOwner(Patrol).GetType(), HTNAtomType::HTN_ATOM_TYPE_SYMBOL);
    EXPECT_EQ(HTNAtomOwner(HTNAtomListOwner()).GetType(), HTNAtomType::HTN_ATOM_TYPE_LIST);
}

TEST(HTNAtomSymbolTest, DistinguishesSymbolsFromStringsAndPrintsSymbolsWithoutQuotes)
{
    const HtnSymbol* Patrol = HtnSymbol::sGetSymbol("patrol");
    const HTNAtomOwner SymbolAtom(Patrol);
    const HTNAtomOwner StringAtom(std::string("patrol"));

    EXPECT_TRUE(SymbolAtom.IsType<const HtnSymbol*>());
    EXPECT_EQ(Patrol, SymbolAtom.GetValue<const HtnSymbol*>());
    EXPECT_NE(SymbolAtom, StringAtom);
    EXPECT_EQ("patrol", SymbolAtom.ToString(true));

    const HTNAtomOwner List(HTNAtomListOwner({SymbolAtom, StringAtom}));
    EXPECT_EQ("(patrol \"patrol\")", List.ToString(true));
}

TEST(HTNAtomListTest, PooledAllocatorReusesFixedCapacityWithoutGrowing)
{
    HTNPooledAtomListAllocator Allocator(3);

    {
        HTNAtomListOwner List(Allocator);
        EXPECT_TRUE(PushListValue(*List.Get(), HTNAtomOwner(1)));
        EXPECT_TRUE(PushListValue(*List.Get(), HTNAtomOwner(2)));
        EXPECT_TRUE(PushListValue(*List.Get(), HTNAtomOwner(3)));
        EXPECT_FALSE(PushListValue(*List.Get(), HTNAtomOwner(4)));
        EXPECT_EQ(3u, HTNAtomList_GetSize(List.Get()));
        EXPECT_EQ(0u, Allocator.GetAvailableNodeCount());
    }

    EXPECT_EQ(3u, Allocator.GetAvailableNodeCount());
    EXPECT_EQ(0u, Allocator.GetAllocatedNodeCount());

    HTNAtomListOwner ReusedList(Allocator);
    EXPECT_TRUE(PushListValue(*ReusedList.Get(), HTNAtomOwner(5)));
    EXPECT_EQ(5, HTNAtomGetValue<int32>(*HTNAtomList_Get(ReusedList.Get(), 0)));
}

TEST(HTNAtomListTest, SupportsCustomClientAllocator)
{
    HTNCountingNewDeleteAtomListAllocator Allocator;

    {
        HTNAtomListOwner List(Allocator, {HTNAtomOwner(1), HTNAtomOwner(2), HTNAtomOwner(3)});
        EXPECT_EQ(3u, Allocator.mAllocationCount);
        EXPECT_EQ(0u, Allocator.mDeallocationCount);
        EXPECT_EQ(3u, HTNAtomList_GetSize(List.Get()));
    }

    EXPECT_EQ(3u, Allocator.mAllocationCount);
    EXPECT_EQ(3u, Allocator.mDeallocationCount);
}

TEST(HTNAtomListTest, CopiesAndMovesPooledLists)
{
    HTNPooledAtomListAllocator Allocator(8);

    HTNAtomListOwner Source(Allocator, {HTNAtomOwner(1), HTNAtomOwner(2)});
    HTNAtomListOwner Copy(Source);
    EXPECT_EQ(Source, Copy);
    EXPECT_EQ(4u, Allocator.GetAllocatedNodeCount());

    HTNAtomListOwner Moved(std::move(Copy));
    EXPECT_EQ(Source, Moved);
    EXPECT_TRUE(HTNAtomList_IsEmpty(Copy.Get()) != 0);
    EXPECT_EQ(4u, Allocator.GetAllocatedNodeCount());

    // A moved-from list remains attached to the pool selected by the client.
    EXPECT_TRUE(PushListValue(*Copy.Get(), HTNAtomOwner(3)));
    EXPECT_EQ(5u, Allocator.GetAllocatedNodeCount());
}

TEST(HTNAtomListTest, SupportsNestedListsWithOnePool)
{
    HTNPooledAtomListAllocator Allocator(16);

    HTNAtomListOwner Position(Allocator, {HTNAtomOwner(10.0f), HTNAtomOwner(20.0f), HTNAtomOwner(30.0f)});
    HTNAtomOwner PositionAtom(std::move(Position));
    HTNAtomListOwner Result(Allocator, {HTNAtomOwner(std::string("move_to")), PositionAtom});

    ASSERT_EQ(2u, HTNAtomList_GetSize(Result.Get()));
    EXPECT_EQ("move_to", HTNAtomGetValue<std::string>(*HTNAtomList_Get(Result.Get(), 0)));

    const HTNAtom& NestedPosition = *HTNAtomList_Get(Result.Get(), 1);
    ASSERT_EQ(3, HTNAtomGetListSize(NestedPosition));
    EXPECT_FLOAT_EQ(10.0f, HTNAtomGetValue<float>(HTNAtomGetListElement(NestedPosition, 0)));
    EXPECT_FLOAT_EQ(20.0f, HTNAtomGetValue<float>(HTNAtomGetListElement(NestedPosition, 1)));
    EXPECT_FLOAT_EQ(30.0f, HTNAtomGetValue<float>(HTNAtomGetListElement(NestedPosition, 2)));
}

TEST(HTNAtomListTest, AtomCanOwnListUsingClientAllocator)
{
    HTNPooledAtomListAllocator Allocator(2);
    HTNAtomListOwner List(Allocator);
    HTNAtomOwner Atom(std::move(List));

    EXPECT_TRUE(Atom.PushBackElementToList(HTNAtomOwner(10)));
    EXPECT_TRUE(Atom.PushBackElementToList(HTNAtomOwner(20)));
    EXPECT_FALSE(Atom.PushBackElementToList(HTNAtomOwner(30)));
    EXPECT_EQ(2, Atom.GetListSize());
}

TEST(HTNAtomListTest, CopyReportsAllocatorCapacityFailure)
{
    HTNAtomListOwner Source({HTNAtomOwner(1), HTNAtomOwner(2), HTNAtomOwner(3)});

    HTNPooledAtomListAllocator ExactAllocator(3);
    HTNAtomListOwner ExactClone(ExactAllocator);
    EXPECT_TRUE(HTNAtomList_Copy(ExactClone.Get(), Source.Get()));
    EXPECT_EQ(3u, HTNAtomList_GetSize(ExactClone.Get()));

    HTNPooledAtomListAllocator SmallAllocator(2);
    HTNAtomListOwner FailedClone(SmallAllocator);
    EXPECT_FALSE(HTNAtomList_Copy(FailedClone.Get(), Source.Get()));
    EXPECT_TRUE(HTNAtomList_IsEmpty(FailedClone.Get()));
}

TEST(HTNAtomListPerformanceTest, ComparePoolAgainstNewDelete)
{
    constexpr uint32 kIterationCount = 5000;
    constexpr uint32 kElementCount   = 32;

    HTNPooledAtomListAllocator   PooledAllocator(kElementCount);
    HTNNewDeleteAtomListAllocator& NewDeleteAllocator = HTNNewDeleteAtomListAllocator::Get();

    const std::int64_t PooledMicroseconds = MeasureListAllocator(PooledAllocator, kIterationCount, kElementCount);
    const std::int64_t NewDeleteMicroseconds = MeasureListAllocator(NewDeleteAllocator, kIterationCount, kElementCount);

    ASSERT_GE(PooledMicroseconds, 0);
    ASSERT_GE(NewDeleteMicroseconds, 0);
    std::cout << "[ HTNAtomList performance ] pool=" << PooledMicroseconds << "us, new/delete=" << NewDeleteMicroseconds << "us\n";
    RecordProperty("pooled_microseconds", std::to_string(PooledMicroseconds));
    RecordProperty("new_delete_microseconds", std::to_string(NewDeleteMicroseconds));
}

TEST(HTNAtomListTest, SplitFrontReturnsFirstElementAndSuffix)
{
    HTNAtomListOwner List({HTNAtomOwner(std::string("one")), HTNAtomOwner(std::string("two")), HTNAtomOwner(std::string("three"))});
    HTNAtomOwner Element;
    HTNAtomOwner Remainder;

    ASSERT_TRUE(HTNAtomList_Split(List.Get(), HTN_ATOM_LIST_SPLIT_FRONT, Element.Get(), Remainder.Get()));
    ASSERT_TRUE(Element.IsType<std::string>());
    EXPECT_EQ("one", Element.GetValue<std::string>());
    ASSERT_TRUE(Remainder.IsType<HTNAtomList>());
    EXPECT_EQ(2, Remainder.GetListSize());
    EXPECT_EQ("two", HTNAtomGetValue<std::string>(Remainder.GetListElement(0u)));
    EXPECT_EQ("three", HTNAtomGetValue<std::string>(Remainder.GetListElement(1u)));
}

TEST(HTNAtomListTest, SplitSingleElementReturnsEmptyListRemainder)
{
    HTNAtomListOwner List({HTNAtomOwner(std::string("only"))});
    HTNAtomOwner Element;
    HTNAtomOwner Remainder;

    ASSERT_TRUE(HTNAtomList_Split(List.Get(), HTN_ATOM_LIST_SPLIT_FRONT, Element.Get(), Remainder.Get()));
    EXPECT_EQ("only", Element.GetValue<std::string>());
    ASSERT_TRUE(Remainder.IsType<HTNAtomList>());
    EXPECT_EQ(0, Remainder.GetListSize());
}

TEST(HTNAtomListTest, SplitBackReturnsLastElementAndPrefix)
{
    HTNAtomListOwner List({HTNAtomOwner(std::string("one")), HTNAtomOwner(std::string("two")), HTNAtomOwner(std::string("three"))});
    HTNAtomOwner Element;
    HTNAtomOwner Remainder;

    ASSERT_TRUE(HTNAtomList_Split(List.Get(), HTN_ATOM_LIST_SPLIT_BACK, Element.Get(), Remainder.Get()));
    ASSERT_TRUE(Element.IsType<std::string>());
    EXPECT_EQ("three", Element.GetValue<std::string>());
    ASSERT_TRUE(Remainder.IsType<HTNAtomList>());
    EXPECT_EQ(2, Remainder.GetListSize());
    EXPECT_EQ("one", HTNAtomGetValue<std::string>(Remainder.GetListElement(0u)));
    EXPECT_EQ("two", HTNAtomGetValue<std::string>(Remainder.GetListElement(1u)));
}

TEST(HTNAtomListTest, SplitRejectsEmptyListWithoutChangingOutputs)
{
    HTNAtomListOwner List;
    HTNAtomOwner Element(std::string("unchanged"));
    HTNAtomOwner Remainder(int32_t{7});

    EXPECT_FALSE(HTNAtomList_Split(List.Get(), HTN_ATOM_LIST_SPLIT_FRONT, Element.Get(), Remainder.Get()));
    EXPECT_EQ("unchanged", Element.GetValue<std::string>());
    EXPECT_EQ(7, Remainder.GetValue<int32>());
}


TEST(HTNAtomListTest, PushBackMoveTransfersOwnershipWithoutCopyingNestedList)
{
    HTNAtomListOwner SourceList({HTNAtomOwner(1), HTNAtomOwner(2)});
    HTNAtom Source;
    HTNAtom_Init(&Source);
    HTNAtom_SetListMove(&Source, SourceList.Get());

    HTNAtomList Destination;
    HTNAtomList_Init(&Destination);
    ASSERT_TRUE(HTNAtomList_PushBackMove(&Destination, &Source));
    EXPECT_EQ(HTN_ATOM_TYPE_UNBOUND, HTNAtom_GetType(&Source));

    const HTNAtom* Moved = HTNAtomList_Get(&Destination, 0u);
    ASSERT_NE(nullptr, Moved);
    ASSERT_EQ(2, HTNAtom_GetListSize(Moved));
    EXPECT_EQ(1, HTNAtomGetValue<int32>(*HTNAtom_GetListElement(Moved, 0u)));
    EXPECT_EQ(2, HTNAtomGetValue<int32>(*HTNAtom_GetListElement(Moved, 1u)));

    HTNAtom_Destroy(&Source);
    HTNAtomList_Destroy(&Destination);
}

TEST(HTNAtomListTest, PopFrontMoveTransfersOwnershipAndRemovesNode)
{
    HTNAtomListOwner List({HTNAtomOwner(std::string("first")), HTNAtomOwner(std::string("second"))});

    HTNAtom Value;
    ASSERT_TRUE(HTNAtomList_PopFrontMove(List.Get(), &Value));
    EXPECT_EQ("first", HTNAtomGetValue<std::string>(Value));
    EXPECT_EQ(1u, HTNAtomList_GetSize(List.Get()));
    EXPECT_EQ("second", HTNAtomGetValue<std::string>(*HTNAtomList_Get(List.Get(), 0u)));
    HTNAtom_Destroy(&Value);
}

TEST(HTNAtomCallTest, CApiCreatesOwningCallAndCopiesArguments)
{
    const HtnSymbol* Head = HtnSymbol::sGetSymbol("get_priority_for_role");
    ASSERT_NE(nullptr, Head);

    HTNAtom Arguments[3];
    HTNAtom_InitRange(Arguments, 3u);
    HTNAtom_SetInt(&Arguments[0], 42);
    HTNAtom_SetSymbol(&Arguments[1], HtnSymbol::sGetSymbol("defender"));
    ASSERT_TRUE(HTNAtom_SetString(&Arguments[2], "context", 7u));

    HTNAtom Call;
    ASSERT_TRUE(HTNAtom_CreateCall(&Call, Head, Arguments, 3u));
    ASSERT_EQ(4, HTNAtom_GetListSize(&Call));
    EXPECT_EQ(Head, HTNAtomGetValue<const HtnSymbol*>(*HTNAtom_GetListElement(&Call, 0u)));
    EXPECT_EQ(42, HTNAtomGetValue<int32>(*HTNAtom_GetListElement(&Call, 1u)));
    EXPECT_EQ(HtnSymbol::sGetSymbol("defender"), HTNAtomGetValue<const HtnSymbol*>(*HTNAtom_GetListElement(&Call, 2u)));
    EXPECT_EQ("context", HTNAtomGetValue<std::string>(*HTNAtom_GetListElement(&Call, 3u)));

    // The call owns independent copies of its arguments.
    HTNAtom_SetInt(&Arguments[0], 7);
    ASSERT_TRUE(HTNAtom_SetString(&Arguments[2], "changed", 7u));
    EXPECT_EQ(42, HTNAtomGetValue<int32>(*HTNAtom_GetListElement(&Call, 1u)));
    EXPECT_EQ("context", HTNAtomGetValue<std::string>(*HTNAtom_GetListElement(&Call, 3u)));

    HTNAtom_DestroyRange(Arguments, 3u);
    HTNAtom_Destroy(&Call);
}


TEST(HTNAtomCopyTest, AllocationFailureIsReportedAndLeavesDestinationDestroyable)
{
    const HTNAtomDebugStats Before = HTNAtomDebug_GetStats();

    {
        HTNPooledAtomListAllocator Allocator(1u);

        HTNAtomOwner Element(int32{7});
        HTNAtomListOwner SourceList(Allocator);
        ASSERT_TRUE(SourceList.PushBack(*Element.Get()));
        HTNAtomOwner Source(std::move(SourceList));

        HTNAtom Copy;
        EXPECT_FALSE(HTNAtom_Copy(&Copy, Source.Get()));
        EXPECT_EQ(HTN_ATOM_TYPE_UNBOUND, HTNAtom_GetType(&Copy));
        HTNAtom_Destroy(&Copy);

        HTNAtom Destination;
        HTNAtom_Init(&Destination);
        ASSERT_TRUE(HTNAtom_SetString(&Destination, "owned-before-failed-copy", 24u));
        EXPECT_FALSE(HTNAtom_AssignCopy(&Destination, Source.Get()));
        EXPECT_EQ(HTN_ATOM_TYPE_UNBOUND, HTNAtom_GetType(&Destination));
        HTNAtom_Destroy(&Destination);

    }

#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    const HTNAtomDebugStats After = HTNAtomDebug_GetStats();
    EXPECT_EQ(Before.live_heap_strings, After.live_heap_strings);
    EXPECT_EQ(Before.live_heap_string_bytes, After.live_heap_string_bytes);
    EXPECT_EQ(Before.live_list_nodes, After.live_list_nodes);
#endif
}

TEST(HTNAtomCopyTest, CopyRangeFailureDestroysEveryInitializedOutput)
{
    HTNToggleFailAtomListAllocator Allocator;
    HTNAtomListOwner SourceList(Allocator);
    HTNAtomOwner Element(int32{7});
    ASSERT_TRUE(SourceList.PushBack(*Element.Get()));

    HTNAtom Source[2];
    HTNAtom_InitRange(Source, 2u);
    HTNAtom_SetInt(&Source[0], 1);
    ASSERT_TRUE(HTNAtom_SetListCopy(&Source[1], SourceList.Get()));

    const HTNAtomDebugStats BeforeCopy = HTNAtomDebug_GetStats();
    Allocator.mFailAllocations = true;

    HTNAtom Copy[2];
    EXPECT_FALSE(HTNAtom_CopyRange(Copy, Source, 2u));

#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    const HTNAtomDebugStats AfterCopy = HTNAtomDebug_GetStats();
    EXPECT_EQ(BeforeCopy.live_heap_strings, AfterCopy.live_heap_strings);
    EXPECT_EQ(BeforeCopy.live_heap_string_bytes, AfterCopy.live_heap_string_bytes);
    EXPECT_EQ(BeforeCopy.live_list_nodes, AfterCopy.live_list_nodes);
#endif

    HTNAtom_DestroyRange(Source, 2u);
}

TEST(HTNAtomCallTest, CppCreateCallInfersTypesAndUsesExplicitDestroyOwnership)
{
    const HTNAtomDebugStats Before = HTNAtomDebug_GetStats();

    enum class RoleContext : int32_t { Defensive = 3 };
    const HtnSymbol* Head = HtnSymbol::sGetSymbol("get_priority_for_role");
    const HtnSymbol* Role = HtnSymbol::sGetSymbol("defender");

    HTNAtom ExistingArgument;
    HTNAtom_Init(&ExistingArgument);
    ASSERT_TRUE(HTNAtom_SetString(&ExistingArgument, "candidate", 9u));

    HTNAtom Call = HTNAtom::sCreateCall(
        Head,
        ExistingArgument,
        int32_t{27},
        Role,
        RoleContext::Defensive,
        1.5f,
        true,
        std::string("role_context"));

    ASSERT_EQ(8, HTNAtom_GetListSize(&Call));
    EXPECT_EQ(Head, HTNAtomGetValue<const HtnSymbol*>(*HTNAtom_GetListElement(&Call, 0u)));
    EXPECT_EQ("candidate", HTNAtomGetValue<std::string>(*HTNAtom_GetListElement(&Call, 1u)));
    EXPECT_EQ(27, HTNAtomGetValue<int32>(*HTNAtom_GetListElement(&Call, 2u)));
    EXPECT_EQ(Role, HTNAtomGetValue<const HtnSymbol*>(*HTNAtom_GetListElement(&Call, 3u)));
    EXPECT_EQ(3, HTNAtomGetValue<int32>(*HTNAtom_GetListElement(&Call, 4u)));
    EXPECT_FLOAT_EQ(1.5f, HTNAtomGetValue<float>(*HTNAtom_GetListElement(&Call, 5u)));
    EXPECT_TRUE(HTNAtomGetValue<bool>(*HTNAtom_GetListElement(&Call, 6u)));
    EXPECT_EQ("role_context", HTNAtomGetValue<std::string>(*HTNAtom_GetListElement(&Call, 7u)));

    // Destroying/changing the original argument does not affect the call.
    HTNAtom_Destroy(&ExistingArgument);
    EXPECT_EQ("candidate", HTNAtomGetValue<std::string>(*HTNAtom_GetListElement(&Call, 1u)));

    HTNAtom::sDestroy(Call);

#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    const HTNAtomDebugStats After = HTNAtomDebug_GetStats();
    EXPECT_EQ(Before.live_heap_strings, After.live_heap_strings);
    EXPECT_EQ(Before.live_heap_string_bytes, After.live_heap_string_bytes);
    EXPECT_EQ(Before.live_list_nodes, After.live_list_nodes);
#endif
}

TEST(HTNAtomCallTest, FailedCreateLeavesDestroyableUnboundAtom)
{
    HTNAtom InvalidArgument;
    HTNAtom_Init(&InvalidArgument);

    HTNAtom Call;
    EXPECT_FALSE(HTNAtom_CreateCall(&Call, HtnSymbol::sGetSymbol("invalid"), &InvalidArgument, 1u));
    EXPECT_EQ(HTN_ATOM_TYPE_UNBOUND, HTNAtom_GetType(&Call));

    HTNAtom_Destroy(&Call);
    HTNAtom_Destroy(&InvalidArgument);
}


TEST(HTNAtomCallTest, CppCreateCallUsesRegisteredCustomTypeConversion)
{
    const HTNAtomDebugStats Before = HTNAtomDebug_GetStats();
    const HtnSymbol* Head = HtnSymbol::sGetSymbol("move_to");

    HTNAtom Call = HTNAtom::sCreateCall(Head, HTNTestVector3{1.0f, 2.0f, 3.0f});

    ASSERT_EQ(2, HTNAtom_GetListSize(&Call));
    const HTNAtom* VectorAtom = HTNAtom_GetListElement(&Call, 1u);
    ASSERT_NE(nullptr, VectorAtom);
    ASSERT_EQ(HTN_ATOM_TYPE_LIST, HTNAtom_GetType(VectorAtom));
    ASSERT_EQ(3, HTNAtom_GetListSize(VectorAtom));
    EXPECT_FLOAT_EQ(1.0f, HTNAtomGetValue<float>(*HTNAtom_GetListElement(VectorAtom, 0u)));
    EXPECT_FLOAT_EQ(2.0f, HTNAtomGetValue<float>(*HTNAtom_GetListElement(VectorAtom, 1u)));
    EXPECT_FLOAT_EQ(3.0f, HTNAtomGetValue<float>(*HTNAtom_GetListElement(VectorAtom, 2u)));

    HTNAtom::sDestroy(Call);

#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    const HTNAtomDebugStats After = HTNAtomDebug_GetStats();
    EXPECT_EQ(Before.live_heap_strings, After.live_heap_strings);
    EXPECT_EQ(Before.live_heap_string_bytes, After.live_heap_string_bytes);
    EXPECT_EQ(Before.live_list_nodes, After.live_list_nodes);
#endif
}

TEST(HTNAtomCallTest, FailedCustomTypeConversionCleansTemporaryAtomAndReturnsDestroyableCall)
{
    const HTNAtomDebugStats Before = HTNAtomDebug_GetStats();
    const HtnSymbol* Head = HtnSymbol::sGetSymbol("custom_failure");

    HTNAtom Call = HTNAtom::sCreateCall(Head, HTNTestFailingCustomType{});
    EXPECT_EQ(HTN_ATOM_TYPE_UNBOUND, HTNAtom_GetType(&Call));
    HTNAtom::sDestroy(Call);

#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
    const HTNAtomDebugStats After = HTNAtomDebug_GetStats();
    EXPECT_EQ(Before.live_heap_strings, After.live_heap_strings);
    EXPECT_EQ(Before.live_heap_string_bytes, After.live_heap_string_bytes);
    EXPECT_EQ(Before.live_list_nodes, After.live_list_nodes);
#endif
}
