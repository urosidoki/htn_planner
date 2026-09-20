// Copyright (c) 2023 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <new>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "HTNCoreMinimal.h"
#include "HTNIntegration.h"
#include "AI/AIHtnListDaemon.h"
#include "Core/HTNFileHelpers.h"
#include "Core/HtnSymbol.h"
#include "Core/HTNPathHelpers.h"
#include "Core/HTNCallTermBinding.h"
#include "Core/HTNTask.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "Translator/HTNGeneratedDebugger.h"

#include "gtest/gtest-param-test.h"
#include "gtest/gtest.h"

extern "C" const HTNGeneratedPlannerDefinition* CreateEliteNinjaHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateGruntHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateNormalNinjaHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateWandererHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateCalltermsHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateComplexScenarioHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateHumanHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateHierarchicalBacktrackingHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateNestedCallsHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateIncludeDemoHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateAtomListDemoHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateBacktrackingPolicyOverflowHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateBacktrackingPolicyFixedSmallHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateBacktrackingPolicyFixedEnoughHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateAAACombatNPCHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateNumericExpressionsHTN_GetDefinition(void);
namespace
{
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
class HTNOwnedResourceTestEnvironment final : public ::testing::Environment
{
public:
    void SetUp() override
    {
        mBefore = HTNAtomDebug_GetStats();
        std::fprintf(stderr,
            "[HTNAtom] test suite baseline: heap strings=%llu, heap string bytes=%llu, list nodes=%llu\n",
            static_cast<unsigned long long>(mBefore.live_heap_strings),
            static_cast<unsigned long long>(mBefore.live_heap_string_bytes),
            static_cast<unsigned long long>(mBefore.live_list_nodes));
    }

    void TearDown() override
    {
        const HTNAtomDebugStats After = HTNAtomDebug_GetStats();
        const int64_t StringDelta = static_cast<int64_t>(After.live_heap_strings) - static_cast<int64_t>(mBefore.live_heap_strings);
        const int64_t StringByteDelta = static_cast<int64_t>(After.live_heap_string_bytes) - static_cast<int64_t>(mBefore.live_heap_string_bytes);
        const int64_t ListNodeDelta = static_cast<int64_t>(After.live_list_nodes) - static_cast<int64_t>(mBefore.live_list_nodes);

        std::fprintf(stderr,
            "[HTNAtom] test suite final: heap strings=%llu (%+lld), heap string bytes=%llu (%+lld), list nodes=%llu (%+lld)\n",
            static_cast<unsigned long long>(After.live_heap_strings),
            static_cast<long long>(StringDelta),
            static_cast<unsigned long long>(After.live_heap_string_bytes),
            static_cast<long long>(StringByteDelta),
            static_cast<unsigned long long>(After.live_list_nodes),
            static_cast<long long>(ListNodeDelta));

        EXPECT_EQ(mBefore.live_heap_strings, After.live_heap_strings);
        EXPECT_EQ(mBefore.live_heap_string_bytes, After.live_heap_string_bytes);
        EXPECT_EQ(mBefore.live_list_nodes, After.live_list_nodes);
    }

private:
    HTNAtomDebugStats mBefore{};
};

::testing::Environment* const GHTNOwnedResourceTestEnvironment =
    ::testing::AddGlobalTestEnvironment(new HTNOwnedResourceTestEnvironment());

#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS_DETAILED
class HTNOwnedResourcePerTestListener final : public ::testing::EmptyTestEventListener
{
public:
    void OnTestStart(const ::testing::TestInfo&) override
    {
        mBefore = HTNAtomDebug_GetStats();
    }

    void OnTestEnd(const ::testing::TestInfo& inTestInfo) override
    {
        const HTNAtomDebugStats After = HTNAtomDebug_GetStats();
        const int64_t StringDelta = static_cast<int64_t>(After.live_heap_strings) - static_cast<int64_t>(mBefore.live_heap_strings);
        const int64_t StringByteDelta = static_cast<int64_t>(After.live_heap_string_bytes) - static_cast<int64_t>(mBefore.live_heap_string_bytes);
        const int64_t ListNodeDelta = static_cast<int64_t>(After.live_list_nodes) - static_cast<int64_t>(mBefore.live_list_nodes);

        if (StringDelta == 0 && StringByteDelta == 0 && ListNodeDelta == 0)
            return;

        std::fprintf(stderr,
            "[HTNAtom] test owned-resource delta: %s.%s heap strings=%+lld, heap string bytes=%+lld, list nodes=%+lld\n",
            inTestInfo.test_case_name(),
            inTestInfo.name(),
            static_cast<long long>(StringDelta),
            static_cast<long long>(StringByteDelta),
            static_cast<long long>(ListNodeDelta));
    }

private:
    HTNAtomDebugStats mBefore{};
};

struct HTNOwnedResourceListenerRegistration
{
    HTNOwnedResourceListenerRegistration()
    {
        ::testing::UnitTest::GetInstance()->listeners().Append(new HTNOwnedResourcePerTestListener());
    }
};

HTNOwnedResourceListenerRegistration GHTNOwnedResourceListenerRegistration;
#endif
#endif

struct GeneratedPlannerRegistration
{
    const char* domain_id;
    const HTNGeneratedPlannerDefinition* definition;
};

const GeneratedPlannerRegistration kGeneratedPlannerRegistrations[] = {
    { "AAACombatNPC", CreateAAACombatNPCHTN_GetDefinition() },
    { "AtomListDemo", CreateAtomListDemoHTN_GetDefinition() },
    { "EliteNinja", CreateEliteNinjaHTN_GetDefinition() },
    { "Grunt", CreateGruntHTN_GetDefinition() },
    { "NormalNinja", CreateNormalNinjaHTN_GetDefinition() },
    { "Wanderer", CreateWandererHTN_GetDefinition() },
    { "CallTermsDemo", CreateCalltermsHTN_GetDefinition() },
    { "ComplexScenario", CreateComplexScenarioHTN_GetDefinition() },
    { "Human", CreateHumanHTN_GetDefinition() },
    { "NestedCallsDemo", CreateNestedCallsHTN_GetDefinition() },
    { "IncludeDemo", CreateIncludeDemoHTN_GetDefinition() },
    { "NumericExpressionsDemo", CreateNumericExpressionsHTN_GetDefinition() },
};

uint32_t GetGeneratedPlannerDefinitionCount()
{
    return static_cast<uint32_t>(sizeof(kGeneratedPlannerRegistrations) / sizeof(kGeneratedPlannerRegistrations[0]));
}

const HTNGeneratedPlannerDefinition* GetGeneratedPlannerDefinition(const uint32_t inIndex)
{
    return inIndex < GetGeneratedPlannerDefinitionCount() ? kGeneratedPlannerRegistrations[inIndex].definition : nullptr;
}

struct HTNEquivalenceCase
{
    const char* TestName;
    const char* WorldStateFile;
    const char* DomainFile;
    const char* GeneratedDomainName;
    const char* TopLevelMethod;
};

class HTNAtomLifetimeBalanceScope
{
public:
    HTNAtomLifetimeBalanceScope()
        : mBefore(HTNAtomDebug_GetStats())
    {
    }

    ~HTNAtomLifetimeBalanceScope()
    {
#ifdef HTN_MEMORY_ATOM_DIAGNOSTICS
        const HTNAtomDebugStats After = HTNAtomDebug_GetStats();
        EXPECT_EQ(mBefore.live_heap_strings, After.live_heap_strings);
        EXPECT_EQ(mBefore.live_heap_string_bytes, After.live_heap_string_bytes);
        EXPECT_EQ(mBefore.live_list_nodes, After.live_list_nodes);
#endif
    }

private:
    HTNAtomDebugStats mBefore;
};

bool binded_function_with_args(const std::string& Arg)
{
    (void)Arg;
    return true;
}

int get_health(int entityId)
{
    (void)entityId;
    return 50;
}

float get_max_speed(int entityId)
{
    (void)entityId;
    return 1.0f;
}

bool lower_than(int Left, int Right)
{
    return Left < Right;
}

int increase(int Value)
{
    return Value + 1;
}

struct PerEntityCallTermDaemon
{
    int32 Value = 0;

    int32 ReadValue()
    {
        return Value;
    }
};

class TestWorldStateCallTermDaemon
{
public:
    template<typename THook>
    explicit TestWorldStateCallTermDaemon(
        THook& inPlannerHook,
        std::string inTarget = "enemy0")
        : mWorldState(inPlannerHook.GetWorldState())
        , mTarget(std::move(inTarget))
    {
    }

    static void BindCallTerms(HTNCallTermRegistry& ioRegistry)
    {
        HTN_CALLTERM_BIND_MEMBER(
            ioRegistry,
            "add_target_available",
            TestWorldStateCallTermDaemon,
            AddTargetAvailable);
    }

private:
    bool AddTargetAvailable()
    {
        const std::vector<HTNAtomOwner> Arguments{HTNAtomOwner(mTarget)};
        mWorldState.AddFact("target_available", Arguments);
        return true;
    }

    HTNWorldState& mWorldState;
    std::string mTarget;
};

template<typename THook>
void SetTestWorldStateCallTermDaemon(
    THook& ioPlannerHook,
    TestWorldStateCallTermDaemon& inDaemon)
{
    ASSERT_TRUE(HTN_CALLTERM_SET_DAEMON(
        ioPlannerHook.GetCallTermBindingContext(),
        TestWorldStateCallTermDaemon,
        &inDaemon));
}

void BindTestCallTerms(HTNCallTermRegistry& ioRegistry)
{
    AIHtnListDaemon::BindCallTerms(ioRegistry);
    TestWorldStateCallTermDaemon::BindCallTerms(ioRegistry);

    ioRegistry.Bind("binded_function_with_args",
        [](const HTNCallTermArguments& inArguments) -> bool
        {
            if (inArguments.size() != 1 || !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<std::string>(inArguments[0]))
                return false;
            return binded_function_with_args(HTNAtomGetValue<std::string>(inArguments[0]));
        });

    ioRegistry.Bind("get_health",
        [](const HTNCallTermArguments& inArguments) -> int
        {
            if (inArguments.size() != 1 || !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]))
                return 0;
            return get_health(HTNAtomGetValue<int32>(inArguments[0]));
        });

    ioRegistry.Bind("get_max_speed",
        [](const HTNCallTermArguments& inArguments) -> float
        {
            if (inArguments.size() != 1 || !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]))
                return 0.0f;
            return get_max_speed(HTNAtomGetValue<int32>(inArguments[0]));
        });


    ioRegistry.Bind("lt",
        [](const HTNCallTermArguments& inArguments) -> bool
        {
            if (inArguments.size() != 2 ||
                !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]) ||
                !HTNAtomIsBound(inArguments[1]) || !HTNAtomIsType<int32>(inArguments[1]))
            {
                return false;
            }

            return lower_than(
                HTNAtomGetValue<int32>(inArguments[0]),
                HTNAtomGetValue<int32>(inArguments[1]));
        });

    ioRegistry.Bind("inc",
        [](const HTNCallTermArguments& inArguments) -> int
        {
            if (inArguments.size() != 1 || !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]))
                return 0;

            return increase(HTNAtomGetValue<int32>(inArguments[0]));
        });

    ioRegistry.Bind("add",
        [](const HTNCallTermArguments& inArguments) -> int
        {
            if (inArguments.size() != 2 ||
                !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]) ||
                !HTNAtomIsBound(inArguments[1]) || !HTNAtomIsType<int32>(inArguments[1]))
            {
                return 0;
            }
            return HTNAtomGetValue<int32>(inArguments[0]) + HTNAtomGetValue<int32>(inArguments[1]);
        });

    ioRegistry.Bind("mul",
        [](const HTNCallTermArguments& inArguments) -> int
        {
            if (inArguments.size() != 2 ||
                !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]) ||
                !HTNAtomIsBound(inArguments[1]) || !HTNAtomIsType<int32>(inArguments[1]))
            {
                return 0;
            }
            return HTNAtomGetValue<int32>(inArguments[0]) * HTNAtomGetValue<int32>(inArguments[1]);
        });
}

void BindTracingNestedCallTerms(HTNCallTermRegistry& ioRegistry, std::vector<std::string>& outTrace)
{
    ioRegistry.Bind("inc",
        [&outTrace](const HTNCallTermArguments& inArguments) -> int
        {
            if (inArguments.size() != 1 || !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]))
                return 0;
            const int32 Value = HTNAtomGetValue<int32>(inArguments[0]);
            outTrace.emplace_back("inc(" + std::to_string(Value) + ")");
            return Value + 1;
        });

    ioRegistry.Bind("add",
        [&outTrace](const HTNCallTermArguments& inArguments) -> int
        {
            if (inArguments.size() != 2 ||
                !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]) ||
                !HTNAtomIsBound(inArguments[1]) || !HTNAtomIsType<int32>(inArguments[1]))
            {
                return 0;
            }
            const int32 Left = HTNAtomGetValue<int32>(inArguments[0]);
            const int32 Right = HTNAtomGetValue<int32>(inArguments[1]);
            outTrace.emplace_back("add(" + std::to_string(Left) + "," + std::to_string(Right) + ")");
            return Left + Right;
        });

    ioRegistry.Bind("mul",
        [&outTrace](const HTNCallTermArguments& inArguments) -> int
        {
            if (inArguments.size() != 2 ||
                !HTNAtomIsBound(inArguments[0]) || !HTNAtomIsType<int32>(inArguments[0]) ||
                !HTNAtomIsBound(inArguments[1]) || !HTNAtomIsType<int32>(inArguments[1]))
            {
                return 0;
            }
            const int32 Left = HTNAtomGetValue<int32>(inArguments[0]);
            const int32 Right = HTNAtomGetValue<int32>(inArguments[1]);
            outTrace.emplace_back("mul(" + std::to_string(Left) + "," + std::to_string(Right) + ")");
            return Left * Right;
        });
}

std::string FormatPlanStep(const HTNAtomOwner& inTask)
{
    std::ostringstream Stream;
    const HtnSymbol* TaskHead = HTNGetTaskHead(inTask);
    Stream << (TaskHead ? TaskHead->GetString() : "<invalid task>");
    const uint32 ArgumentCount = HTNGetTaskArgumentCount(inTask);
    for (uint32 ArgumentIndex = 0u; ArgumentIndex < ArgumentCount; ++ArgumentIndex)
        Stream << " " << HTNAtomToString(HTNGetTaskArgument(inTask, ArgumentIndex), true);
    return Stream.str();
}

std::vector<std::string> FormatPlan(const HTNAtomOwner& inOutput)
{
    std::vector<std::string> Result;
    const int32_t Count = inOutput.GetListSize();
    if (Count <= 0)
        return Result;

    Result.reserve(static_cast<size_t>(Count));
    for (int32_t Index = 0; Index < Count; ++Index)
        Result.emplace_back(FormatPlanStep(HTNAtomOwner(inOutput.GetListElement(static_cast<uint32>(Index)))));
    return Result;
}

const HTNGeneratedPlannerDefinition* FindGeneratedDomain(const std::string& inDomainName)
{
    const uint32_t Count = GetGeneratedPlannerDefinitionCount();
    for (uint32_t I = 0; I < Count; ++I)
    {
        const GeneratedPlannerRegistration& Registration = kGeneratedPlannerRegistrations[I];
        if (Registration.domain_id && inDomainName == Registration.domain_id)
            return Registration.definition;
    }
    return nullptr;
}

HTNDecompositionStatus RunGeneratedPlannerWithStorage(const HTNGeneratedPlannerDefinition& inRegistration,
                                    HTNWorldState& ioWorldState,
                                    const HTNCallTermBindingContext& inCallTermBindingContext,
                                    const std::string& inTopLevelMethod,
                                    void* inPreparedStorage,
                                    void* inExecutionStorage,
                                    HTNAtomOwner& outResult)
{
    const HtnSymbol* TopLevelMethod = HtnSymbol::sGetSymbol(inTopLevelMethod);
    HTNGeneratedPlannerContext Context{};
    Context.world_state = &ioWorldState;
    Context.callterm_binding_context = &inCallTermBindingContext;
    Context.backtracking_mode = HTN_BACKTRACKING_ALL;
    Context.execution_storage = inExecutionStorage;
    Context.prepared_storage = inPreparedStorage;

    if (!inRegistration.decompose_call || !TopLevelMethod)
        return HTN_DECOMPOSITION_INVALID_CONTEXT;

    HTNAtom Call = HTNAtom::sCreateCall(TopLevelMethod);
    HTNAtom Result{};
    const HTNDecompositionStatus Status = inRegistration.decompose_call(&Context, &Call, 1, &Result);
    outResult = HTNAtomOwner(std::move(Result));
    HTNAtom::sDestroy(Call);
    return Status;
}

HTNDecompositionStatus RunGeneratedPlanner(const HTNGeneratedPlannerDefinition& inRegistration,
                         HTNWorldState& ioWorldState,
                         const HTNCallTermBindingContext& inCallTermBindingContext,
                         const std::string& inTopLevelMethod,
                         HTNAtomOwner& outResult)
{
    const HTNGeneratedPlannerDefinition* Domain = &inRegistration;

    void* PreparedStorage = ::operator new(Domain->prepared_storage_size, std::nothrow);
    if (!PreparedStorage || !Domain->initialize_prepared_storage(PreparedStorage))
    {
        ::operator delete(PreparedStorage);
        return HTN_DECOMPOSITION_OUT_OF_MEMORY;
    }

    void* ExecutionStorage = ::operator new(Domain->execution_storage_size, std::nothrow);
    if (!ExecutionStorage || !Domain->initialize_execution_storage(ExecutionStorage))
    {
        ::operator delete(ExecutionStorage);
        Domain->destroy_prepared_storage(PreparedStorage);
        ::operator delete(PreparedStorage);
        return HTN_DECOMPOSITION_OUT_OF_MEMORY;
    }

    const HTNDecompositionStatus Status = RunGeneratedPlannerWithStorage(
        inRegistration,
        ioWorldState,
        inCallTermBindingContext,
        inTopLevelMethod,
        PreparedStorage,
        ExecutionStorage,
        outResult);

    Domain->destroy_execution_storage(ExecutionStorage);
    ::operator delete(ExecutionStorage);
    Domain->destroy_prepared_storage(PreparedStorage);
    ::operator delete(PreparedStorage);
    return Status;
}

HTNDecompositionStatus RunGeneratedPlanner(const HTNGeneratedPlannerDefinition& inRegistration,
                         HTNWorldState& ioWorldState,
                         const HTNCallTermBindingContext& inCallTermBindingContext,
                         const std::string& inTopLevelMethod,
                         std::vector<std::string>& outPlan)
{
    HTNAtomOwner Output;
    const HTNDecompositionStatus Status = RunGeneratedPlanner(
        inRegistration, ioWorldState, inCallTermBindingContext, inTopLevelMethod, Output);
    outPlan = FormatPlan(Output);
    return Status;
}

std::string MakeTestFilePath(const std::string& inRoot,
                             const std::string& inFileName,
                             const std::string& inExtension)
{
    const std::string TestDirectory = inRoot + "/" + HTNFileHelpers::kTestDirectoryName;
    return HTNFileHelpers::MakeAbsolutePath(
        HTNFileHelpers::MakeFilePath(TestDirectory, inFileName, inExtension)).string();
}

class HTNGeneratedEquivalenceTest : public testing::TestWithParam<HTNEquivalenceCase>
{
};

TEST(HTNGeneratedBacktrackingTest, ParentAndChildGuardProduceSameFallbackPlan)
{
    HTNDatabaseHook Database;
    HTNPlannerHook Planner(Database.GetWorldState());
    EXPECT_TRUE(Planner.SetGeneratedPlannerDefinition(CreateHierarchicalBacktrackingHTN_GetDefinition()));

    const auto ExpectSearchPlan = [&](const char* inMethod)
    {
        HTNPlanningUnit PlanningUnit(Database, Planner, HtnSymbol::sGetSymbol(inMethod));
        ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(
            HtnSymbol::sGetSymbol(inMethod),
            HtnSymbol::sGetSymbol("enemy")), HTN_DECOMPOSITION_SUCCEEDED);

        const HTNAtomOwner& Plan = PlanningUnit.GetLastDecomposition().GetResult();
        ASSERT_TRUE(Plan.IsType<HTNAtomList>());
        ASSERT_EQ(Plan.GetListSize(), 1);
        const HTNAtom* Step = HTNAtom_GetListElement(Plan.Get(), 0u);
        ASSERT_NE(Step, nullptr);
        ASSERT_NE(HTNGetCallHead(Step), nullptr);
        EXPECT_EQ(HTNGetCallHead(Step)->GetString(), "!search");
    };

    ExpectSearchPlan("do_behavior_parent_guard");
    ExpectSearchPlan("do_behavior_child_guard");
}

TEST(HTNGeneratedBacktrackingRuntimeModeTest, SupportsIndependentFactAxiomAndBranchFlags)
{
    const HTNGeneratedPlannerDefinition* Definition = CreateHierarchicalBacktrackingHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);
    if ((Definition->features & HTN_GENERATED_FEATURE_RUNTIME_BACKTRACKING) == 0u)
    {
        return;
    }

    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "hierarchical_backtracking",
        HTNFileHelpers::kDomainFileExtension);
    const std::string WorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "hierarchical_backtracking_no_combat",
        HTNFileHelpers::kWorldStateFileExtension);

    struct ModeCase
    {
        HTNBacktrackingMode Mode;
        bool FactAxiomBacktracking;
        bool BranchBacktracking;
    };

    static constexpr ModeCase Cases[] = {
        {HTN_BACKTRACKING_NONE, false, false},
        {HTN_BACKTRACKING_FACTS_AND_AXIOMS, true, false},
        {HTN_BACKTRACKING_BRANCHES, false, true},
        {HTN_BACKTRACKING_ALL, true, true},
    };

    const auto ExpectPlan = [](HTNPlanningUnit& ioPlanningUnit, const char* inMethod,
                               const HTNDecompositionStatus inExpectedResult,
                               const char* inExpectedHead, const char* inExpectedArgument)
    {
        ASSERT_EQ(ioPlanningUnit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol(inMethod)), inExpectedResult);
        if (inExpectedResult != HTN_DECOMPOSITION_SUCCEEDED)
            return;

        const HTNAtomOwner& Plan = ioPlanningUnit.GetLastDecomposition().GetResult();
        ASSERT_TRUE(Plan.IsType<HTNAtomList>());
        ASSERT_EQ(Plan.GetListSize(), 1);
        const HTNAtom* Step = HTNAtom_GetListElement(Plan.Get(), 0u);
        ASSERT_NE(Step, nullptr);
        ASSERT_NE(HTNGetCallHead(Step), nullptr);
        EXPECT_EQ(HTNGetCallHead(Step)->GetString(), inExpectedHead);
        if (inExpectedArgument)
        {
            const HTNAtom* Argument = HTNFindCallArgument(Step, 0u);
            ASSERT_NE(Argument, nullptr);
            ASSERT_EQ(HTNAtom_GetType(Argument), HTN_ATOM_TYPE_SYMBOL);
            ASSERT_NE(Argument->value.symbol_value, nullptr);
            EXPECT_EQ(static_cast<const HtnSymbol*>(Argument->value.symbol_value)->GetString(), inExpectedArgument);
        }
    };

    for (const ModeCase& Case : Cases)
    {
        HTNDatabaseHook Database;
        ASSERT_TRUE(Database.ParseWorldStateFile(WorldStatePath));
        HTNPlannerHook Planner(Database.GetWorldState());
        EXPECT_TRUE(Planner.SetGeneratedPlannerDefinition(Definition));

        HTNPlanningUnit PlanningUnit(Database, Planner, HtnSymbol::sGetSymbol("validate_child_guard"));
        PlanningUnit.SetBacktrackingMode(Case.Mode);

        ExpectPlan(PlanningUnit, "validate_child_guard",
                   Case.BranchBacktracking ? HTN_DECOMPOSITION_SUCCEEDED : HTN_DECOMPOSITION_NO_PLAN,
                   "!search", "enemy");
        ExpectPlan(PlanningUnit, "validate_fact_backtracking", HTN_DECOMPOSITION_SUCCEEDED,
                   Case.FactAxiomBacktracking ? "!selected_fact" : "!fact_fallback",
                   Case.FactAxiomBacktracking ? "second" : nullptr);
        ExpectPlan(PlanningUnit, "validate_axiom_backtracking", HTN_DECOMPOSITION_SUCCEEDED,
                   Case.FactAxiomBacktracking ? "!selected_axiom" : "!axiom_fallback",
                   Case.FactAxiomBacktracking ? "second" : nullptr);
    }
}

TEST(HTNGeneratedBacktrackingPolicyTest, FixedWithOverflowExceedsInlineCapacityAndCompletesPlan)
{
    HTNAtomLifetimeBalanceScope LifetimeBalance;
    HTNDatabaseHook Database;
    HTNPlannerHook PlannerHook(Database.GetWorldState());

    HTNAtomOwner Output;
    const HTNGeneratedPlannerDefinition* Definition = CreateBacktrackingPolicyOverflowHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);

    const HTNDecompositionStatus Result = RunGeneratedPlanner(
        *Definition,
        Database.GetWorldState(),
        PlannerHook.GetCallTermBindingContext(),
        "run",
        Output);
    EXPECT_EQ(Result, HTN_DECOMPOSITION_SUCCEEDED);

    const std::vector<std::string> ExpectedPlan = {
        "!step \"one\"",
        "!step \"two\"",
        "!step \"three\""};
    EXPECT_EQ(ExpectedPlan, FormatPlan(Output));
}

TEST(HTNGeneratedBacktrackingPolicyTest, FixedCapacityExhaustionFailsCleanlyAndStorageRemainsReusable)
{
    HTNAtomLifetimeBalanceScope LifetimeBalance;
    HTNDatabaseHook Database;
    HTNPlannerHook PlannerHook(Database.GetWorldState());
    const HTNGeneratedPlannerDefinition* Definition = CreateBacktrackingPolicyFixedSmallHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);

    const HTNGeneratedPlannerDefinition* Domain = Definition;
    ASSERT_NE(Domain, nullptr);
    void* PreparedStorage = ::operator new(Domain->prepared_storage_size, std::nothrow);
    ASSERT_NE(PreparedStorage, nullptr);
    if (!Domain->initialize_prepared_storage(PreparedStorage))
    {
        ::operator delete(PreparedStorage);
        FAIL() << "Prepared storage initialization failed";
    }

    void* ExecutionStorage = ::operator new(Domain->execution_storage_size, std::nothrow);
    if (!ExecutionStorage)
    {
        Domain->destroy_prepared_storage(PreparedStorage);
        ::operator delete(PreparedStorage);
        FAIL() << "Execution storage allocation failed";
    }
    if (!Domain->initialize_execution_storage(ExecutionStorage))
    {
        ::operator delete(ExecutionStorage);
        Domain->destroy_prepared_storage(PreparedStorage);
        ::operator delete(PreparedStorage);
        FAIL() << "Execution storage initialization failed";
    }

    HTNAtomOwner FailedOutput;
    const HTNDecompositionStatus ExhaustedResult = RunGeneratedPlannerWithStorage(
        *Definition,
        Database.GetWorldState(),
        PlannerHook.GetCallTermBindingContext(),
        "run",
        PreparedStorage,
        ExecutionStorage,
        FailedOutput);
    EXPECT_EQ(ExhaustedResult, HTN_DECOMPOSITION_BACKTRACKING_CAPACITY_EXCEEDED);
    EXPECT_TRUE(FailedOutput.IsType<HTNAtomList>());
    EXPECT_TRUE(FailedOutput.IsListEmpty());

    HTNAtomOwner RecoveryOutput;
    const HTNDecompositionStatus RecoveryResult = RunGeneratedPlannerWithStorage(
        *Definition,
        Database.GetWorldState(),
        PlannerHook.GetCallTermBindingContext(),
        "run_small",
        PreparedStorage,
        ExecutionStorage,
        RecoveryOutput);
    EXPECT_EQ(RecoveryResult, HTN_DECOMPOSITION_SUCCEEDED);

    const std::vector<std::string> ExpectedRecoveryPlan = {
        "!step \"small-one\"",
        "!step \"small-two\""};
    EXPECT_EQ(ExpectedRecoveryPlan, FormatPlan(RecoveryOutput));

    Domain->destroy_execution_storage(ExecutionStorage);
    ::operator delete(ExecutionStorage);
    Domain->destroy_prepared_storage(PreparedStorage);
    ::operator delete(PreparedStorage);
}


TEST(HTNGeneratedEquivalenceRobustnessTest, NoPlanLeavesNoPartialResultAndGeneratedStorageRemainsReusable)
{
    HTNAtomLifetimeBalanceScope LifetimeBalance;
    HTNDatabaseHook Database;
    HTNPlannerHook Hook(Database.GetWorldState());
    ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(CreateBacktrackingPolicyFixedEnoughHTN_GetDefinition()));
    HTNPlanningUnit Unit(Database, Hook, "always_fail");

    EXPECT_EQ(Unit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_NO_PLAN);
    EXPECT_TRUE(Unit.GetLastDecomposition().GetResult().IsListEmpty());

    ASSERT_EQ(Unit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("run_small")),
              HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(FormatPlan(Unit.GetLastDecomposition().GetResult()),
              (std::vector<std::string>{"!step \"small-one\"", "!step \"small-two\""}));
}



TEST(HTNGeneratedBacktrackingPolicyTest, FixedCapacityProducesExpectedPlanWhenCapacityIsSufficient)
{
    HTNDatabaseHook Database;
    HTNPlannerHook Hook(Database.GetWorldState());
    HTNAtomOwner Output;
    const HTNGeneratedPlannerDefinition* Definition = CreateBacktrackingPolicyFixedEnoughHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);

    ASSERT_EQ(RunGeneratedPlanner(*Definition, Database.GetWorldState(),
                                  Hook.GetCallTermBindingContext(), "run", Output),
              HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(FormatPlan(Output),
              (std::vector<std::string>{"!step \"one\"", "!step \"two\"", "!step \"three\""}));
}


TEST_P(HTNGeneratedEquivalenceTest, GeneratedProducesExpectedResult)
{
    const HTNEquivalenceCase& TestCase = GetParam();
    const bool IsAAACombatNPC = std::string_view(TestCase.GeneratedDomainName) == "AAACombatNPC";

    const std::string WorldStatePath = IsAAACombatNPC
        ? HTNFileHelpers::MakeAbsolutePath(std::string("WorldStates/") + TestCase.WorldStateFile + ".worldstate").string()
        : MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        TestCase.WorldStateFile,
        HTNFileHelpers::kWorldStateFileExtension);

    HTNDatabaseHook GeneratedDatabase;
    ASSERT_TRUE(GeneratedDatabase.ParseWorldStateFile(WorldStatePath));

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNPlannerHook GeneratedHook(GeneratedDatabase.GetWorldState(), CallTermRegistry);
    TestWorldStateCallTermDaemon GeneratedDaemon(GeneratedHook);
    SetTestWorldStateCallTermDaemon(GeneratedHook, GeneratedDaemon);

    const HTNGeneratedPlannerDefinition* Registration = FindGeneratedDomain(TestCase.GeneratedDomainName);
    ASSERT_NE(Registration, nullptr) << "Generated domain was not registered: " << TestCase.GeneratedDomainName;

    HTNAtomOwner GeneratedOutput;
    const HTNDecompositionStatus GeneratedResult = RunGeneratedPlanner(
        *Registration,
        GeneratedDatabase.GetWorldState(),
        GeneratedHook.GetCallTermBindingContext(),
        TestCase.TopLevelMethod,
        GeneratedOutput);

    ASSERT_EQ(GeneratedResult, HTN_DECOMPOSITION_SUCCEEDED);
    ASSERT_FALSE(GeneratedOutput.IsListEmpty());

    if (std::string_view(TestCase.GeneratedDomainName) == "AAACombatNPC")
    {
        ASSERT_EQ(GeneratedResult, HTN_DECOMPOSITION_SUCCEEDED);
        const auto Plan = FormatPlan(GeneratedOutput);
        ASSERT_FALSE(Plan.empty());
        const std::string Name = TestCase.TestName;
        const std::string ExpectedState = Name == "HighOrder" ? "high_order" :
            Name == "Melee" || Name == "Ranged" ? "combat" :
            Name == "MediumOrder" ? "medium_order" :
            Name == "Investigation" ? "investigation" :
            Name == "Search" ? "search" :
            Name == "LowOrder" ? "low_order" : "idle";
        EXPECT_NE(Plan.front().find(ExpectedState), std::string::npos);
        if (Name == "Melee")
            EXPECT_NE(Plan.back().find("202"), std::string::npos);
        if (Name == "Ranged")
            EXPECT_NE(Plan.back().find("302"), std::string::npos);
    }

    if (std::string_view(TestCase.TestName) == "CallTermCreatesFactVisibleImmediately")
    {
        EXPECT_EQ(HTN_DECOMPOSITION_SUCCEEDED, GeneratedResult);
        const std::vector<std::string> ExpectedPlan = {
            "!target_available_visible \"enemy0\""};
        EXPECT_EQ(ExpectedPlan, FormatPlan(GeneratedOutput));
    }
    else if (std::string_view(TestCase.TestName) == "CallTermWorldStateMutationSurvivesBacktracking")
    {
        EXPECT_EQ(HTN_DECOMPOSITION_SUCCEEDED, GeneratedResult);
        const std::vector<std::string> ExpectedPlan = {
            "!persisted_target_available \"enemy0\""};
        EXPECT_EQ(ExpectedPlan, FormatPlan(GeneratedOutput));
    }
    else if (std::string_view(TestCase.TestName) == "ConstantList")
    {
        const std::vector<std::string> ExpectedPlan = {
            "!print_original (patrol 3 true (10.0 20.0 30.0))",
            "!print_added (patrol 3 true (10.0 20.0 30.0) \"return\")",
            "!print_removed (patrol true (10.0 20.0 30.0) \"return\")",
            "!print_element (10.0 20.0 30.0)",
            "!print_size 5",
            "!print_cleared ()"};
        EXPECT_EQ(ExpectedPlan, FormatPlan(GeneratedOutput));
    }
    else if (std::string_view(TestCase.TestName) == "SplitListBasic")
    {
        const std::vector<std::string> ExpectedPlan = {
            "!split_result \"one\" (\"two\" \"three\")"};
        EXPECT_EQ(ExpectedPlan, FormatPlan(GeneratedOutput));
    }
    else if (std::string_view(TestCase.TestName) == "SplitListSingle")
    {
        const std::vector<std::string> ExpectedPlan = {
            "!split_single_result \"only\" ()"};
        EXPECT_EQ(ExpectedPlan, FormatPlan(GeneratedOutput));
    }
    else if (std::string_view(TestCase.TestName) == "SplitListEmpty")
    {
        const std::vector<std::string> ExpectedPlan = {
            "!empty_list_rejected"};
        EXPECT_EQ(ExpectedPlan, FormatPlan(GeneratedOutput));
    }
    else if (std::string_view(TestCase.TestName) == "SplitListBoundOutputs")
    {
        const std::vector<std::string> ExpectedPlan = {
            "!bound_outputs_match"};
        EXPECT_EQ(ExpectedPlan, FormatPlan(GeneratedOutput));
    }
    else if (std::string_view(TestCase.TestName) == "SplitListRollback")
    {
        const std::vector<std::string> ExpectedPlan = {
            "!rollback_result \"only\" ()"};
        EXPECT_EQ(ExpectedPlan, FormatPlan(GeneratedOutput));
    }
    else if (std::string_view(TestCase.GeneratedDomainName) == "NumericExpressionsDemo")
    {
        const std::string ExpectedResult = std::string_view(TestCase.TestName) == "NestedAndMixedArithmetic"
            ? "!numeric_result \"success\""
            : "!numeric_result \"controlled_failure\"";
        EXPECT_EQ(FormatPlan(GeneratedOutput), (std::vector<std::string>{ExpectedResult}));
    }
}






TEST(HTNNestedCallExpressionTest, GeneratedNestedCallsEvaluateDepthFirstLeftToRight)
{
    HTNDatabaseHook Database;
    ASSERT_TRUE(Database.ParseWorldStateFile(MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName, "callterms",
        HTNFileHelpers::kWorldStateFileExtension)));

    std::vector<std::string> Trace;
    HTNCallTermRegistry Registry;
    BindTracingNestedCallTerms(Registry, Trace);
    HTNPlannerHook Hook(Database.GetWorldState(), Registry);
    const HTNGeneratedPlannerDefinition* Definition = FindGeneratedDomain("NestedCallsDemo");
    ASSERT_NE(Definition, nullptr);

    HTNAtomOwner Output;
    ASSERT_EQ(RunGeneratedPlanner(*Definition, Database.GetWorldState(),
                                  Hook.GetCallTermBindingContext(), "test_nested_calls", Output),
              HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(Trace, (std::vector<std::string>{
        "inc(1)", "inc(2)", "inc(3)", "inc(4)", "mul(3,5)", "add(2,15)",
        "inc(4)", "inc(5)", "inc(6)", "inc(7)", "add(6,8)", "add(5,14)"}));
    EXPECT_EQ(FormatPlan(Output),
              (std::vector<std::string>{"!capture 17", "!capture 19"}));
}



#ifdef HTN_DEBUG_DECOMPOSITION
TEST(HTNGeneratedCodeArchitectureTest, GeneratedMetadataContainsOnlyCompileTimeResolvedReferences)
{
    const uint32_t DomainCount = GetGeneratedPlannerDefinitionCount();
    ASSERT_GT(DomainCount, 0u);
    for (uint32_t DomainIndex = 0u; DomainIndex < DomainCount; ++DomainIndex)
    {
        const HTNGeneratedPlannerDefinition* Registration = GetGeneratedPlannerDefinition(DomainIndex);
        ASSERT_NE(Registration, nullptr);
        const HTNGeneratedPlannerDefinition& Domain = *Registration;

        ASSERT_NE(Domain.debug_metadata, nullptr);
        for (uint32_t ValueIndex = 0u; ValueIndex < Domain.debug_metadata->value_count; ++ValueIndex)
        {
            if ((Domain.debug_metadata->values[ValueIndex].flags & HTN_GENERATED_DEBUG_VALUE_FLAG_VARIABLE) != 0u)
            {
                const HTNGeneratedDebugValue& Value = Domain.debug_metadata->values[ValueIndex];
                ASSERT_LT(Value.text, Domain.debug_metadata->string_count)
                    << "Generated variable " << ValueIndex << " has an invalid string-table id";
                ASSERT_NE(Domain.debug_metadata->strings[Value.text], nullptr);

                const std::string VariableID = Domain.debug_metadata->strings[Value.text];
                const bool IsAnySingleton = VariableID.rfind("?any_", 0u) == 0u ||
                                            VariableID.rfind("any_", 0u) == 0u;

                if (IsAnySingleton)
                {
                    EXPECT_EQ(Value.variable_slot, HTN_GENERATED_NO_INDEX)
                        << "Generated singleton variable '" << VariableID
                        << "' unexpectedly owns generated binding storage";
                }
                else
                {
                    EXPECT_NE(Value.variable_slot, HTN_GENERATED_NO_INDEX)
                        << "Generated variable '" << VariableID
                        << "' has no compile-time slot";
                }
            }
        }

        ASSERT_NE(Domain.debug_metadata, nullptr);
        for (uint32_t ConditionIndex = 0u; ConditionIndex < Domain.debug_metadata->condition_count; ++ConditionIndex)
        {
            const HTNGeneratedDebugCondition& Condition = Domain.debug_metadata->conditions[ConditionIndex];
            if (Condition.kind == HTN_CONDITION_AXIOM)
            {
                EXPECT_NE(Condition.resolved_index, HTN_GENERATED_NO_INDEX)
                    << "Generated axiom condition " << ConditionIndex << " has no compile-time target index";
                EXPECT_LT(Condition.resolved_index, Domain.debug_metadata->axiom_count);
            }
            else if (Condition.kind == HTN_CONDITION_FACT)
            {
                EXPECT_NE(Condition.resolved_index, HTN_GENERATED_NO_INDEX)
                    << "Generated fact condition " << ConditionIndex << " has no compile-time fact slot";
                EXPECT_LT(Condition.resolved_index, Domain.debug_metadata->fact_slot_count);
            }
            else if (Condition.kind == HTN_CONDITION_CALL || Condition.kind == HTN_CONDITION_CALL_BIND)
            {
                EXPECT_NE(Condition.resolved_index, HTN_GENERATED_NO_INDEX)
                    << "Generated callterm condition " << ConditionIndex << " has no compile-time callterm slot";
                EXPECT_LT(Condition.resolved_index, Domain.debug_metadata->callterm_slot_count);
            }
        }
    }
}
#endif

TEST(HTNGeneratedCodeArchitectureTest, GeneratedDomainsDoNotReferenceGenericConditionEvaluator)
{
    const std::filesystem::path GeneratedDirectory = HTNFileHelpers::MakeAbsolutePath("HTNTest/generated");
    ASSERT_TRUE(std::filesystem::exists(GeneratedDirectory));

    // Guard against accidentally reintroducing retired generic execution layers.
    // Domain-specific generated helpers plus the focused backtracking/debug/profiling
    // ABIs remain valid; the old runtime/view/debug bridges do not.
    static const char* ForbiddenSymbols[] =
    {
        // The retired generic runtime layer must never reappear in generated C.
        "HTNGeneratedRuntime",
        "HTNGeneratedPreparedDomain",
        "prepared_domain",

        // Other retired bridges/views used different names, so keep explicit guards.
        "HTN_GENERATED_DEBUG_",
        "debug_context",
        "HTNGeneratedEnvironment_PushCheckpoint(",
        "HTNGeneratedEnvironment_CommitCheckpoint(",
        "HTNGeneratedEnvironment_RollbackCheckpoint(",
        "HTNGeneratedBacktracking_CloseEnvironmentCheckpoint(",
        "HTNGeneratedBacktracking_CaptureVariableMutationBaseline(",
        "HTNGeneratedBacktracking_CaptureContinuationMutationBaseline(",
        "HTNGeneratedBacktracking_PushPendingContinuations(",
        "HTNGeneratedBacktracking_PopPendingContinuation(",
        "prepared_storage->symbols",
        "prepared_storage->values",
        "context->runtime_view",
        "storage->view.variables",
        "storage->view.preparation_cache",
        "storage->view.resolved_slots",
        "storage->view.frame_state",
        "storage->view.continuations",
        "HTNGeneratedBacktracking_BindGeneratedStorage("
    };

    size_t GeneratedFileCount = 0u;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(GeneratedDirectory))
    {
        if (!Entry.is_regular_file() || !Entry.path().filename().string().ends_with(".generated.c"))
            continue;
        ++GeneratedFileCount;
        std::ifstream Input(Entry.path(), std::ios::binary);
        ASSERT_TRUE(Input.good()) << Entry.path().string();
        const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
        for (const char* Forbidden : ForbiddenSymbols)
        {
            EXPECT_EQ(Text.find(Forbidden), std::string::npos)
                << Entry.path().filename().string() << " still references " << Forbidden;
        }
    }
    EXPECT_GT(GeneratedFileCount, 0u);
}


TEST(HTNGeneratedCodeArchitectureTest, GeneratedDomainsDoNotAllocateDynamically)
{
    const std::filesystem::path GeneratedDirectory = HTNFileHelpers::MakeAbsolutePath("HTNTest/generated");
    ASSERT_TRUE(std::filesystem::exists(GeneratedDirectory));

    static const char* ForbiddenAllocationTokens[] =
    {
        "malloc(",
        "calloc(",
        "realloc(",
        "free(",
        "operator new",
        "new HTN",
        "std::vector",
        "std::string",
        "std::make_shared",
        "std::make_unique"
    };

    size_t GeneratedFileCount = 0u;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(GeneratedDirectory))
    {
        if (!Entry.is_regular_file() || !Entry.path().filename().string().ends_with(".generated.c"))
            continue;

        ++GeneratedFileCount;
        std::ifstream Input(Entry.path(), std::ios::binary);
        ASSERT_TRUE(Input.good()) << Entry.path().string();
        const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());

        for (const char* Forbidden : ForbiddenAllocationTokens)
        {
            EXPECT_EQ(Text.find(Forbidden), std::string::npos)
                << Entry.path().filename().string()
                << " contains a dynamic-allocation construct forbidden in generated planner source: "
                << Forbidden;
        }
    }

    EXPECT_GT(GeneratedFileCount, 0u);
}

TEST(HTNGeneratedCodeArchitectureTest, GeneratedCallTermsUsePreparedRegistryEntries)
{
    const std::filesystem::path GeneratedDirectory = HTNFileHelpers::MakeAbsolutePath("HTNTest/generated");
    ASSERT_TRUE(std::filesystem::exists(GeneratedDirectory));

    size_t CallTermFileCount = 0u;
    size_t InvocationCount = 0u;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(GeneratedDirectory))
    {
        if (!Entry.is_regular_file() || !Entry.path().filename().string().ends_with(".generated.c"))
            continue;

        std::ifstream Input(Entry.path(), std::ios::binary);
        ASSERT_TRUE(Input.good()) << Entry.path().string();
        const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
        if (Text.find("HTNCallTermRegistry_ResolveGeneratedCallTerm(") == std::string::npos)
            continue;

        ++CallTermFileCount;
        EXPECT_NE(Text.find("HTNGeneratedCallTerm callterm_slots["), std::string::npos)
            << Entry.path().filename().string() << " does not store prepared registry entries";

        size_t Invocation = 0u;
        while ((Invocation = Text.find("HTNCallTermRegistry_InvokeGeneratedCallTerm(", Invocation)) != std::string::npos)
        {
            const size_t LineEnd = Text.find('\n', Invocation);
            const std::string_view Line(Text.data() + Invocation,
                                        (LineEnd == std::string::npos ? Text.size() : LineEnd) - Invocation);
            EXPECT_NE(Line.find("&HTN_GENERATED_EXECUTION(context)->callterm_slots["), std::string_view::npos)
                << Entry.path().filename().string() << " does not invoke through a prepared callterm slot";
            EXPECT_EQ(Line.find('"'), std::string_view::npos)
                << Entry.path().filename().string() << " still passes a textual name during callterm invocation";
            ++InvocationCount;
            ++Invocation;
        }
    }

    EXPECT_GT(CallTermFileCount, 0u);
    EXPECT_GT(InvocationCount, 0u);
}

TEST(HTNGeneratedCodeArchitectureTest, PreparedAtomsUseGeneratedStaticPayloads)
{
    const std::filesystem::path GeneratedDirectory = HTNFileHelpers::MakeAbsolutePath("HTNTest/generated");
    ASSERT_TRUE(std::filesystem::exists(GeneratedDirectory));

    static const char* ForbiddenPreparedPayloadBuilders[] =
    {
        "HTNAtom_SetBool(",
        "HTNAtom_SetInt(",
        "HTNAtom_SetFloat(",
        "HTNAtom_SetSymbol(",
        "HTNAtom_SetString(",
        "HTNAtom_SetListMove(",
        "HTNAtom_PushBackListElement(",
        "HTNAtom_AssignCopy("
    };

    size_t GeneratedFileCount = 0u;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(GeneratedDirectory))
    {
        if (!Entry.is_regular_file() || !Entry.path().filename().string().ends_with(".generated.c"))
            continue;

        ++GeneratedFileCount;
        std::ifstream Input(Entry.path(), std::ios::binary);
        ASSERT_TRUE(Input.good()) << Entry.path().string();
        const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
        const size_t PrepareStart = Text.find("_INITIALIZE_PREPARED_STORAGE(void*");
        ASSERT_NE(PrepareStart, std::string::npos) << Entry.path().filename().string();
        const size_t PrepareEnd = Text.find("_PREPARE_FACTS(", PrepareStart);
        ASSERT_NE(PrepareEnd, std::string::npos) << Entry.path().filename().string();
        const std::string_view PrepareAtoms(Text.data() + PrepareStart, PrepareEnd - PrepareStart);

        for (const char* Forbidden : ForbiddenPreparedPayloadBuilders)
        {
            EXPECT_EQ(PrepareAtoms.find(Forbidden), std::string_view::npos)
                << Entry.path().filename().string()
                << " builds a compile-time prepared atom through a generic payload helper: "
                << Forbidden;
        }
    }

    EXPECT_GT(GeneratedFileCount, 0u);
}

TEST(HTNGeneratedCodeArchitectureTest, GeneratedDomainsContainNoMutableStaticStorage)
{
    const std::filesystem::path GeneratedDirectory = HTNFileHelpers::MakeAbsolutePath("HTNTest/generated");
    ASSERT_TRUE(std::filesystem::exists(GeneratedDirectory));

    size_t GeneratedFileCount = 0u;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(GeneratedDirectory))
    {
        if (!Entry.is_regular_file() || !Entry.path().filename().string().ends_with(".generated.c"))
            continue;

        ++GeneratedFileCount;
        std::ifstream Input(Entry.path());
        ASSERT_TRUE(Input.good()) << Entry.path().string();

        std::string Line;
        size_t LineNumber = 0u;
        while (std::getline(Input, Line))
        {
            ++LineNumber;
            const size_t First = Line.find_first_not_of(" \t");
            if (First == std::string::npos)
                continue;
            const std::string_view Trimmed(Line.data() + First, Line.size() - First);
            if (!Trimmed.starts_with("static "))
                continue;

            // Immutable generated metadata is intentionally shared by every planner execution.
            if (Trimmed.starts_with("static const ") || Trimmed.starts_with("static constexpr "))
                continue;

            // Internal generated functions have static linkage but contain no shared state.
            const bool IsFunction = Trimmed.find('(') != std::string_view::npos &&
                                    Trimmed.find('=') == std::string_view::npos;
            EXPECT_TRUE(IsFunction)
                << Entry.path().filename().string() << ":" << LineNumber
                << " contains mutable static storage in generated planner source: " << Line;
        }
    }

    EXPECT_GT(GeneratedFileCount, 0u);
}

TEST(HTNGeneratedCodeArchitectureTest, GeneratedDomainsDoNotMutatePlannerContextDescriptor)
{
    const std::filesystem::path GeneratedDirectory = HTNFileHelpers::MakeAbsolutePath("HTNTest/generated");
    ASSERT_TRUE(std::filesystem::exists(GeneratedDirectory));

    // Generated code may mutate objects referenced by the context (execution/world/debug state),
    // but the caller-owned context descriptor itself is read-only and can therefore be shared
    // as configuration only when its pointed-to mutable state is independently owned.
    const std::regex DirectContextAssignment(R"(context->[A-Za-z_][A-Za-z0-9_]*\s*=(?!=))");

    size_t GeneratedFileCount = 0u;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(GeneratedDirectory))
    {
        if (!Entry.is_regular_file() || !Entry.path().filename().string().ends_with(".generated.c"))
            continue;

        ++GeneratedFileCount;
        std::ifstream Input(Entry.path(), std::ios::binary);
        ASSERT_TRUE(Input.good()) << Entry.path().string();
        const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
        EXPECT_FALSE(std::regex_search(Text, DirectContextAssignment))
            << Entry.path().filename().string() << " directly mutates HTNGeneratedPlannerContext";
    }

    EXPECT_GT(GeneratedFileCount, 0u);
}

TEST(HTNGeneratedCodeArchitectureTest, GeneratedEntryPointAcceptsReadOnlyContextDescriptor)
{
    static_assert(std::is_same_v<HTNGeneratedDecomposeCallFn,
                                 HTNDecompositionStatus (*)(const HTNGeneratedPlannerContext*, const HTNAtom*, int, HTNAtom*)>);
    SUCCEED();
}

TEST(HTNGeneratedCodeArchitectureTest, GeneratedPlannerAbiDoesNotDependOnPlannerExecutionContext)
{
    const std::filesystem::path PlannerHeader = HTNFileHelpers::MakeAbsolutePath(
        "HTNFramework/src/Translator/HTNGeneratedPlanner.h");
    std::ifstream HeaderInput(PlannerHeader, std::ios::binary);
    ASSERT_TRUE(HeaderInput.good()) << PlannerHeader.string();
    const std::string HeaderText((std::istreambuf_iterator<char>(HeaderInput)),
                                 std::istreambuf_iterator<char>());
    EXPECT_EQ(HeaderText.find("HTNPlannerExecutionContext"), std::string::npos);

    const std::filesystem::path GeneratedDirectory = HTNFileHelpers::MakeAbsolutePath("HTNTest/generated");
    ASSERT_TRUE(std::filesystem::exists(GeneratedDirectory));
    size_t GeneratedFileCount = 0u;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::recursive_directory_iterator(GeneratedDirectory))
    {
        if (!Entry.is_regular_file() || !Entry.path().filename().string().ends_with(".generated.c"))
            continue;
        ++GeneratedFileCount;
        std::ifstream Input(Entry.path(), std::ios::binary);
        ASSERT_TRUE(Input.good()) << Entry.path().string();
        const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
        EXPECT_EQ(Text.find("execution_context"), std::string::npos)
            << Entry.path().filename().string() << " reaches through the host execution descriptor";
    }
    EXPECT_GT(GeneratedFileCount, 0u);
}



TEST(HTNGeneratedCodeArchitectureTest, GeneratedProfilingAccessorIsExplicitOptInOnly)
{
    const std::filesystem::path PlannerHeader = HTNFileHelpers::MakeAbsolutePath(
        "HTNFramework/src/Translator/HTNGeneratedPlanner.h");
    std::ifstream HeaderInput(PlannerHeader, std::ios::binary);
    ASSERT_TRUE(HeaderInput.good()) << PlannerHeader.string();
    const std::string HeaderText((std::istreambuf_iterator<char>(HeaderInput)),
                                 std::istreambuf_iterator<char>());

    const size_t AccessorPosition = HeaderText.find("HTNGeneratedGetExecutionProfilingFn");
    ASSERT_NE(AccessorPosition, std::string::npos);
    const size_t GuardPosition = HeaderText.rfind("#ifdef HTN_GENERATED_EXECUTION_PROFILING", AccessorPosition);
    ASSERT_NE(GuardPosition, std::string::npos);
    const size_t GuardEndPosition = HeaderText.find("#endif", GuardPosition);
    ASSERT_NE(GuardEndPosition, std::string::npos);
    EXPECT_LT(AccessorPosition, GuardEndPosition);
}

TEST(HTNGeneratedCodeArchitectureTest, GeneratedPlannerAbiUsesSingleDefinitionDescriptor)
{
    const std::filesystem::path PlannerHeader = HTNFileHelpers::MakeAbsolutePath(
        "HTNFramework/src/Translator/HTNGeneratedPlanner.h");
    std::ifstream HeaderInput(PlannerHeader, std::ios::binary);
    ASSERT_TRUE(HeaderInput.good()) << PlannerHeader.string();
    const std::string HeaderText((std::istreambuf_iterator<char>(HeaderInput)),
                                 std::istreambuf_iterator<char>());
    EXPECT_EQ(HeaderText.find("HTNGeneratedDomain"), std::string::npos);
    EXPECT_EQ(HeaderText.find("domain_id"), std::string::npos);

    const std::filesystem::path GeneratedDirectory = HTNFileHelpers::MakeAbsolutePath("HTNTest/generated");
    ASSERT_TRUE(std::filesystem::exists(GeneratedDirectory));
    size_t GeneratedFileCount = 0u;
    for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(GeneratedDirectory))
    {
        if (!Entry.is_regular_file() || !Entry.path().filename().string().ends_with(".generated.c"))
            continue;

        ++GeneratedFileCount;
        std::ifstream Input(Entry.path(), std::ios::binary);
        ASSERT_TRUE(Input.good()) << Entry.path().string();
        const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
        EXPECT_EQ(Text.find("HTNGeneratedDomain"), std::string::npos)
            << Entry.path().filename().string() << " still emits a second generated-domain descriptor";
    }
    EXPECT_GT(GeneratedFileCount, 0u);
}


TEST(HTNGeneratedCallTermTest, MissingUnreachedCallTermDoesNotPreventPlanning)
{
    HTNDatabaseHook Database;
    HTNCallTermRegistry EmptyRegistry;
    HTNCallTermBindingContext EmptyContext(EmptyRegistry);
    HTNAtomOwner Output;
    const HTNGeneratedPlannerDefinition* Definition = CreateWandererHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);

    EXPECT_EQ(RunGeneratedPlanner(*Definition, Database.GetWorldState(), EmptyContext,
                                  "run", Output),
              HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(FormatPlan(Output), (std::vector<std::string>{"!wanderer_idle"}));
}


TEST(HTNGeneratedCallTermTest, PreparedSlotKeepsDirectRegistryEntryAcrossRegistryGrowth)
{
    HTNCallTermRegistry Registry;
    Registry.Bind("direct_entry", [](const HTNCallTermArguments&) { return true; });
    HTNCallTermBindingContext BindingContext(Registry);

    const HTNGeneratedCallTerm Slot = HTNCallTermRegistry_ResolveGeneratedCallTerm(&BindingContext, "direct_entry");
    ASSERT_NE(Slot.registry_entry, nullptr);
    ASSERT_NE(Slot.name, nullptr);
    EXPECT_STREQ(Slot.name, "direct_entry");

    for (size_t Index = 0u; Index < 256u; ++Index)
    {
        Registry.Bind("additional_entry_" + std::to_string(Index),
                      [](const HTNCallTermArguments&) { return false; });
    }

    const HTNGeneratedCallTerm ResolvedAgain =
        HTNCallTermRegistry_ResolveGeneratedCallTerm(&BindingContext, "direct_entry");
    EXPECT_EQ(Slot.registry_entry, ResolvedAgain.registry_entry);

    HTNAtom Result;
    HTNAtom_Init(&Result);
    EXPECT_TRUE(HTNCallTermRegistry_InvokeGeneratedCallTerm(
        &BindingContext,
        &Slot,
        nullptr,
        0u,
        &Result));
    EXPECT_EQ(Result.type, HTN_ATOM_TYPE_BOOL);
    EXPECT_NE(Result.value.bool_value, 0u);
    HTNAtom_Destroy(&Result);
}

TEST(HTNGeneratedCallTermTest, SharedMemberBindingUsesTheDaemonFromEachPlannerHook)
{
    HTNCallTermRegistry Registry;
    ASSERT_TRUE(HTN_CALLTERM_BIND_MEMBER(
        Registry, "read_entity_value", PerEntityCallTermDaemon, ReadValue));

    HTNWorldState FirstWorldState;
    HTNWorldState SecondWorldState;
    HTNPlannerHook FirstHook(FirstWorldState, Registry);
    HTNPlannerHook SecondHook(SecondWorldState, Registry);
    PerEntityCallTermDaemon FirstDaemon{17};
    PerEntityCallTermDaemon SecondDaemon{42};
    ASSERT_TRUE(HTN_CALLTERM_SET_DAEMON(
        FirstHook.GetCallTermBindingContext(), PerEntityCallTermDaemon, &FirstDaemon));
    ASSERT_TRUE(HTN_CALLTERM_SET_DAEMON(
        SecondHook.GetCallTermBindingContext(), PerEntityCallTermDaemon, &SecondDaemon));

    const HTNGeneratedCallTerm FirstSlot = HTNCallTermRegistry_ResolveGeneratedCallTerm(
        &FirstHook.GetCallTermBindingContext(), "read_entity_value");
    const HTNGeneratedCallTerm SecondSlot = HTNCallTermRegistry_ResolveGeneratedCallTerm(
        &SecondHook.GetCallTermBindingContext(), "read_entity_value");
    ASSERT_EQ(FirstSlot.registry_entry, SecondSlot.registry_entry);

    const std::vector<HTNAtomOwner> NoArguments;
    const HTNCallTermArguments RegistryArguments(NoArguments);
    const HTNAtomOwner FirstRegistryResult = Registry.Execute(
        "read_entity_value",
        FirstHook.GetCallTermBindingContext(),
        RegistryArguments);
    const HTNAtomOwner SecondRegistryResult = Registry.Execute(
        "read_entity_value",
        SecondHook.GetCallTermBindingContext(),
        RegistryArguments);
    ASSERT_TRUE(FirstRegistryResult.IsType<int32>());
    ASSERT_TRUE(SecondRegistryResult.IsType<int32>());
    EXPECT_EQ(FirstRegistryResult.GetValue<int32>(), 17);
    EXPECT_EQ(SecondRegistryResult.GetValue<int32>(), 42);

    HTNAtom FirstResult;
    HTNAtom SecondResult;
    HTNAtom_Init(&FirstResult);
    HTNAtom_Init(&SecondResult);
    ASSERT_TRUE(HTNCallTermRegistry_InvokeGeneratedCallTerm(
        &FirstHook.GetCallTermBindingContext(), &FirstSlot, nullptr, 0u, &FirstResult));
    ASSERT_TRUE(HTNCallTermRegistry_InvokeGeneratedCallTerm(
        &SecondHook.GetCallTermBindingContext(), &SecondSlot, nullptr, 0u, &SecondResult));
    EXPECT_EQ(FirstResult.type, HTN_ATOM_TYPE_INT);
    EXPECT_EQ(SecondResult.type, HTN_ATOM_TYPE_INT);
    EXPECT_EQ(FirstResult.value.int_value, 17);
    EXPECT_EQ(SecondResult.value.int_value, 42);
    HTNAtom_Destroy(&FirstResult);
    HTNAtom_Destroy(&SecondResult);
}

TEST(HTNPlannerHookTest, RetainsItsWorldStateForConstAndNonConstAccess)
{
    HTNWorldState WorldState;
    HTNPlannerHook PlannerHook(WorldState);

    EXPECT_EQ(&PlannerHook.GetWorldState(), &WorldState);

    const HTNPlannerHook& ConstPlannerHook = PlannerHook;
    EXPECT_EQ(&ConstPlannerHook.GetWorldState(), &WorldState);
}

TEST(HTNGeneratedCallTermTest, MissingDaemonFailsOnInvocationAndCanRecover)
{
    HTNCallTermRegistry Registry;
    ASSERT_TRUE(HTN_CALLTERM_BIND_MEMBER(
        Registry, "read_entity_value", PerEntityCallTermDaemon, ReadValue));
    HTNWorldState WorldState;
    HTNPlannerHook PlannerHook(WorldState, Registry);

    const HTNGeneratedCallTerm Slot = HTNCallTermRegistry_ResolveGeneratedCallTerm(
        &PlannerHook.GetCallTermBindingContext(), "read_entity_value");
    ASSERT_NE(Slot.registry_entry, nullptr);

    HTNAtom Result;
    HTNAtom_Init(&Result);
    EXPECT_FALSE(HTNCallTermRegistry_InvokeGeneratedCallTerm(
        &PlannerHook.GetCallTermBindingContext(), &Slot, nullptr, 0u, &Result));
    EXPECT_EQ(Result.type, HTN_ATOM_TYPE_UNBOUND);

    PerEntityCallTermDaemon Daemon{73};
    ASSERT_TRUE(HTN_CALLTERM_SET_DAEMON(
        PlannerHook.GetCallTermBindingContext(), PerEntityCallTermDaemon, &Daemon));
    ASSERT_TRUE(HTNCallTermRegistry_InvokeGeneratedCallTerm(
        &PlannerHook.GetCallTermBindingContext(), &Slot, nullptr, 0u, &Result));
    EXPECT_EQ(Result.type, HTN_ATOM_TYPE_INT);
    EXPECT_EQ(Result.value.int_value, 73);
    HTNAtom_Destroy(&Result);
}

TEST(HTNGeneratedCallTermTest, DaemonTypeCapacityIsCheckedDuringGlobalRegistration)
{
    HTNCallTermRegistry Registry;
    const HTNCallTermFunction Function = [](
        void*, const HTNCallTermArguments&) -> HTNAtomOwner
    {
        return HTNAtomOwner(true);
    };

    for (std::size_t Index = 0u; Index < HTN_MAX_CALLTERM_DAEMON_TYPES; ++Index)
    {
        EXPECT_TRUE(Registry.BindMember(
            "callterm_" + std::to_string(Index),
            "daemon_" + std::to_string(Index),
            Function,
            {}));
    }

    EXPECT_FALSE(Registry.BindMember(
        "overflow_callterm", "overflow_daemon", Function, {}));
}


TEST(HTNGeneratedCallTermTest, MissingReachedCallTermReturnsNoPlan)
{
    HTNDatabaseHook Database;
    ASSERT_TRUE(Database.ParseWorldStateFile(MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName, "callterms",
        HTNFileHelpers::kWorldStateFileExtension)));
    HTNCallTermRegistry EmptyRegistry;
    HTNCallTermBindingContext EmptyContext(EmptyRegistry);
    HTNAtomOwner Output;
    const HTNGeneratedPlannerDefinition* Definition = CreateCalltermsHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);

    EXPECT_EQ(RunGeneratedPlanner(*Definition, Database.GetWorldState(), EmptyContext,
                                  "test_callterms", Output),
              HTN_DECOMPOSITION_NO_PLAN);
    EXPECT_TRUE(Output.IsType<HTNAtomList>());
    EXPECT_TRUE(Output.IsListEmpty());
}


TEST(HTNGeneratedEntryPointTest, TopLevelMethodIsSelectedByInternedSymbol)
{
    const HTNGeneratedPlannerDefinition* Definition = FindGeneratedDomain("CallTermsDemo");
    ASSERT_NE(Definition, nullptr);
    ASSERT_NE(Definition->decompose_call, nullptr);

    HTNDatabaseHook Database;
    const std::string WorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "callterms",
        HTNFileHelpers::kWorldStateFileExtension);
    ASSERT_TRUE(Database.ParseWorldStateFile(WorldStatePath));

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNPlannerHook PlannerHook(Database.GetWorldState(), CallTermRegistry);

    std::vector<std::string> Plan;
    const HTNDecompositionStatus ValidMethodResult = RunGeneratedPlanner(
        *Definition,
        Database.GetWorldState(),
        PlannerHook.GetCallTermBindingContext(),
        "test_callterms",
        Plan);
    EXPECT_EQ(ValidMethodResult, HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_FALSE(Plan.empty());

    Plan.clear();
    const HTNDecompositionStatus MissingMethodResult = RunGeneratedPlanner(
        *Definition,
        Database.GetWorldState(),
        PlannerHook.GetCallTermBindingContext(),
        "method_that_does_not_exist",
        Plan);
    EXPECT_EQ(MissingMethodResult, HTN_DECOMPOSITION_INVALID_CALL);
}



TEST(HTNPlanningUnitOwnershipTest, GeneratedCompletedResultSurvivesContextDestructionAndActivePlanChanges)
{
    HTNDatabaseHook Database;
    HTNPlannerHook Hook(Database.GetWorldState());
    ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(CreateCalltermsHTN_GetDefinition()));
    HTNPlanningUnit Unit(Database, Hook, "echo_top_level");
    const HtnSymbol* Method = HtnSymbol::sGetSymbol("echo_top_level");
    const std::string Payload = "owned payload longer than the inline string capacity";

    ASSERT_EQ(Unit.DecomposeTopLevelMethod(Method, 7, Payload, 11), HTN_DECOMPOSITION_SUCCEEDED);
    const HTNAtomOwner FirstResult = Unit.GetLastDecomposition().GetResult();
    const std::vector<std::string> Expected{"!top_level_args 7 \"" + Payload + "\" 11"};
    EXPECT_EQ(FormatPlan(FirstResult), Expected);
    ASSERT_EQ(Unit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::TaskReady);
    Unit.CompleteCurrentPrimitiveTask();
    EXPECT_EQ(Unit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::PlanCompleted);
    Unit.ClearCurrentPlan();
    EXPECT_EQ(Unit.GetLastDecomposition().GetResult(), FirstResult);

    ASSERT_EQ(Unit.DecomposeTopLevelMethod(Method, 8, std::string("second"), 12),
              HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(FormatPlan(Unit.GetLastDecomposition().GetResult()),
              (std::vector<std::string>{"!top_level_args 8 \"second\" 12"}));
    EXPECT_EQ(FormatPlan(FirstResult), Expected);
}


TEST(HTNGeneratedConcurrencyTest, PerEntityStateAndDaemonsRemainIsolatedUnderStress)
{
    constexpr size_t WorkerCount = 8u;
    constexpr size_t IterationCount = 128u;

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    const HTNGeneratedPlannerDefinition* Definition = CreateCalltermsHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);
    const HtnSymbol* TopLevelMethod = HtnSymbol::sGetSymbol(
        "callterm_creates_fact_visible_immediately");

    std::vector<std::unique_ptr<HTNDatabaseHook>> WorkerDatabases;
    std::vector<std::unique_ptr<HTNPlannerHook>> WorkerPlannerHooks;
    std::vector<std::unique_ptr<TestWorldStateCallTermDaemon>> WorkerDaemons;
    std::vector<std::unique_ptr<HTNPlanningUnit>> WorkerPlanningUnits;
    WorkerDatabases.reserve(WorkerCount);
    WorkerPlannerHooks.reserve(WorkerCount);
    WorkerDaemons.reserve(WorkerCount);
    WorkerPlanningUnits.reserve(WorkerCount);

    for (size_t WorkerIndex = 0u; WorkerIndex < WorkerCount; ++WorkerIndex)
    {
        auto Database = std::make_unique<HTNDatabaseHook>();
        auto PlannerHook = std::make_unique<HTNPlannerHook>(
            Database->GetWorldState(), CallTermRegistry);
        ASSERT_TRUE(PlannerHook->SetGeneratedPlannerDefinition(Definition));

        auto Daemon = std::make_unique<TestWorldStateCallTermDaemon>(
            *PlannerHook,
            "enemy" + std::to_string(WorkerIndex));
        ASSERT_TRUE(HTN_CALLTERM_SET_DAEMON(
            PlannerHook->GetCallTermBindingContext(),
            TestWorldStateCallTermDaemon,
            Daemon.get()));

        auto PlanningUnit = std::make_unique<HTNPlanningUnit>(
            *Database,
            *PlannerHook,
            TopLevelMethod);

        WorkerDatabases.emplace_back(std::move(Database));
        WorkerPlannerHooks.emplace_back(std::move(PlannerHook));
        WorkerDaemons.emplace_back(std::move(Daemon));
        WorkerPlanningUnits.emplace_back(std::move(PlanningUnit));
    }

    for (size_t Left = 0u; Left < WorkerCount; ++Left)
    {
        ASSERT_NE(WorkerPlannerHooks[Left]->GetGeneratedPreparedStorage(), nullptr);
        for (size_t Right = Left + 1u; Right < WorkerCount; ++Right)
        {
            EXPECT_NE(WorkerPlannerHooks[Left]->GetGeneratedPreparedStorage(),
                      WorkerPlannerHooks[Right]->GetGeneratedPreparedStorage());
        }
    }

    std::atomic<size_t> ReadyCount{0u};
    std::atomic<bool> Start{false};
    std::atomic<size_t> FailureCount{0u};
    std::vector<std::thread> Workers;
    Workers.reserve(WorkerCount);

    for (size_t WorkerIndex = 0u; WorkerIndex < WorkerCount; ++WorkerIndex)
    {
        HTNPlanningUnit* PlanningUnit = WorkerPlanningUnits[WorkerIndex].get();
        HTNDatabaseHook* Database = WorkerDatabases[WorkerIndex].get();
        Workers.emplace_back(
            [PlanningUnit, Database, WorkerIndex, &ReadyCount, &Start, &FailureCount]()
            {
                const std::vector<std::string> ExpectedPlan{
                    "!target_available_visible \"enemy" + std::to_string(WorkerIndex) + "\""};

                ReadyCount.fetch_add(1u, std::memory_order_release);
                while (!Start.load(std::memory_order_acquire))
                    std::this_thread::yield();

                for (size_t Iteration = 0u; Iteration < IterationCount; ++Iteration)
                {
                    Database->GetWorldState().RemoveAllFacts();
                    if (PlanningUnit->DecomposeTopLevelMethod() != HTN_DECOMPOSITION_SUCCEEDED ||
                        FormatPlan(PlanningUnit->GetLastDecomposition().GetResult()) != ExpectedPlan)
                    {
                        FailureCount.fetch_add(1u, std::memory_order_relaxed);
                    }
#ifdef HTN_GENERATED_EXECUTION_PROFILING
                    if (PlanningUnit->GetLastGeneratedTimingBreakdown().ExecutionStorageCreated !=
                        (Iteration == 0u))
                    {
                        FailureCount.fetch_add(1u, std::memory_order_relaxed);
                    }
#endif
                }
            });
    }

    while (ReadyCount.load(std::memory_order_acquire) != WorkerCount)
        std::this_thread::yield();
    Start.store(true, std::memory_order_release);

    for (std::thread& Worker : Workers)
        Worker.join();

    EXPECT_EQ(FailureCount.load(std::memory_order_relaxed), 0u);
}

TEST(HTNGeneratedCodeArchitectureTest, SelfTailRecursiveTasksUseFrameReuseFastPath)
{
    const std::filesystem::path GeneratedPath =
        HTNFileHelpers::MakeAbsolutePath("HTNTest/generated/complex_scenario.generated.c");
    ASSERT_TRUE(std::filesystem::exists(GeneratedPath));

    std::ifstream Input(GeneratedPath, std::ios::binary);
    ASSERT_TRUE(Input.good());
    const std::string Text((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
    EXPECT_NE(Text.find("HTN_GENERATED_EXECUTION(context)->variables.bound_mask[("), std::string::npos)
        << "recursive complex scenario did not discard dead caller slots directly in generated storage";
    EXPECT_NE(Text.find("HTN_GENERATED_EXECUTION(context)->current_variable_frame_id = HTN_GENERATED_EXECUTION(context)->next_variable_frame_id++;"), std::string::npos)
        << "recursive complex scenario did not enter the replacement variable frame";
}

TEST(HTNPublicApiArchitectureTest, UsesConstOverloadsAndGenericResultTerminology)
{
    static_assert(std::is_same_v<
        decltype(std::declval<HTNDatabaseHook&>().GetWorldState()),
        HTNWorldState&>);
    static_assert(std::is_same_v<
        decltype(std::declval<const HTNDatabaseHook&>().GetWorldState()),
        const HTNWorldState&>);
    const std::filesystem::path SourceRoot =
        HTNFileHelpers::MakeAbsolutePath("HTNFramework/src");
    const std::filesystem::path IntegrationRoot =
        HTNFileHelpers::MakeAbsolutePath("HTNIntegration/src");
    const std::vector<std::filesystem::path> PublicHeaders{
        SourceRoot / "HTNPlanner.h",
        SourceRoot / "Core/HTNAtomCpp.h",
        SourceRoot / "Core/HTNAtomOwner.h",
        IntegrationRoot / "HTNIntegration.h",
        IntegrationRoot / "Hook/HTNDatabaseHook.h",
        IntegrationRoot / "Hook/HTNPlannerHook.h",
        IntegrationRoot / "Hook/HTNPlanningUnit.h"};

    const std::regex MutableGetter(R"(\bGet[A-Za-z0-9_]*Mutable\s*\()");
    for (const std::filesystem::path& Header : PublicHeaders)
    {
        ASSERT_TRUE(std::filesystem::exists(Header)) << Header.string();
        std::ifstream Input(Header, std::ios::binary);
        ASSERT_TRUE(Input.good()) << Header.string();
        const std::string Text(
            (std::istreambuf_iterator<char>(Input)),
            std::istreambuf_iterator<char>());
        EXPECT_FALSE(std::regex_search(Text, MutableGetter)) << Header.string();
    }

}

std::string EquivalenceCaseName(const testing::TestParamInfo<HTNEquivalenceCase>& inInfo)
{
    return inInfo.param.TestName;
}

INSTANTIATE_TEST_CASE_P(
    AtomList,
    HTNGeneratedEquivalenceTest,
    testing::Values(
        HTNEquivalenceCase{"ConstantList", "atom_list_demo", "atom_list_demo", "AtomListDemo", "show_atom_list"},
        HTNEquivalenceCase{"SplitListBasic", "atom_list_demo", "atom_list_demo", "AtomListDemo", "split_list_basic"},
        HTNEquivalenceCase{"SplitListSingle", "atom_list_demo", "atom_list_demo", "AtomListDemo", "split_list_single_element"},
        HTNEquivalenceCase{"SplitListEmpty", "atom_list_demo", "atom_list_demo", "AtomListDemo", "split_list_empty_fails"},
        HTNEquivalenceCase{"SplitListBoundOutputs", "atom_list_demo", "atom_list_demo", "AtomListDemo", "split_list_bound_outputs"},
        HTNEquivalenceCase{"SplitListRollback", "atom_list_demo", "atom_list_demo", "AtomListDemo", "split_list_rollback"},
        HTNEquivalenceCase{"SplitListFrontBasic", "atom_list_demo", "atom_list_demo", "AtomListDemo", "split_list_front_basic"},
        HTNEquivalenceCase{"SplitListBackBasic", "atom_list_demo", "atom_list_demo", "AtomListDemo", "split_list_back_basic"},
        HTNEquivalenceCase{"SplitListBackBoundOutputs", "atom_list_demo", "atom_list_demo", "AtomListDemo", "split_list_back_bound_outputs"}),
    EquivalenceCaseName);

INSTANTIATE_TEST_CASE_P(
    AAACombatNPC,
    HTNGeneratedEquivalenceTest,
    testing::Values(
        HTNEquivalenceCase{"HighOrder", "AAACombatNPC_high_order", "AAACombatNPC", "AAACombatNPC", "run"},
        HTNEquivalenceCase{"Melee", "AAACombatNPC_melee", "AAACombatNPC", "AAACombatNPC", "run"},
        HTNEquivalenceCase{"Ranged", "AAACombatNPC_ranged", "AAACombatNPC", "AAACombatNPC", "run"},
        HTNEquivalenceCase{"MediumOrder", "AAACombatNPC_medium_order", "AAACombatNPC", "AAACombatNPC", "run"},
        HTNEquivalenceCase{"Investigation", "AAACombatNPC_investigation", "AAACombatNPC", "AAACombatNPC", "run"},
        HTNEquivalenceCase{"Search", "AAACombatNPC_search", "AAACombatNPC", "AAACombatNPC", "run"},
        HTNEquivalenceCase{"LowOrder", "AAACombatNPC_low_order", "AAACombatNPC", "AAACombatNPC", "run"},
        HTNEquivalenceCase{"Idle", "AAACombatNPC_idle", "AAACombatNPC", "AAACombatNPC", "run"}),
    EquivalenceCaseName);

INSTANTIATE_TEST_CASE_P(
    ComplexScenario,
    HTNGeneratedEquivalenceTest,
    testing::Values(
        HTNEquivalenceCase{"Combat", "complex_scenario_combat", "complex_scenario", "ComplexScenario", "run_scenario"},
        HTNEquivalenceCase{"Emergency", "complex_scenario_emergency", "complex_scenario", "ComplexScenario", "run_scenario"},
        HTNEquivalenceCase{"Idle", "complex_scenario_idle", "complex_scenario", "ComplexScenario", "run_scenario"},
        HTNEquivalenceCase{"Mobility", "complex_scenario_mobility", "complex_scenario", "ComplexScenario", "run_scenario"},
        HTNEquivalenceCase{"Recovery", "complex_scenario_recovery", "complex_scenario", "ComplexScenario", "run_scenario"},
        HTNEquivalenceCase{"Recursive100Entities", "complex_scenario_recursive_100", "complex_scenario", "ComplexScenario", "run_scenario"}),
    EquivalenceCaseName);

INSTANTIATE_TEST_CASE_P(
    CallTerms,
    HTNGeneratedEquivalenceTest,
    testing::Values(
        HTNEquivalenceCase{"BasicCallTerms", "callterms", "callterms", "CallTermsDemo", "test_callterms"},
        HTNEquivalenceCase{"CallTermCreatesFactVisibleImmediately", "callterms", "callterms", "CallTermsDemo", "callterm_creates_fact_visible_immediately"},
        HTNEquivalenceCase{"CallTermWorldStateMutationSurvivesBacktracking", "callterms", "callterms", "CallTermsDemo", "callterm_world_state_mutation_survives_backtracking"}),
    EquivalenceCaseName);

INSTANTIATE_TEST_CASE_P(
    NestedCalls,
    HTNGeneratedEquivalenceTest,
    testing::Values(
        HTNEquivalenceCase{"NestedCalls", "callterms", "nested_calls", "NestedCallsDemo", "test_nested_calls"}),
    EquivalenceCaseName);

INSTANTIATE_TEST_CASE_P(
    NumericExpressions,
    HTNGeneratedEquivalenceTest,
    testing::Values(
        HTNEquivalenceCase{"NestedAndMixedArithmetic", "numeric_expressions", "numeric_expressions", "NumericExpressionsDemo", "run"},
        HTNEquivalenceCase{"DivisionByZero", "numeric_expressions", "numeric_expressions", "NumericExpressionsDemo", "division_by_zero"},
        HTNEquivalenceCase{"InvalidOperandType", "numeric_expressions", "numeric_expressions", "NumericExpressionsDemo", "invalid_operand_type"}),
    EquivalenceCaseName);

INSTANTIATE_TEST_CASE_P(
    Human,
    HTNGeneratedEquivalenceTest,
    testing::Values(
        HTNEquivalenceCase{"Main", "human", "human", "Human", "behave"},
        HTNEquivalenceCase{"AxiomBacktracking", "human_axiom_backtracking", "human", "Human", "behave"}),
    EquivalenceCaseName);
}


#ifdef HTN_DEBUG_DECOMPOSITION
TEST(HTNGeneratedDomainMetadataTest, PrimitiveTaskHeadsAreStoredExplicitlyWithBangPrefix)
{
    const HTNGeneratedPlannerDefinition* Definition = CreateComplexScenarioHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);
    const HTNGeneratedPlannerDefinition& Domain = *Definition;

    ASSERT_NE(Domain.debug_metadata, nullptr);
    for (uint32_t TaskIndex = 0u; TaskIndex < Domain.debug_metadata->task_count; ++TaskIndex)
    {
        const HTNGeneratedDebugTask& DebugTask = Domain.debug_metadata->tasks[TaskIndex];
        if (DebugTask.kind != HTN_TASK_PRIMITIVE)
            continue;

        ASSERT_LT(DebugTask.plan_step_head_string_id, Domain.debug_metadata->string_count);
        ASSERT_LT(DebugTask.id, Domain.debug_metadata->string_count);
        const std::string PrimitiveHead = Domain.debug_metadata->strings[DebugTask.plan_step_head_string_id];
        EXPECT_FALSE(PrimitiveHead.empty());
        EXPECT_EQ(PrimitiveHead.front(), HTNPrimitiveTaskPrefix);
        EXPECT_EQ(PrimitiveHead,
                  std::string(1u, HTNPrimitiveTaskPrefix) + Domain.debug_metadata->strings[DebugTask.id]);
    }
}
#endif

TEST(HTNGeneratedPlannerHookTest, PlanningUnitUsesGeneratedBackendWhenDefinitionIsConfigured)
{
    const std::string WorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "complex_scenario_combat",
        HTNFileHelpers::kWorldStateFileExtension);
    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "complex_scenario",
        HTNFileHelpers::kDomainFileExtension);

    HTNDatabaseHook Database;
    ASSERT_TRUE(Database.ParseWorldStateFile(WorldStatePath));

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNPlannerHook PlannerHook(Database.GetWorldState(), CallTermRegistry);

    const HTNGeneratedPlannerDefinition* Definition =
        CreateComplexScenarioHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);
    EXPECT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(Definition));
    ASSERT_TRUE(PlannerHook.HasGeneratedPlannerDefinition());

    HTNPlanningUnit PlanningUnit(
        Database,
        PlannerHook,
        HtnSymbol::sGetSymbol("run_scenario"));

    ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_FALSE(PlanningUnit.GetLastDecomposition().GetResult().IsListEmpty());
}

TEST(HTNPublicApiArchitectureTest, DecompositionStatusKeepsNumericValuesAndPlanningReturnTypes)
{
    static_assert(HTN_DECOMPOSITION_SUCCEEDED == 0);
    static_assert(HTN_DECOMPOSITION_NO_PLAN == 1);
    static_assert(HTN_DECOMPOSITION_BACKTRACKING_CAPACITY_EXCEEDED == 2);
    static_assert(HTN_DECOMPOSITION_OUT_OF_MEMORY == 3);
    static_assert(HTN_DECOMPOSITION_INVALID_CONTEXT == 4);
    static_assert(HTN_DECOMPOSITION_INVALID_CALL == 5);
    static_assert(HTN_DECOMPOSITION_PREPARATION_FAILED == 6);
    static_assert(HTN_DECOMPOSITION_NOT_RUN == 7);

    static_assert(std::is_same_v<
        decltype(std::declval<HTNPlanningUnit&>().DecomposeTopLevelMethod()),
        HTNDecompositionStatus>);
    static_assert(std::is_same_v<
        decltype(std::declval<HTNPlanningUnit&>().DecomposeTopLevelMethod(
            std::declval<const HTNAtom&>())), HTNDecompositionStatus>);
    static_assert(std::is_same_v<
        decltype(std::declval<HTNPlanningUnit&>().DecomposeTopLevelMethod(
            std::declval<const HtnSymbol*>())), HTNDecompositionStatus>);
    static_assert(std::is_same_v<
        decltype(std::declval<HTNPlanningUnit&>().DecomposeTopLevelMethod(
            std::declval<const HtnSymbol*>(), 42)), HTNDecompositionStatus>);
    static_assert(std::is_same_v<HTNGeneratedDecomposeCallFn,
        HTNDecompositionStatus (*)(const HTNGeneratedPlannerContext*, const HTNAtom*, int, HTNAtom*)>);
}

TEST(HTNGeneratedPlannerAbiTest, GeneratedDefinitionReportsTheRuntimeAbiVersion)
{
    static_assert(offsetof(HTNGeneratedPlannerDefinition, abi_version) == 0u);

    const HTNGeneratedPlannerDefinition* Definition =
        CreateComplexScenarioHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);
    EXPECT_EQ(Definition->abi_version, HTN_GENERATED_PLANNER_ABI_VERSION);
}

TEST(HTNGeneratedPlannerHookTest, RegistersCompiledFactsWithoutParsingTheDomain)
{
    HTNDatabaseHook Database;
    HTNPlannerHook Hook(Database.GetWorldState());
    ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(CreateComplexScenarioHTN_GetDefinition()));
    EXPECT_NE(Hook.FindFactSlot(HtnSymbol::sGetSymbol("controlled_entity")), HTN_INVALID_FACT_SLOT);
    EXPECT_EQ(Hook.FindFactSlot(HtnSymbol::sGetSymbol("unrelated_fact")), HTN_INVALID_FACT_SLOT);
    Database.GetWorldState().SetFactRegistry(&Hook.GetFactRegistry());
    EXPECT_TRUE(Database.GetWorldState().WriteFact(HtnSymbol::sGetSymbol("controlled_entity"), 7));
}

TEST(HTNGeneratedPlannerAbiTest, IncompatibleDefinitionIsRejectedWithoutReplacingTheActiveDefinition)
{
    HTNDatabaseHook Database;
    HTNPlannerHook PlannerHook(Database.GetWorldState());

    const HTNGeneratedPlannerDefinition* Definition =
        CreateComplexScenarioHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);
    ASSERT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(Definition));
    const void* PreparedStorage = PlannerHook.GetGeneratedPreparedStorage();
    ASSERT_NE(PreparedStorage, nullptr);

    HTNGeneratedPlannerDefinition IncompatibleDefinition = *Definition;
    // Before ABI versioning, runtime-backtracking features occupied the first word.
    // That legacy value must never be accepted as a current ABI identifier.
    IncompatibleDefinition.abi_version = HTN_GENERATED_FEATURE_RUNTIME_BACKTRACKING;

    EXPECT_FALSE(PlannerHook.SetGeneratedPlannerDefinition(&IncompatibleDefinition));
    EXPECT_EQ(PlannerHook.GetGeneratedPlannerDefinition(), Definition);
    EXPECT_EQ(PlannerHook.GetGeneratedPreparedStorage(), PreparedStorage);
}

TEST(HTNGeneratedPlannerAbiTest, InvalidLifecycleContractIsRejectedAndNullClearsTheBackend)
{
    HTNDatabaseHook Database;
    HTNPlannerHook PlannerHook(Database.GetWorldState());

    const HTNGeneratedPlannerDefinition* Definition =
        CreateComplexScenarioHTN_GetDefinition();
    ASSERT_NE(Definition, nullptr);

    HTNGeneratedPlannerDefinition InvalidDefinition = *Definition;
    InvalidDefinition.initialize_prepared_storage = nullptr;
    EXPECT_FALSE(PlannerHook.SetGeneratedPlannerDefinition(&InvalidDefinition));
    EXPECT_FALSE(PlannerHook.HasGeneratedPlannerDefinition());

    ASSERT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(Definition));
    EXPECT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(nullptr));
    EXPECT_FALSE(PlannerHook.HasGeneratedPlannerDefinition());
    EXPECT_EQ(PlannerHook.GetGeneratedPreparedStorage(), nullptr);
}

TEST(HTNGeneratedPlannerHookTest, CommonExecutionContextCarriesTopLevelCall)
{
    HTNDatabaseHook Database;
    HTNPlannerHook PlannerHook(Database.GetWorldState());
    HTNAtom Call = HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("behave"));
    ASSERT_TRUE(HTNAtom_IsBound(&Call));

    HTNPlannerExecutionContext ExecutionContext{
        &Database.GetWorldState(),
        &PlannerHook.GetCallTermBindingContext(),
        &Call,
        nullptr,
        HTN_BACKTRACKING_ALL,
        nullptr};

    EXPECT_EQ(ExecutionContext.Call, &Call);
    EXPECT_EQ(HTNGetCallHead(ExecutionContext.Call), HtnSymbol::sGetSymbol("behave"));
    HTNAtom::sDestroy(Call);
}

TEST(HTNPlanningUnitCallTest, GeneratedBindsTopLevelArguments)
{
    HTNAtomLifetimeBalanceScope LifetimeBalance;
    HTNCallTermRegistry Registry;
    BindTestCallTerms(Registry);
    HTNDatabaseHook Database;
    HTNPlannerHook Hook(Database.GetWorldState(), Registry);
    ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(CreateCalltermsHTN_GetDefinition()));
    HTNPlanningUnit Unit(Database, Hook, "echo_top_level");
    const HtnSymbol* Method = HtnSymbol::sGetSymbol("echo_top_level");
    const HtnSymbol* Role = HtnSymbol::sGetSymbol("striker");
    HTNAtom Call = HTNAtom::sCreateCall(Method, int32{7}, Role, std::string("role_context"));
    ASSERT_TRUE(HTNAtom_IsBound(&Call));
    EXPECT_EQ(Unit.DecomposeTopLevelMethod(Call), HTN_DECOMPOSITION_SUCCEEDED);
    HTNAtom::sDestroy(Call);
    EXPECT_EQ(FormatPlan(Unit.GetLastDecomposition().GetResult()),
              (std::vector<std::string>{"!top_level_args 7 striker \"role_context\""}));
}

TEST(HTNPlanningUnitCallTest, GeneratedRejectsInvalidTopLevelCallAndRecovers)
{
    HTNCallTermRegistry Registry;
    BindTestCallTerms(Registry);
    HTNDatabaseHook Database;
    HTNPlannerHook Hook(Database.GetWorldState(), Registry);
    ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(CreateCalltermsHTN_GetDefinition()));
    HTNPlanningUnit Unit(Database, Hook, "echo_top_level");

    HTNAtom WrongArity = HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("echo_top_level"), int32{7});
    ASSERT_TRUE(HTNAtom_IsBound(&WrongArity));
    EXPECT_EQ(Unit.DecomposeTopLevelMethod(WrongArity), HTN_DECOMPOSITION_INVALID_CALL);
    EXPECT_TRUE(Unit.GetLastDecomposition().GetResult().IsListEmpty());
    HTNAtom::sDestroy(WrongArity);

    EXPECT_EQ(Unit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("echo_top_level"), int32{7},
                                          HtnSymbol::sGetSymbol("striker"), std::string("role_context")),
              HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(FormatPlan(Unit.GetLastDecomposition().GetResult()),
              (std::vector<std::string>{"!top_level_args 7 striker \"role_context\""}));
}


TEST(HTNGeneratedPlanningUnitTest, ReusesPlanningUnitAcrossRepeatedGeneratedExecutions)
{
    HTNAtomLifetimeBalanceScope LifetimeBalance;
    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "complex_scenario",
        HTNFileHelpers::kDomainFileExtension);
    const std::string WorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "complex_scenario_combat",
        HTNFileHelpers::kWorldStateFileExtension);

    HTNDatabaseHook Database;
    ASSERT_TRUE(Database.ParseWorldStateFile(WorldStatePath));

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNPlannerHook PlannerHook(Database.GetWorldState(), CallTermRegistry);
    EXPECT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(CreateComplexScenarioHTN_GetDefinition()));

    HTNPlanningUnit PlanningUnit(
        Database,
        PlannerHook,
        HtnSymbol::sGetSymbol("run_scenario"));

    ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    const auto FirstPlan = PlanningUnit.GetLastDecomposition();

    ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    const auto SecondPlan = PlanningUnit.GetLastDecomposition();

    EXPECT_EQ(FirstPlan.GetResult(), SecondPlan.GetResult());
}


TEST(HTNGeneratedPlanningUnitTest, PersistentRuntimeIsResetBetweenDifferentWorldStates)
{
    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "complex_scenario",
        HTNFileHelpers::kDomainFileExtension);
    const std::string CombatWorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "complex_scenario_combat",
        HTNFileHelpers::kWorldStateFileExtension);
    const std::string IdleWorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "complex_scenario_idle",
        HTNFileHelpers::kWorldStateFileExtension);

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNDatabaseHook ReusedDatabase;
    ASSERT_TRUE(ReusedDatabase.ParseWorldStateFile(CombatWorldStatePath));
    HTNPlannerHook ReusedPlannerHook(ReusedDatabase.GetWorldState(), CallTermRegistry);
    EXPECT_TRUE(ReusedPlannerHook.SetGeneratedPlannerDefinition(CreateComplexScenarioHTN_GetDefinition()));

    // First execute a different scenario through the same PlanningUnit/execution storage.
    HTNPlanningUnit ReusedPlanningUnit(
        ReusedDatabase,
        ReusedPlannerHook,
        HtnSymbol::sGetSymbol("run_scenario"));

    ASSERT_EQ(ReusedPlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);

    // Replace the world state and execute again while retaining the generated execution storage.
    ASSERT_TRUE(ReusedDatabase.ParseWorldStateFile(IdleWorldStatePath));
    ASSERT_EQ(ReusedPlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    const auto ReusedRuntimePlan = ReusedPlanningUnit.GetLastDecomposition();

    // A freshly-created PlanningUnit/execution storage on that same second world state must produce
    // exactly the same plan. This catches stale variables, pending continuations, trails,
    // axiom frames, etc. leaking from the previous invocation.
    HTNDatabaseHook FreshDatabase;
    ASSERT_TRUE(FreshDatabase.ParseWorldStateFile(IdleWorldStatePath));
    HTNPlannerHook FreshPlannerHook(FreshDatabase.GetWorldState(), CallTermRegistry);
    EXPECT_TRUE(FreshPlannerHook.SetGeneratedPlannerDefinition(CreateComplexScenarioHTN_GetDefinition()));
    HTNPlanningUnit FreshPlanningUnit(
        FreshDatabase,
        FreshPlannerHook,
        HtnSymbol::sGetSymbol("run_scenario"));

    ASSERT_EQ(FreshPlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    const auto FreshRuntimePlan = FreshPlanningUnit.GetLastDecomposition();

    EXPECT_EQ(ReusedRuntimePlan.GetResult(), FreshRuntimePlan.GetResult());
}


TEST(HTNGeneratedPlanningUnitTest, ReloadingDatabaseInvalidatesPersistentFactSlotCache)
{
    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "complex_scenario",
        HTNFileHelpers::kDomainFileExtension);
    const std::string CombatWorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "complex_scenario_combat",
        HTNFileHelpers::kWorldStateFileExtension);
    const std::string IdleWorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "complex_scenario_idle",
        HTNFileHelpers::kWorldStateFileExtension);

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNDatabaseHook ReusedDatabase;
    ASSERT_TRUE(ReusedDatabase.ParseWorldStateFile(CombatWorldStatePath));
    HTNPlannerHook PlannerHook(ReusedDatabase.GetWorldState(), CallTermRegistry);
    EXPECT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(CreateComplexScenarioHTN_GetDefinition()));

    HTNPlanningUnit ReusedPlanningUnit(
        ReusedDatabase,
        PlannerHook,
        HtnSymbol::sGetSymbol("run_scenario"));

    ASSERT_EQ(ReusedPlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);

    // Reloading keeps the HTNWorldState object's address stable but must invalidate
    // generated fact-slot pointers through the monotonically increasing storage generation.
    ASSERT_TRUE(ReusedDatabase.ParseWorldStateFile(IdleWorldStatePath));
    ASSERT_EQ(ReusedPlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    const auto ReusedPlan = ReusedPlanningUnit.GetLastDecomposition();

    HTNDatabaseHook FreshDatabase;
    ASSERT_TRUE(FreshDatabase.ParseWorldStateFile(IdleWorldStatePath));
    HTNPlanningUnit FreshPlanningUnit(
        FreshDatabase,
        PlannerHook,
        HtnSymbol::sGetSymbol("run_scenario"));

    ASSERT_EQ(FreshPlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);
    const auto FreshPlan = FreshPlanningUnit.GetLastDecomposition();

    EXPECT_EQ(ReusedPlan.GetResult(), FreshPlan.GetResult());
}


#ifdef HTN_DEBUG_DECOMPOSITION
TEST(HTNGeneratedEventDebuggerTest, NestedTaskCallsPreserveDomainExpressionsInTaskTitles)
{
    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "nested_calls",
        HTNFileHelpers::kDomainFileExtension);
    const std::string WorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "callterms",
        HTNFileHelpers::kWorldStateFileExtension);

    HTNDatabaseHook Database;
    ASSERT_TRUE(Database.ParseWorldStateFile(WorldStatePath));

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNPlannerHook PlannerHook(Database.GetWorldState(), CallTermRegistry);
    EXPECT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(CreateNestedCallsHTN_GetDefinition()));

    HTNGeneratedDebugger Debugger;
    Debugger.SetEnabled(true);

    HTNPlanningUnit PlanningUnit(
        Database,
        PlannerHook,
        HtnSymbol::sGetSymbol("test_nested_calls"));
    PlanningUnit.SetGeneratedDebugger(&Debugger);

    ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);

    bool SawCaptureExpression = false;
    bool SawConsumeExpression = false;
    for (const HTNGeneratedDebugger::Node& Node : Debugger.GetNodes())
    {
        if (Node.Kind != HTNGeneratedDebugger::NodeKind::Task)
            continue;

        EXPECT_EQ(Node.DisplayName.find("__task_call_result_"), std::string::npos)
            << "Generated debugger leaked an internal task-call temporary: " << Node.DisplayName;

        if (Node.DisplayName.find("!capture (call add (call inc 1)") != std::string::npos)
        {
            SawCaptureExpression = true;
            ASSERT_GE(Node.TitleTokens.size(), 2u);
            EXPECT_EQ(Node.TitleTokens[1].Kind, HTNGeneratedDebugger::Node::TitleTokenKind::CallExpression);
        }
        if (Node.DisplayName.find("consume_nested (call add (call inc 4)") != std::string::npos)
        {
            SawConsumeExpression = true;
            ASSERT_GE(Node.TitleTokens.size(), 2u);
            EXPECT_EQ(Node.TitleTokens[1].Kind, HTNGeneratedDebugger::Node::TitleTokenKind::CallExpression);
        }
    }

    EXPECT_TRUE(SawCaptureExpression)
        << "Generated debugger did not preserve the nested call expression written in the primitive task";
    EXPECT_TRUE(SawConsumeExpression)
        << "Generated debugger did not preserve the nested call expression written in the compound task";
}

TEST(HTNGeneratedEventDebuggerTest, PreservesConstantNamesInConditionTitles)
{
    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "atom_list_demo",
        HTNFileHelpers::kDomainFileExtension);
    const std::string WorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "atom_list_demo",
        HTNFileHelpers::kWorldStateFileExtension);

    HTNDatabaseHook Database;
    ASSERT_TRUE(Database.ParseWorldStateFile(WorldStatePath));

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNPlannerHook PlannerHook(Database.GetWorldState(), CallTermRegistry);
    EXPECT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(CreateAtomListDemoHTN_GetDefinition()));

    HTNGeneratedDebugger Debugger;
    Debugger.SetEnabled(true);

    HTNPlanningUnit PlanningUnit(
        Database,
        PlannerHook,
        HtnSymbol::sGetSymbol("split_list_basic"));
    PlanningUnit.SetGeneratedDebugger(&Debugger);

    ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);

    const HTNGeneratedDebugger::Node* SplitNode = nullptr;
    for (const HTNGeneratedDebugger::Node& Node : Debugger.GetNodes())
    {
        if (Node.Kind == HTNGeneratedDebugger::NodeKind::BuiltinListSplit)
        {
            SplitNode = &Node;
            break;
        }
    }

    ASSERT_NE(SplitNode, nullptr);
    EXPECT_EQ(SplitNode->DisplayName, "split_list @split_list_input ?head ?tail");

    const auto ConstantToken = std::find_if(
        SplitNode->TitleTokens.begin(),
        SplitNode->TitleTokens.end(),
        [](const HTNGeneratedDebugger::Node::TitleToken& Token)
        {
            return Token.Text == "@split_list_input";
        });
    ASSERT_NE(ConstantToken, SplitNode->TitleTokens.end());
    EXPECT_EQ(ConstantToken->Kind, HTNGeneratedDebugger::Node::TitleTokenKind::Constant);

    ASSERT_EQ(SplitNode->Constants.size(), 1u);
    EXPECT_EQ(SplitNode->Constants[0].Name, "@split_list_input");
    EXPECT_EQ(SplitNode->Constants[0].Value, "(one two three)");
}

TEST(HTNGeneratedEventDebuggerTest, ExplicitSplitListOperationsPreserveTheirNames)
{
    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "atom_list_demo",
        HTNFileHelpers::kDomainFileExtension);
    const std::string WorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "atom_list_demo",
        HTNFileHelpers::kWorldStateFileExtension);

    HTNDatabaseHook Database;
    ASSERT_TRUE(Database.ParseWorldStateFile(WorldStatePath));

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNPlannerHook PlannerHook(Database.GetWorldState(), CallTermRegistry);
    EXPECT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(CreateAtomListDemoHTN_GetDefinition()));

    const auto CheckTitle = [&](const char* inMethod, const char* inExpectedTitle)
    {
        HTNGeneratedDebugger Debugger;
        Debugger.SetEnabled(true);

        HTNPlanningUnit PlanningUnit(Database, PlannerHook, HtnSymbol::sGetSymbol(inMethod));
        PlanningUnit.SetGeneratedDebugger(&Debugger);
        ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);

        const auto SplitNode = std::find_if(
            Debugger.GetNodes().begin(),
            Debugger.GetNodes().end(),
            [](const HTNGeneratedDebugger::Node& inNode)
            {
                return inNode.Kind == HTNGeneratedDebugger::NodeKind::BuiltinListSplit;
            });
        ASSERT_NE(SplitNode, Debugger.GetNodes().end());
        EXPECT_EQ(SplitNode->DisplayName, inExpectedTitle);
    };

    CheckTitle("split_list_front_basic", "split_list_front @split_list_input ?element ?remainder");
    CheckTitle("split_list_back_basic", "split_list_back @split_list_input ?remainder ?element");
}

TEST(HTNGeneratedEventDebuggerTest, SplitListConditionTreePreservesSourceOrderAndCompositeStructure)
{
    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "atom_list_demo",
        HTNFileHelpers::kDomainFileExtension);
    const std::string WorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "atom_list_demo",
        HTNFileHelpers::kWorldStateFileExtension);

    auto RunAndFindBranch = [&](const char* inTopLevelMethod, const char* inBranchName,
                                HTNGeneratedDebugger& outDebugger) -> const HTNGeneratedDebugger::Node*
    {
        HTNDatabaseHook Database;
        EXPECT_TRUE(Database.ParseWorldStateFile(WorldStatePath));

        HTNCallTermRegistry CallTermRegistry;
        BindTestCallTerms(CallTermRegistry);
        HTNPlannerHook PlannerHook(Database.GetWorldState(), CallTermRegistry);
        EXPECT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(CreateAtomListDemoHTN_GetDefinition()));

        outDebugger.SetEnabled(true);
        HTNPlanningUnit PlanningUnit(Database, PlannerHook, HtnSymbol::sGetSymbol(inTopLevelMethod));
        PlanningUnit.SetGeneratedDebugger(&outDebugger);
        EXPECT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);

        for (const HTNGeneratedDebugger::Node& Node : outDebugger.GetNodes())
        {
            if (Node.Kind == HTNGeneratedDebugger::NodeKind::Branch &&
                Node.DisplayName == inBranchName && Node.Started)
                return &Node;
        }
        return nullptr;
    };

    {
        HTNGeneratedDebugger Debugger;
        const HTNGeneratedDebugger::Node* Branch =
            RunAndFindBranch("split_list_empty_fails", "unexpected", Debugger);
        ASSERT_NE(Branch, nullptr);
        ASSERT_EQ(Branch->Children.size(), 1u);

        const HTNGeneratedDebugger::Node* AndNode = Debugger.FindNode(Branch->Children[0u]);
        ASSERT_NE(AndNode, nullptr);
        ASSERT_EQ(AndNode->Kind, HTNGeneratedDebugger::NodeKind::And);
        ASSERT_EQ(AndNode->Children.size(), 2u);

        const HTNGeneratedDebugger::Node* ClearCall = Debugger.FindNode(AndNode->Children[0u]);
        const HTNGeneratedDebugger::Node* Split = Debugger.FindNode(AndNode->Children[1u]);
        ASSERT_NE(ClearCall, nullptr);
        ASSERT_NE(Split, nullptr);
        EXPECT_EQ(ClearCall->Kind, HTNGeneratedDebugger::NodeKind::CallBind);
        EXPECT_EQ(Split->Kind, HTNGeneratedDebugger::NodeKind::BuiltinListSplit);
        EXPECT_TRUE(ClearCall->Started);
        EXPECT_TRUE(Split->Started);
    }

    {
        HTNGeneratedDebugger Debugger;
        const HTNGeneratedDebugger::Node* Branch =
            RunAndFindBranch("split_list_bound_outputs", "compatible", Debugger);
        ASSERT_NE(Branch, nullptr);
        ASSERT_EQ(Branch->Children.size(), 2u);

        const HTNGeneratedDebugger::Node* AndNode = Debugger.FindNode(Branch->Children[0u]);
        const HTNGeneratedDebugger::Node* Task = Debugger.FindNode(Branch->Children[1u]);
        ASSERT_NE(AndNode, nullptr);
        ASSERT_NE(Task, nullptr);
        ASSERT_EQ(AndNode->Kind, HTNGeneratedDebugger::NodeKind::And);
        EXPECT_EQ(Task->Kind, HTNGeneratedDebugger::NodeKind::Task);
        EXPECT_EQ(Task->DisplayName, "!bound_outputs_match");
        ASSERT_EQ(AndNode->Children.size(), 1u);

        const HTNGeneratedDebugger::Node* Split = Debugger.FindNode(AndNode->Children[0u]);
        ASSERT_NE(Split, nullptr);
        EXPECT_EQ(Split->Kind, HTNGeneratedDebugger::NodeKind::BuiltinListSplit);
        EXPECT_TRUE(Split->Started);
    }

    {
        HTNGeneratedDebugger Debugger;
        const HTNGeneratedDebugger::Node* Branch =
            RunAndFindBranch("split_list_rollback", "fails_after_binding", Debugger);
        ASSERT_NE(Branch, nullptr);
        ASSERT_EQ(Branch->Children.size(), 1u);

        const HTNGeneratedDebugger::Node* AndNode = Debugger.FindNode(Branch->Children[0u]);
        ASSERT_NE(AndNode, nullptr);
        ASSERT_EQ(AndNode->Kind, HTNGeneratedDebugger::NodeKind::And);
        ASSERT_EQ(AndNode->Children.size(), 2u);

        const HTNGeneratedDebugger::Node* Split = Debugger.FindNode(AndNode->Children[0u]);
        const HTNGeneratedDebugger::Node* Comparison = Debugger.FindNode(AndNode->Children[1u]);
        ASSERT_NE(Split, nullptr);
        ASSERT_NE(Comparison, nullptr);
        EXPECT_EQ(Split->Kind, HTNGeneratedDebugger::NodeKind::BuiltinListSplit);
        EXPECT_EQ(Comparison->Kind, HTNGeneratedDebugger::NodeKind::BuiltinComparison);
        EXPECT_TRUE(Split->Started);
        EXPECT_TRUE(Comparison->Started);
    }
}

TEST(HTNGeneratedEventDebuggerTest, AxiomChoiceBacktrackingRestoresBindingsAndCompletesLifecycle)
{
    const std::string DomainPath = MakeTestFilePath(
        HTNFileHelpers::kDomainsDirectoryName,
        "human",
        HTNFileHelpers::kDomainFileExtension);
    const std::string WorldStatePath = MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName,
        "human_axiom_backtracking",
        HTNFileHelpers::kWorldStateFileExtension);

    HTNDatabaseHook Database;
    ASSERT_TRUE(Database.ParseWorldStateFile(WorldStatePath));

    HTNCallTermRegistry CallTermRegistry;
    BindTestCallTerms(CallTermRegistry);
    HTNPlannerHook PlannerHook(Database.GetWorldState(), CallTermRegistry);
    EXPECT_TRUE(PlannerHook.SetGeneratedPlannerDefinition(CreateHumanHTN_GetDefinition()));

    HTNGeneratedDebugger Debugger;
    Debugger.SetEnabled(true);

    HTNPlanningUnit PlanningUnit(
        Database,
        PlannerHook,
        HtnSymbol::sGetSymbol("behave"));
    PlanningUnit.SetGeneratedDebugger(&Debugger);

    ASSERT_EQ(PlanningUnit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);

    const std::vector<HTNGeneratedDebugger::Node>& Nodes = Debugger.GetNodes();
    ASSERT_FALSE(Nodes.empty());

    // The generated event stream must be structurally balanced. Metadata-only
    // precondition children are intentionally not Started; every node that did
    // execute, however, must receive its matching End event.
    std::uint32_t PlanCount = 0u;
    std::uint32_t StartedMethodCount = 0u;
    for (const HTNGeneratedDebugger::Node& Node : Nodes)
    {
        if (Node.Kind == HTNGeneratedDebugger::NodeKind::Plan)
        {
            ++PlanCount;
            EXPECT_EQ(Node.ParentEventNodeId, HTN_GENERATED_NO_INDEX);
            EXPECT_TRUE(Node.Completed);
            EXPECT_TRUE(Node.Succeeded);
        }

        if (Node.Kind == HTNGeneratedDebugger::NodeKind::Method && Node.Started)
            ++StartedMethodCount;

        if (Node.Started)
        {
            EXPECT_TRUE(Node.Completed)
                << "Generated event node " << Node.EventNodeId
                << " ('" << Node.DisplayName << "') started without a matching End event";
        }
    }
    EXPECT_EQ(PlanCount, 1u);
    EXPECT_GT(StartedMethodCount, 1u)
        << "The scenario should execute nested compound methods";

    // human_axiom_backtracking.worldstate deliberately makes banana the first
    // edible candidate but marks it as moldy. The planner must backtrack and
    // retry find_edible_item with orange. Seeing both bindings in the event
    // history proves that the choice point was retried rather than overwritten.
    bool SawBananaBinding = false;
    bool SawOrangeBinding = false;
    for (const HTNGeneratedDebugger::Node& Node : Nodes)
    {
        for (const HTNGeneratedDebugger::Node::VariableValue& Variable : Node.VariablesAfter)
        {
            if (Variable.Name != "food" || !Variable.Value.IsBound() || !Variable.Value.IsType<std::string>())
                continue;

            const std::string& Value = Variable.Value.GetValue<std::string>();
            SawBananaBinding = SawBananaBinding || Value == "banana";
            SawOrangeBinding = SawOrangeBinding || Value == "orange";
        }
    }

    EXPECT_TRUE(SawBananaBinding)
        << "Generated debugger did not record the first axiom-choice candidate";
    EXPECT_TRUE(SawOrangeBinding)
        << "Generated debugger did not record the candidate selected after backtracking";

    // The discarded banana binding must not leak into the committed plan.
    bool SawTakeOrange = false;
    bool SawTakeBanana = false;
    const HTNAtomOwner& Output = PlanningUnit.GetLastDecomposition().GetResult();
    const int32_t OutputCount = Output.GetListSize();
    ASSERT_GE(OutputCount, 0);
    for (int32_t OutputIndex = 0; OutputIndex < OutputCount; ++OutputIndex)
    {
        const HTNAtomOwner Task(Output.GetListElement(static_cast<uint32>(OutputIndex)));
        const HtnSymbol* Head = HTNGetTaskHead(Task);
        if (!Head || Head->GetString() != "!take" || HTNGetTaskArgumentCount(Task) != 1u)
            continue;

        const HTNAtom& Argument = HTNGetTaskArgument(Task, 0u);
        if (!HTNAtomIsBound(Argument) || !HTNAtomIsType<std::string>(Argument))
            continue;

        SawTakeOrange = SawTakeOrange || HTNAtomGetValue<std::string>(Argument) == "orange";
        SawTakeBanana = SawTakeBanana || HTNAtomGetValue<std::string>(Argument) == "banana";
    }

    EXPECT_TRUE(SawTakeOrange);
    EXPECT_FALSE(SawTakeBanana)
        << "A binding from the rejected choice leaked through rollback into the final plan";
}


TEST(HTNGeneratedDebuggerTest, BacktrackedConditionsKeepGeneratedMetadataPaths)
{
    HTNDatabaseHook Database;
    ASSERT_TRUE(Database.ParseWorldStateFile(MakeTestFilePath(
        HTNFileHelpers::kWorldStatesDirectoryName, "complex_scenario_recursive_100",
        HTNFileHelpers::kWorldStateFileExtension)));
    HTNCallTermRegistry Registry;
    BindTestCallTerms(Registry);
    HTNPlannerHook Hook(Database.GetWorldState(), Registry);
    ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(CreateComplexScenarioHTN_GetDefinition()));
    HTNGeneratedDebugger Debugger;
    Debugger.SetEnabled(true);
    HTNPlanningUnit Unit(Database, Hook, HtnSymbol::sGetSymbol("run_scenario"));
    Unit.SetGeneratedDebugger(&Debugger);
    ASSERT_EQ(Unit.DecomposeTopLevelMethod(), HTN_DECOMPOSITION_SUCCEEDED);

    const HTNGeneratedDebugger::Node* Branch = nullptr;
    for (const HTNGeneratedDebugger::Node& Node : Debugger.GetNodes())
        if (Node.Kind == HTNGeneratedDebugger::NodeKind::Branch &&
            Node.DisplayName == "branch_iterate_all_entities")
            Branch = &Node;
    ASSERT_NE(Branch, nullptr);

    const HTNGeneratedDebugger::Node* AndNode = nullptr;
    for (const std::uint32_t ChildId : Branch->Children)
    {
        const HTNGeneratedDebugger::Node* Child = Debugger.FindNode(ChildId);
        if (Child && Child->Kind == HTNGeneratedDebugger::NodeKind::And)
            AndNode = Child;
    }
    ASSERT_NE(AndNode, nullptr);
    ASSERT_GE(AndNode->Children.size(), 2u);
    const HTNGeneratedDebugger::Node* EntityCount = Debugger.FindNode(AndNode->Children[1u]);
    ASSERT_NE(EntityCount, nullptr);
    EXPECT_EQ(EntityCount->Kind, HTNGeneratedDebugger::NodeKind::Fact);
    EXPECT_TRUE(EntityCount->Started);
    EXPECT_TRUE(EntityCount->Completed);
}

#endif
