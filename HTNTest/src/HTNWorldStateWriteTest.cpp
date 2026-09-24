// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "WorldState/HTNWorldState.h"
#include "gtest/gtest.h"

namespace
{
struct WriteValue { int32 Value; bool Fail = false; bool NeedsContext = false; };
struct WriteServices { int32 Offset; };
}

template<> struct HTNTypeTraits<WriteValue> : HTNTypeTraits<int32> {};
template<> struct HTNTypeConverter<WriteValue>
{
    static bool ToAtom(void* inContext, const WriteValue& inValue, HTNAtom& outAtom)
    {
        if (inValue.Fail)
        {
            // A converter may allocate before detecting failure. The caller owns cleanup.
            HTNTryToAtom(nullptr, std::string("owned conversion temporary requiring cleanup"), outAtom);
            return false;
        }
        if (inValue.NeedsContext && !inContext) return false;
        const int32 Offset = inContext ? static_cast<WriteServices*>(inContext)->Offset : 0;
        return HTNTryToAtom(inContext, inValue.Value + Offset, outAtom);
    }
};

class HTNWorldStateWriteTest : public testing::Test
{
protected:
    HTNFactRegistry Registry;
    HTNWorldState World;
    const HtnSymbol* Fact = HtnSymbol::sGetSymbol("write_test");
    void SetUp() override { Registry.Register(Fact); World.SetFactRegistry(&Registry); }
    template<typename... T> void ExpectRow(T&&... inValues)
    {
        std::array<HTNAtomOwner, sizeof...(T)> Expected{HTNAtomOwner(std::forward<T>(inValues))...};
        EXPECT_EQ(World.Query("write_test", Expected), 1u);
    }
};

TEST_F(HTNWorldStateWriteTest, NativeTypesAndLegacyTextCalls)
{
    const auto* Symbol = HtnSymbol::sGetSymbol("value");
    HTNAtomOwner Atom(int32{42});
    HTNAtomListOwner List{HTNAtomOwner(int32{9})};
    EXPECT_TRUE(World.WriteFact(Fact, true, int32{7}, 2.5f, std::string("text"), Symbol));
    ExpectRow(true, int32{7}, 2.5f, std::string("text"), Symbol);
    EXPECT_TRUE(World.WriteFact(Fact, *Atom.Get(), Atom, *List.Get(), List));
    ExpectRow(Atom, Atom, *List.Get(), List);
    char Mutable[] = "mutable";
    char* Pointer = Mutable;
    const char* Null = nullptr;
    EXPECT_TRUE(World.WriteFact(Fact, "literal", Mutable, Pointer, Null));
    ExpectRow("literal", "mutable", "mutable", "");
    EXPECT_TRUE(World.WriteFact(Fact, std::move(Atom)));
    ExpectRow(int32{42});
    EXPECT_TRUE(World.WriteFact(Fact));
    EXPECT_TRUE(World.WriteFactWithContext(nullptr, Fact));
    EXPECT_EQ(World.GetFactArgumentsCollectionSize("write_test", 0u), 2u);
}

TEST_F(HTNWorldStateWriteTest, CustomValueAndPerCallContext)
{
    EXPECT_TRUE(World.WriteFact(Fact, WriteValue{17}));
    ExpectRow(int32{17});
    WriteServices First{10}, Second{20};
    EXPECT_TRUE(World.WriteFactWithContext(&First, Fact, WriteValue{2, false, true}));
    EXPECT_TRUE(World.WriteFactWithContext(&Second, Fact, WriteValue{2, false, true}));
    ExpectRow(int32{12}); ExpectRow(int32{22});
    EXPECT_FALSE(World.WriteFact(Fact, WriteValue{2, false, true}));
    EXPECT_TRUE(World.WriteFact(Fact, WriteValue{3}));
    ExpectRow(int32{3});
    EXPECT_EQ(World.GetFactArgumentsCollectionSize("write_test", 1u), 4u);
}

TEST_F(HTNWorldStateWriteTest, FailureDoesNotCreateFactStorage)
{
    const auto Generation = World.GetFactStorageGeneration();
    EXPECT_FALSE(World.WriteFact(Fact, WriteValue{1, true}));
    EXPECT_TRUE(World.GetFacts().empty());
    EXPECT_EQ(World.GetFactStorageGeneration(), Generation);
    HTNAtomOwner Unbound;
    EXPECT_FALSE(World.WriteFact(Fact, Unbound));
    EXPECT_TRUE(World.GetFacts().empty());
}

TEST_F(HTNWorldStateWriteTest, MiddleFailurePreservesExistingRowsAndOtherArities)
{
    ASSERT_TRUE(World.WriteFact(Fact, 7));
    ASSERT_TRUE(World.WriteFact(Fact, "original", 8, 9));
    const auto Generation = World.GetFactStorageGeneration();
    EXPECT_FALSE(World.WriteFact(Fact, std::string("first allocated argument"), WriteValue{2, true}, 3));
    EXPECT_FALSE(World.WriteFact(Fact, 1, WriteValue{2, true}));
    EXPECT_EQ(World.GetFactArgumentsCollectionSize("write_test", 1u), 1u);
    EXPECT_EQ(World.GetFactArgumentsCollectionSize("write_test", 2u), 0u);
    EXPECT_EQ(World.GetFactArgumentsCollectionSize("write_test", 3u), 1u);
    EXPECT_EQ(World.GetFactStorageGeneration(), Generation);
    ExpectRow(7); ExpectRow("original", 8, 9);
    EXPECT_TRUE(World.WriteFact(Fact, 1, WriteValue{2}, 3));
    ExpectRow(1, 2, 3);
}

TEST_F(HTNWorldStateWriteTest, UnknownAndNullFactRemainRejected)
{
    EXPECT_FALSE(World.WriteFact(nullptr, 1));
    EXPECT_FALSE(World.WriteFact(HtnSymbol::sGetSymbol("not_registered"), 1));
    EXPECT_TRUE(World.GetFacts().empty());
}

TEST_F(HTNWorldStateWriteTest, CopiesNativeRvaluesAndPreservesInputsOnFailure)
{
    const std::string Text = "owned string copied into a fact";
    HTNAtomOwner Owned(Text);
    ASSERT_TRUE(World.WriteFact(Fact, Owned));
    EXPECT_EQ(Owned.GetValue<std::string>(), Text);
    ASSERT_TRUE(World.WriteFact(Fact, std::move(Owned)));
    EXPECT_EQ(Owned.GetValue<std::string>(), Text);
    HTNAtom Raw;
    HTNAtom_Init(&Raw);
    ASSERT_TRUE(HTNTryToAtom(Text, Raw));
    EXPECT_TRUE(World.WriteFact(Fact, std::move(Raw)));
    EXPECT_EQ(HTNAtomGetValue<std::string>(Raw), Text);
    HTNAtom_Destroy(&Raw);
    HTNAtomListOwner List{HTNAtomOwner(9)};
    ASSERT_TRUE(World.WriteFact(Fact, std::move(List)));
    EXPECT_EQ(HTNAtomList_GetSize(List.Get()), 1u);
    EXPECT_EQ(HTNAtomGetValue<int32>(*HTNAtomList_Get(List.Get(), 0u)), 9);
    HTNAtomListOwner RawList{HTNAtomOwner(10)};
    ASSERT_TRUE(World.WriteFact(Fact, std::move(*RawList.Get())));
    EXPECT_EQ(HTNAtomList_GetSize(RawList.Get()), 1u);
    EXPECT_EQ(HTNAtomGetValue<int32>(*HTNAtomList_Get(RawList.Get(), 0u)), 10);
    const auto Before = HTNAtomDebug_GetStats();
    EXPECT_FALSE(World.WriteFact(Fact, std::move(Owned), WriteValue{1, true}));
    EXPECT_EQ(Owned.GetValue<std::string>(), Text);
    EXPECT_EQ(World.GetFactArgumentsCollectionSize("write_test", 2u), 0u);
    const auto After = HTNAtomDebug_GetStats();
    EXPECT_EQ(Before.live_heap_strings, After.live_heap_strings);
    EXPECT_EQ(Before.live_list_nodes, After.live_list_nodes);
    // Later mutations of the source do not change the stored copies.
    Owned = HTNAtomOwner(99);
    ExpectRow(Text);
}
