// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "Core/HTNTypeConversion.h"

#include "gtest/gtest.h"

namespace
{
struct TestCell
{
    int32 X = 0;
    int32 Y = 0;

    bool operator==(const TestCell& inOther) const
    {
        return X == inOther.X && Y == inOther.Y;
    }
};
}

// This intentionally lives outside HTNFramework. It demonstrates the intended
// extension point for game/engine-native types: the framework never needs to
// include the header that declares TestCell.
template<>
struct HTNTypeTraits<TestCell>
{
    static constexpr bool IsSupported      = true;
    static constexpr bool HasFixedAtomType = true;
    static constexpr HTNAtomType AtomType  = HTNAtomType::HTN_ATOM_TYPE_LIST;
    static constexpr const char* Name      = "TestCell";
};

template<>
struct HTNTypeConverter<TestCell>
{
    static bool FromAtom(const HTNAtom& inAtom, TestCell& outValue)
    {
        HTNAtomListOwner List;
        if (!HTNTryParseType(inAtom, List) || HTNAtomList_GetSize(List.Get()) != 2u)
            return false;

        return HTNTryParseType(*HTNAtomList_Get(List.Get(), 0u), outValue.X) &&
               HTNTryParseType(*HTNAtomList_Get(List.Get(), 1u), outValue.Y);
    }

    static bool ToAtom(const TestCell& inValue, HTNAtom& outAtom)
    {
        HTNAtomOwner X;
        HTNAtomOwner Y;
        if (!HTNTryToAtom(inValue.X, *X.Get()) || !HTNTryToAtom(inValue.Y, *Y.Get()))
            return false;

        const HTNAtomOwner Result(HTNAtomListOwner({X, Y}));
        return HTNAtom_AssignCopy(&outAtom, Result.Get()) != 0;
    }
};

TEST(HTNTypeConversionTest, ConvertsNativeAtomTypesBothWays)
{
    int32 ParsedInt = 0;
    EXPECT_TRUE(HTNTryParseType(HTNAtomOwner(int32(42)), ParsedInt));
    EXPECT_EQ(42, ParsedInt);
    EXPECT_FALSE(HTNTryParseType(HTNAtomOwner(42.0f), ParsedInt));

    HTNAtomOwner Atom;
    EXPECT_TRUE(HTNTryToAtom(int32(17), *Atom.Get()));
    ASSERT_TRUE(Atom.IsType<int32>());
    EXPECT_EQ(17, Atom.GetValue<int32>());
}

TEST(HTNTypeConversionTest, ExposesRepresentationMetadata)
{
    static_assert(HTNIsTypeConvertible<int32>);
    static_assert(HTNIsTypeConvertible<const HTNAtomList&>);
    static_assert(HTNIsTypeConvertible<TestCell>);

    EXPECT_EQ(HTNAtomType::HTN_ATOM_TYPE_INT, HTNGetExpectedAtomType<int32>());
    EXPECT_EQ(HTNAtomType::HTN_ATOM_TYPE_LIST, HTNGetExpectedAtomType<TestCell>());
    EXPECT_EQ(std::nullopt, HTNGetExpectedAtomType<HTNAtom>());
    EXPECT_STREQ("TestCell", HTNTypeTraits<TestCell>::Name);
}

TEST(HTNTypeConversionTest, ConvertsExternalCompositeTypeRecursively)
{
    const HTNAtomOwner CellAtom(HTNAtomListOwner({HTNAtomOwner(int32(3)), HTNAtomOwner(int32(6))}));

    TestCell Cell;
    ASSERT_TRUE(HTNTryParseType(CellAtom, Cell));
    EXPECT_EQ(3, Cell.X);
    EXPECT_EQ(6, Cell.Y);

    HTNAtomOwner RoundTripAtom;
    ASSERT_TRUE(HTNTryToAtom(Cell, *RoundTripAtom.Get()));
    EXPECT_EQ(CellAtom, RoundTripAtom);
}

TEST(HTNTypeConversionTest, RejectsMalformedExternalCompositeType)
{
    TestCell Cell;

    EXPECT_FALSE(HTNTryParseType(HTNAtomOwner(int32(3)), Cell));
    EXPECT_FALSE(HTNTryParseType(
        HTNAtomOwner(HTNAtomListOwner({HTNAtomOwner(int32(3))})), Cell));
    EXPECT_FALSE(HTNTryParseType(
        HTNAtomOwner(HTNAtomListOwner({HTNAtomOwner(int32(3)), HTNAtomOwner(std::string("six"))})), Cell));
}
