// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "Core/HTNTypeConversion.h"
#include "Core/HTNCallTermBinding.h"
#include "Core/HTNTask.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "Core/HTNFileHelpers.h"
#include "Translator/HTNGeneratedPlanner.h"
#include <new>

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
    static bool FromAtom([[maybe_unused]] void* inClientContext, const HTNAtom& inAtom, TestCell& outValue)
    {
        HTNAtomListOwner List;
        if (!HTNTryParseType(inClientContext, inAtom, List) || HTNAtomList_GetSize(List.Get()) != 2u)
            return false;

        return HTNTryParseType(inClientContext, *HTNAtomList_Get(List.Get(), 0u), outValue.X) &&
               HTNTryParseType(inClientContext, *HTNAtomList_Get(List.Get(), 1u), outValue.Y);
    }

    static bool ToAtom([[maybe_unused]] void* inClientContext, const TestCell& inValue, HTNAtom& outAtom)
    {
        HTNAtomOwner X;
        HTNAtomOwner Y;
        if (!HTNTryToAtom(inClientContext, inValue.X, *X.Get()) || !HTNTryToAtom(inClientContext, inValue.Y, *Y.Get()))
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

namespace
{
struct TestEntity { int32 ID = 7; };
struct TestEntityRef { TestEntity* Entity = nullptr; };
struct TestEntityManager
{
    TestEntity Entity;
    bool Alive = true;
    int Reads = 0;
    int Writes = 0;
};
}

template<>
struct HTNTypeTraits<TestEntityRef>
{
    static constexpr bool IsSupported = true;
    static constexpr bool HasFixedAtomType = true;
    static constexpr HTNAtomType AtomType = HTN_ATOM_TYPE_INT;
    static constexpr const char* Name = "TestEntityRef";
};

template<>
struct HTNTypeConverter<TestEntityRef>
{
    static bool FromAtom(void* inClientContext, const HTNAtom& inAtom, TestEntityRef& outValue)
    {
        auto* Manager = static_cast<TestEntityManager*>(inClientContext);
        int32 ID = 0;
        if (!Manager || !HTNTryParseType(inClientContext, inAtom, ID))
            return false;
        ++Manager->Reads;
        if (!Manager->Alive || Manager->Entity.ID != ID)
            return false;
        outValue.Entity = &Manager->Entity;
        return true;
    }

    static bool ToAtom(void* inClientContext, const TestEntityRef& inValue, HTNAtom& outAtom)
    {
        auto* Manager = static_cast<TestEntityManager*>(inClientContext);
        if (!Manager)
            return false;
        ++Manager->Writes;
        if (!Manager->Alive || inValue.Entity != &Manager->Entity)
            return false;
        return HTNTryToAtom(inClientContext, Manager->Entity.ID, outAtom);
    }
};

namespace
{
struct EntityFunctions
{
    static TestEntityRef Identity(TestEntityRef inEntity) { return inEntity; }
};
struct EntityDaemon
{
    TestEntityRef Value;
    int Calls = 0;
    TestEntityRef Probe() { ++Calls; return Value; }
    TestEntityRef Identity(TestEntityRef inEntity) { ++Calls; return inEntity; }
};
}

TEST(HTNTypeConversionTest, ManagerConversionRejectsMissingStaleAndForeignEntities)
{
    TestEntityManager Manager, Other;
    TestEntityRef Value;
    HTNAtomOwner Atom;
    EXPECT_FALSE(HTNTryParseType(nullptr, HTNAtomOwner(int32(7)), Value));
    EXPECT_FALSE(HTNTryToAtom(nullptr, TestEntityRef{&Manager.Entity}, Atom));
    EXPECT_FALSE(HTNTryParseType(&Manager, HTNAtomOwner(int32(99)), Value));
    EXPECT_FALSE(HTNTryToAtom(&Manager, TestEntityRef{&Other.Entity}, Atom));
    ASSERT_TRUE(HTNTryParseType(&Manager, HTNAtomOwner(int32(7)), Value));
    EXPECT_EQ(Value.Entity, &Manager.Entity);
    ASSERT_TRUE(HTNTryToAtom(&Manager, Value, Atom));
    EXPECT_EQ(Atom.GetValue<int32>(), 7);
    Manager.Alive = false;
    EXPECT_FALSE(HTNTryParseType(&Manager, Atom, Value));
    EXPECT_FALSE(HTNTryToAtom(&Manager, Value, Atom));

    Manager.Alive = true;
    HTNAtomOwner Call(HTNAtom::sCreateCallWithContext(&Manager, HtnSymbol::sGetSymbol("entity"), Value));
    EXPECT_TRUE(Call.IsBound());
    HTNAtomOwner Failed(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("entity"), Value));
    EXPECT_FALSE(Failed.IsBound());
}

TEST(HTNTypeConversionTest, StaticAndMemberBindingsUseEachInvocationContext)
{
    HTNCallTermRegistry Registry;
    HTN_CALLTERM_BIND(Registry, "static", EntityFunctions, Identity);
    ASSERT_TRUE(HTN_CALLTERM_BIND_MEMBER(Registry, "member", EntityDaemon, Identity));
    TestEntityManager First, Second;
    EntityDaemon Daemon;
    HTNCallTermBindingContext Context(Registry);
    ASSERT_TRUE(HTN_CALLTERM_SET_DAEMON(Context, EntityDaemon, &Daemon));
    const std::vector<HTNAtomOwner> Arguments{HTNAtomOwner(int32(7))};
    for (const char* Name : {"static", "member"})
    {
        const auto Call = HTNCallTermRegistry_ResolveGeneratedCallTerm(&Context, Name);
        for (auto* Manager : {&First, &Second})
        {
            HTNPlannerExecutionContext Execution{};
            Execution.CallTermBindingContext = &Context;
            Execution.ClientContext = Manager;
            HTNGeneratedPlannerContext Generated{};
            Generated.callterm_binding_context = &Context;
            Generated.client_context = Manager;
            const int Reads = Manager->Reads;
            const int Writes = Manager->Writes;
            ASSERT_TRUE(Registry.Execute(Name, Execution, Arguments).IsBound());
            const HTNAtom* RawArguments[] = {Arguments[0].Get()};
            HTNAtomOwner Result;
            ASSERT_EQ(HTNCallTermRegistry_InvokeGeneratedCallTermWithSource(&Generated, &Call, RawArguments, 1u, Result.Get(), nullptr), 1);
            EXPECT_EQ(Result.GetValue<int32>(), 7);
            EXPECT_EQ(Manager->Reads, Reads + 2);
            EXPECT_EQ(Manager->Writes, Writes + 2);
        }
    }
    EXPECT_EQ(First.Reads, 4);
    EXPECT_EQ(Second.Reads, 4);
    const int Calls = Daemon.Calls;
    Second.Alive = false;
    HTNPlannerExecutionContext Execution{};
    Execution.CallTermBindingContext = &Context;
    Execution.ClientContext = &Second;
    EXPECT_FALSE(Registry.Execute("member", Execution, Arguments).IsBound());
    EXPECT_EQ(Daemon.Calls, Calls);
}

extern "C" const HTNGeneratedPlannerDefinition* CreateMissingCalltermsHTN_GetDefinition(void);

TEST(HTNTypeConversionTest, GeneratedNestedCallsPropagateClientContextAcrossSharedRegistry)
{
    HTNCallTermRegistry Registry;
    HTN_CALLTERM_BIND(Registry, "identity", EntityFunctions, Identity);
    ASSERT_TRUE(HTN_CALLTERM_BIND_MEMBER(Registry, "probe", EntityDaemon, Probe));
    TestEntityManager First, Second;
    Second.Entity.ID = 23;
    HTNDatabaseHook Database;
    HTNPlannerHook FirstHook(Database.GetWorldState(), Registry), SecondHook(Database.GetWorldState(), Registry);
    ASSERT_TRUE(FirstHook.SetGeneratedPlannerDefinition(CreateMissingCalltermsHTN_GetDefinition()));
    ASSERT_TRUE(SecondHook.SetGeneratedPlannerDefinition(CreateMissingCalltermsHTN_GetDefinition()));
    EntityDaemon FirstDaemon{{&First.Entity}}, SecondDaemon{{&Second.Entity}};
    auto& FirstContext = FirstHook.GetCallTermBindingContext();
    auto& SecondContext = SecondHook.GetCallTermBindingContext();
    ASSERT_TRUE(HTN_CALLTERM_SET_DAEMON(FirstContext, EntityDaemon, &FirstDaemon));
    ASSERT_TRUE(HTN_CALLTERM_SET_DAEMON(SecondContext, EntityDaemon, &SecondDaemon));
    HTNPlanningUnit FirstUnit(Database, FirstHook, "nested"), SecondUnit(Database, SecondHook, "nested");
    FirstUnit.SetClientContext(&First);
    SecondUnit.SetClientContext(&Second);
    for (int Iteration = 0; Iteration < 2; ++Iteration)
    {
        ASSERT_EQ(FirstUnit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("nested")), HTN_DECOMPOSITION_SUCCEEDED);
        ASSERT_EQ(SecondUnit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("nested")), HTN_DECOMPOSITION_SUCCEEDED);
        ASSERT_EQ(FirstUnit.GetCurrentPlan().size(), 1u);
        ASSERT_EQ(SecondUnit.GetCurrentPlan().size(), 1u);
        EXPECT_EQ(HTNAtomGetValue<int32>(HTNGetTaskArgument(FirstUnit.GetCurrentPlan().front(), 0u)), 7);
        EXPECT_EQ(HTNAtomGetValue<int32>(HTNGetTaskArgument(SecondUnit.GetCurrentPlan().front(), 0u)), 23);
    }
    EXPECT_EQ(First.Reads, 2);
    EXPECT_EQ(First.Writes, 4);
    EXPECT_EQ(Second.Reads, 2);
    EXPECT_EQ(Second.Writes, 4);
    ASSERT_EQ(FirstUnit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("deferred")), HTN_DECOMPOSITION_SUCCEEDED);
    HTNPlanningUnit Moved(std::move(FirstUnit));
    EXPECT_EQ(Moved.GetClientContext(), &First);
    EXPECT_EQ(Moved.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::TaskReady);
    First.Alive = false;
    EXPECT_EQ(Moved.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("nested")), HTN_DECOMPOSITION_NO_PLAN);
    EXPECT_EQ(SecondUnit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("nested")), HTN_DECOMPOSITION_SUCCEEDED);
}

TEST(HTNTypeConversionTest, CoreGeneratedExecutionUsesCurrentContextWithCachedBindings)
{
    HTNCallTermRegistry Registry;
    HTN_CALLTERM_BIND(Registry, "identity", EntityFunctions, Identity);
    Registry.Bind("probe", [](const HTNCallTermArguments& inArguments) {
        const auto* Manager = static_cast<TestEntityManager*>(inArguments.GetClientContext());
        return Manager ? HTNAtomOwner(Manager->Entity.ID) : HTNAtomOwner();
    });
    HTNCallTermBindingContext Bindings(Registry);
    const auto* Definition = CreateMissingCalltermsHTN_GetDefinition();
    struct Storage
    {
        const HTNGeneratedPlannerDefinition* Definition;
        void* Prepared;
        void* Execution;
        ~Storage()
        {
            Definition->destroy_execution_storage(Execution);
            Definition->destroy_prepared_storage(Prepared);
            ::operator delete(Execution);
            ::operator delete(Prepared);
        }
    };
    void* Prepared = ::operator new(Definition->prepared_storage_size, std::nothrow);
    void* Execution = ::operator new(Definition->execution_storage_size, std::nothrow);
    if (!Prepared || !Execution)
    {
        ::operator delete(Prepared);
        ::operator delete(Execution);
        FAIL() << "Could not allocate test planner storage";
    }
    ASSERT_TRUE(Definition->initialize_prepared_storage(Prepared));
    ASSERT_TRUE(Definition->initialize_execution_storage(Execution));
    Storage Owned{Definition, Prepared, Execution};
    HTNDatabaseHook Database;
    HTNGeneratedPlannerContext Context{};
    Context.world_state = &Database.GetWorldState();
    Context.callterm_binding_context = &Bindings;
    Context.backtracking_mode = HTN_BACKTRACKING_ALL;
    Context.prepared_storage = Prepared;
    Context.execution_storage = Execution;
    TestEntityManager First, Second;
    Second.Entity.ID = 23;
    HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("nested")));
    for (auto* Manager : {&First, &Second, &First})
    {
        Context.client_context = Manager;
        HTNAtomOwner Plan;
        ASSERT_EQ(Definition->decompose_call(&Context, Call.Get(), 1, Plan.Get()), HTN_DECOMPOSITION_SUCCEEDED);
        ASSERT_EQ(Plan.GetListSize(), 1);
        EXPECT_EQ(HTNAtomGetValue<int32>(HTNGetTaskArgument(Plan.GetListElement(0), 0)), Manager->Entity.ID);
    }
    EXPECT_EQ(First.Reads, 2);
    EXPECT_EQ(First.Writes, 2);
    EXPECT_EQ(Second.Reads, 1);
    EXPECT_EQ(Second.Writes, 1);
    Context.client_context = nullptr;
    HTNAtomOwner Plan;
    EXPECT_EQ(Definition->decompose_call(&Context, Call.Get(), 1, Plan.Get()), HTN_DECOMPOSITION_NO_PLAN);
}
