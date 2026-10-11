// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Core/HTNTask.h"
#include "Translator/HTNGeneratedDebugger.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "Translator/HTNGeneratedProfiling.h"
#include "HTNGTest.h"

extern "C" const HTNGeneratedPlannerDefinition* CreateFactRetryHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateFactRetryFixedHTN_GetDefinition(void);

namespace
{
class HTNFactRetryTest : public testing::TestWithParam<bool>
{
protected:
    HTNDatabaseHook Database;
    HTNCallTermRegistry Registry;
    HTNPlannerHook Hook{Database.GetWorldState(), Registry};
    const HTNGeneratedPlannerDefinition* Definition = nullptr;
    HTNGeneratedPlannerContext Context{};
    HTNAtomOwner Plan;

    void SetUp() override
    {
        Definition = GetParam() ? CreateFactRetryFixedHTN_GetDefinition() : CreateFactRetryHTN_GetDefinition();
        ASSERT_TRUE(Hook.SetGeneratedPlannerDefinition(Definition));
        Database.GetWorldState().SetFactRegistry(&Hook.GetFactRegistry());
        Context.world_state = &Database.GetWorldState();
        Context.callterm_binding_context = &Hook.GetCallTermBindingContext();
        Context.prepared_storage = Hook.GetGeneratedPreparedStorage();
        Context.execution_storage = ::operator new(Definition->execution_storage_size);
        ASSERT_TRUE(Definition->initialize_execution_storage(Context.execution_storage));
        Context.backtracking_mode = HTN_BACKTRACKING_ALL;
        Context.callterm_error_policy = HTNCallTermErrorPolicy::FailSilently;
    }
    void TearDown() override
    {
        Definition->destroy_execution_storage(Context.execution_storage);
        ::operator delete(Context.execution_storage);
    }
    void Event(int inID, const char* inType = "explosion")
    {
        ASSERT_TRUE(Database.GetWorldState().WriteFact(HtnSymbol::sGetSymbol("event"),
            HtnSymbol::sGetSymbol(inType), inID, static_cast<float>(inID)));
    }
    template<typename... Args> HTNDecompositionStatus Run(const char* inMethod, Args&&... inArgs)
    {
        HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol(inMethod), std::forward<Args>(inArgs)...));
        HTNAtom_Unbind(Plan.Get());
        return Definition->decompose_call(&Context, Call.Get(), 1, Plan.Get());
    }
    int Selected(int inArgument = 1) const
    {
        return HTNAtom_GetListElement(&Plan.GetListElement(0), inArgument)->value.int_value;
    }
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    uint64_t Count(HTNGeneratedStructuralEvent inEvent) const
    {
        HTNGeneratedStructuralCounters Counters{};
        EXPECT_TRUE(HTNGeneratedProfiling_GetStructuralCounters(
            Definition->get_execution_profiling(Context.execution_storage), &Counters));
        return Counters.events[inEvent];
    }
#endif
};

TEST_P(HTNFactRetryTest, NestedLatestEventHasQuadraticRowBound)
{
    constexpr int N = 16;
    for (int I = 1; I <= N; ++I) Event(I);
    for (int Attempt = 0; Attempt < 2; ++Attempt)
    {
        ASSERT_EQ(Run("select_event"), HTN_DECOMPOSITION_SUCCEEDED);
        EXPECT_EQ(Selected(), N);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
        // N outer rows, 2..N inner rows for candidates 1..N-1, then N
        // inner rows to prove absence of a newer event for the winning candidate.
        EXPECT_EQ(Count(HTN_GENERATED_STRUCTURAL_FACT_ROW_TESTED), N * (N + 1u) / 2u + 2u * N - 1u);
#endif
    }
}

TEST_P(HTNFactRetryTest, BoundFilterResumesAfterLastPhysicalRow)
{
    for (int I = 1; I <= 16; ++I) Event(I, I % 2 ? "noise" : "explosion");
    ASSERT_EQ(Run("filtered", HtnSymbol::sGetSymbol("explosion"), 14.0f), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(Selected(), 16);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    EXPECT_EQ(Count(HTN_GENERATED_STRUCTURAL_FACT_ROW_TESTED), 16u);
    EXPECT_EQ(Count(HTN_GENERATED_STRUCTURAL_FACT_CHOICE_RETRY), 7u);
#endif
}

TEST_P(HTNFactRetryTest, NestedQueriesHaveIndependentPositionsAndRestoreBindings)
{
    auto& World = Database.GetWorldState();
    const auto* Pair = HtnSymbol::sGetSymbol("pair");
    ASSERT_TRUE(World.WriteFact(Pair, 1, 2));
    ASSERT_TRUE(World.WriteFact(Pair, 2, 1));
    ASSERT_TRUE(World.WriteFact(Pair, 3, 4));
    ASSERT_TRUE(World.WriteFact(Pair, 4, 3));
    ASSERT_TRUE(World.WriteFact(HtnSymbol::sGetSymbol("accepted"), 4));
    ASSERT_EQ(Run("pairs"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(Selected(), 4);
    EXPECT_EQ(Selected(2), 3);
}

TEST_P(HTNFactRetryTest, AppendDuringRetryIsVisibleWithoutRescanningExistingRows)
{
    Event(1);
    int Calls = 0;
    Registry.Bind("append_and_accept", [&](const HTNCallTermArguments& Arguments) {
        ++Calls;
        if (Arguments[0].value.int_value == 1) { Event(2); return false; }
        return true;
    });
    ASSERT_EQ(Run("append_during_retry"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(Selected(), 2);
    EXPECT_EQ(Calls, 2);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    EXPECT_EQ(Count(HTN_GENERATED_STRUCTURAL_FACT_ROW_TESTED), 2u);
#endif
}

TEST_P(HTNFactRetryTest, ExhaustionVisitsEachRowOnce)
{
    for (int I = 1; I <= 16; ++I) Event(I);
    ASSERT_EQ(Run("exhaust"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(HTNGetCallHead(&Plan.GetListElement(0)), HtnSymbol::sGetSymbol("!fallback"));
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    EXPECT_EQ(Count(HTN_GENERATED_STRUCTURAL_FACT_ROW_TESTED), 16u);
#endif
}

TEST_P(HTNFactRetryTest, RuntimeBacktrackingModesKeepTheirSemantics)
{
    Event(1); Event(2); Event(3);
    for (auto Mode : {HTN_BACKTRACKING_NONE, HTN_BACKTRACKING_BRANCHES, HTN_BACKTRACKING_FACTS_AND_AXIOMS, HTN_BACKTRACKING_ALL})
    {
        Context.backtracking_mode = Mode;
        const bool Retry = (Mode & HTN_BACKTRACKING_FACTS_AND_AXIOMS) != 0;
        const auto Status = Run("filtered", HtnSymbol::sGetSymbol("explosion"), 2.0f);
        if (Definition->features & HTN_GENERATED_FEATURE_RUNTIME_BACKTRACKING)
        {
            EXPECT_EQ(Status, Retry ? HTN_DECOMPOSITION_SUCCEEDED : HTN_DECOMPOSITION_NO_PLAN);
            if (Retry) EXPECT_EQ(Selected(), 3);
        }
        else EXPECT_EQ(Status, HTN_DECOMPOSITION_SUCCEEDED);
    }
}

#ifdef HTN_DEBUG_DECOMPOSITION
TEST_P(HTNFactRetryTest, PendingMetadataCanBeEnteredOutOfOrderAndRetried)
{
    const auto& Metadata = *Definition->debug_metadata;
    uint32_t ParentIndex = HTN_GENERATED_NO_INDEX;
    for (uint32_t I = 0; I < Metadata.condition_count; ++I)
        if (Metadata.conditions[I].kind == HTN_CONDITION_AND && Metadata.conditions[I].child_count == 2u)
        { ParentIndex = I; break; }
    ASSERT_NE(ParentIndex, HTN_GENERATED_NO_INDEX);
    const auto& Parent = Metadata.conditions[ParentIndex];
    const auto First = Metadata.condition_child_refs[Parent.first_child_ref];
    const auto Second = Metadata.condition_child_refs[Parent.first_child_ref + 1u];
    HTNGeneratedDebugger Debugger;
    Debugger.SetEnabled(true);
    Debugger.BeginCondition(Definition, ParentIndex);
    const auto FirstId = Debugger.GetNodes()[0].Children[0];
    const auto SecondId = Debugger.GetNodes()[0].Children[1];
    Debugger.BeginCondition(Definition, Second);
    Debugger.EndCondition(Definition, nullptr, nullptr, 0u, false);
    EXPECT_FALSE(Debugger.FindNode(FirstId)->Started);
    EXPECT_TRUE(Debugger.FindNode(SecondId)->Completed);
    Debugger.BeginCondition(Definition, First);
    Debugger.EndCondition(Definition, nullptr, nullptr, 0u, true);
    EXPECT_EQ(Debugger.GetNodes()[0].Children.size(), 2u);
    EXPECT_TRUE(Debugger.FindNode(FirstId)->Succeeded);
    Debugger.BeginCondition(Definition, First);
    Debugger.EndCondition(Definition, nullptr, nullptr, 0u, false);
    ASSERT_EQ(Debugger.GetNodes()[0].Children.size(), 3u);
    EXPECT_FALSE(Debugger.FindNode(Debugger.GetNodes()[0].Children.back())->Succeeded);
    EXPECT_TRUE(Debugger.FindNode(FirstId)->Succeeded); // Original attempt still inspectable.
    Debugger.EndCondition(Definition, nullptr, nullptr, 0u, false);
}

TEST_P(HTNFactRetryTest, DebuggerKeepsEveryAttemptAndBinding)
{
    for (int I = 1; I <= 16; ++I) Event(I);
    HTNGeneratedDebugger Debugger;
    Debugger.SetEnabled(true);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    Debugger.SetPerformanceCountersEnabled(true);
#endif
    Context.debugger = &Debugger;
    ASSERT_EQ(Run("select_event"), HTN_DECOMPOSITION_SUCCEEDED);
    const auto Snapshot = Debugger.GetNodes();
    // Baseline history: preserve all attempts, not just the final winning path.
    EXPECT_EQ(Snapshot.size(), 359u);
    int Candidate = 0, FailedComparisons = 0;
    for (const auto& Node : Snapshot)
    {
        if (Node.Kind == HTNGeneratedDebugger::NodeKind::BuiltinComparison && Node.Completed && !Node.Succeeded)
            ++FailedComparisons;
        if (Node.DisplayName != "(event ?out_type ?out_event_id ?out_timestamp)" || !Node.Started) continue;
        ASSERT_TRUE(Node.Completed);
        ASSERT_TRUE(Node.Succeeded);
        ++Candidate;
        bool Found = false;
        for (const auto& Value : Node.VariablesBefore) EXPECT_NE(Value.Name, "out_event_id");
        for (const auto& Value : Node.VariablesAfter)
            if (Value.Name == "out_event_id")
            {
                Found = true;
                EXPECT_EQ(Value.Value.Get()->value.int_value, Candidate);
            }
        EXPECT_TRUE(Found);
        EXPECT_GT(Node.Source.Line, 0u);
        EXPECT_NE(Node.Source.DomainPath.find("fact_retry.domain"), std::string::npos);
    }
    EXPECT_EQ(Candidate, 16);
    EXPECT_EQ(FailedComparisons, 136);
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    EXPECT_EQ(Debugger.GetPerformanceCounters().NodesCreated, Snapshot.size());
    EXPECT_EQ(Debugger.GetPerformanceCounters().Captures, Snapshot.size() * 2u);
    // Lookup cost depends on the metadata's children, not the accumulated retries.
    EXPECT_LE(Debugger.GetPerformanceCounters().ChildrenExamined, Snapshot.size() * 2u);
#endif
    Debugger.SetEnabled(false);
    ASSERT_EQ(Run("select_event"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_TRUE(Debugger.GetNodes().empty());
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    EXPECT_EQ(Debugger.GetPerformanceCounters().Captures, 0u);
    EXPECT_EQ(Debugger.GetPerformanceCounters().AtomsCopied, 0u);
#endif
    EXPECT_EQ(Snapshot.size(), 359u); // Client snapshot survives reset/another decomposition.
}
#endif

INSTANTIATE_TEST_SUITE_P(StoragePolicies, HTNFactRetryTest, testing::Bool());
}
