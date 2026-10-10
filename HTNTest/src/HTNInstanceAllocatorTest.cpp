// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNAtomListAllocator.h"
#include "Core/HTNAtomListOwner.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "Translator/HTNGeneratedDebugger.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "HTNGTest.h"

#include <atomic>
#include <limits>
#include <thread>
#include <unordered_set>

extern "C" const HTNGeneratedPlannerDefinition* CreateInstanceAllocatorHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateInstanceAllocatorNoneHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateInstanceAllocatorOverflowHTN_GetDefinition(void);

namespace
{
class TrackingAllocator final : public HTNAtomListAllocator
{
public:
    explicit TrackingAllocator(uint32 Capacity = 4096) : Pool(Capacity) {}
    HTNAtomNode* Allocate() override
    {
        if (Attempts++ == FailAt) return nullptr;
        HTNAtomNode* Node = Pool.Allocate();
        if (Node) { EXPECT_TRUE(Live.insert(Node).second); ++Allocations; }
        return Node;
    }
    void Deallocate(HTNAtomNode* Node) override
    {
        if (!Node) return;
        if (Live.erase(Node) != 1) { ++ForeignReleases; ADD_FAILURE() << "Wrong allocator for list node"; return; }
        ++Releases;
        Pool.Deallocate(Node);
    }
    HTNPooledAtomListAllocator Pool;
    std::unordered_set<HTNAtomNode*> Live;
    size_t Attempts = 0, Allocations = 0, Releases = 0, ForeignReleases = 0;
    size_t FailAt = std::numeric_limits<size_t>::max();
};

class TrackingHeap final : public HTNAtomListAllocator
{
public:
    HTNAtomNode* Allocate() override
    {
        if (Attempts++ == FailAt) return nullptr;
        HTNAtomNode* Node = HTNNewDeleteAtomListAllocator::Get().Allocate();
        if (Node) { EXPECT_TRUE(Live.insert(Node).second); ++Allocations; }
        return Node;
    }
    void Deallocate(HTNAtomNode* Node) override
    {
        if (!Node) return;
        ASSERT_EQ(Live.erase(Node), 1u);
        ++Releases;
        HTNNewDeleteAtomListAllocator::Get().Deallocate(Node);
    }
    std::unordered_set<HTNAtomNode*> Live;
    size_t Attempts = 0, Allocations = 0, Releases = 0;
    size_t FailAt = std::numeric_limits<size_t>::max();
};

void ExpectAllocator(const HTNAtom& Atom, HTNAtomListAllocator* Allocator)
{
    if (Atom.type != HTN_ATOM_TYPE_LIST) return;
    EXPECT_EQ(Atom.value.list_value.allocator, Allocator);
    for (const HTNAtomNode* Node = Atom.value.list_value.head_node; Node; Node = Node->next_node)
        ExpectAllocator(Node->data, Allocator);
}

struct Instance
{
    HTNDatabaseHook Database;
    HTNCallTermRegistry Registry;
    HTNPlannerHook Hook{Database.GetWorldState(), Registry};
    const HTNGeneratedPlannerDefinition* Definition;
    HTNGeneratedPlannerContext Context{};
    HTNAtomOwner Plan;
    int Observations = 0, Fallbacks = 0;
    HTNAtomOwner Supplied{HTNAtomListOwner{17, HTNAtomListOwner{23, 29}}};
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNGeneratedDebugger Debugger;
#endif

    Instance(const HTNGeneratedPlannerDefinition* InDefinition, HTNAtomListAllocator* Allocator) : Definition(InDefinition)
    {
        Registry.Bind("inspect", [this](const HTNCallTermArguments& Args) {
            ++Observations;
            if (Context.list_allocator) ExpectAllocator(Args[0], static_cast<HTNAtomListAllocator*>(Context.list_allocator));
            return true;
        });
        Registry.Bind("supplied_list", [this](const HTNCallTermArguments&) { return Supplied; });
        Registry.Bind("failing", [](const HTNCallTermArguments&) { return HTNAtomOwner(); });
        Registry.Bind("after_failure", [this](const HTNCallTermArguments&) { ++Fallbacks; return true; });
        EXPECT_TRUE(Hook.SetGeneratedPlannerDefinition(Definition));
        Context.world_state = &Database.GetWorldState();
        Context.callterm_binding_context = &Hook.GetCallTermBindingContext();
        Context.prepared_storage = Hook.GetGeneratedPreparedStorage();
        Context.execution_storage = ::operator new(Definition->execution_storage_size);
        EXPECT_TRUE(Definition->initialize_execution_storage(Context.execution_storage));
        Context.backtracking_mode = HTN_BACKTRACKING_ALL;
        Context.callterm_error_policy = HTNCallTermErrorPolicy::FailSilently;
        Context.list_allocator = Allocator;
#ifdef HTN_DEBUG_DECOMPOSITION
        Debugger.SetEnabled(true);
        Context.debugger = &Debugger;
#endif
    }
    ~Instance()
    {
        Definition->destroy_execution_storage(Context.execution_storage);
        ::operator delete(Context.execution_storage);
    }
    HTNDecompositionStatus Run(const HTNAtom& Call, bool Public = true)
    {
        HTNAtom_Unbind(Plan.Get());
        return Definition->decompose_call(&Context, &Call, Public, Plan.Get());
    }
    template<typename... Args> HTNDecompositionStatus Run(const char* Method, Args&&... Arguments)
    {
        HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol(Method), std::forward<Args>(Arguments)...));
        return Run(*Call.Get());
    }
};

TEST(HTNSafePooledAtomListAllocatorTest, RoutesMixedNodesAndReusesPoolSlots)
{
    TrackingHeap Fallback;
    HTNSafePooledAtomListAllocator Pool(1, Fallback);
    HTNAtomNode* Pooled = Pool.Allocate();
    HTNAtomNode* Heap = Pool.Allocate();
    ASSERT_NE(Pooled, nullptr);
    ASSERT_NE(Heap, nullptr);
    HTNAtom_Init(&Pooled->data);
    HTNAtom_Init(&Heap->data);
    EXPECT_EQ(Pool.GetPooledNodeCount(), 1u);
    EXPECT_EQ(Pool.GetFallbackNodeCount(), 1u);
    EXPECT_EQ(Fallback.Allocations, 1u);
    Pool.Deallocate(Pooled);
    HTNAtomNode* Reused = Pool.Allocate();
    EXPECT_EQ(Reused, Pooled);
    HTNAtom_Init(&Reused->data);
    Pool.Deallocate(Heap);
    Pool.Deallocate(Reused);
    EXPECT_EQ(Pool.GetPooledNodeCount(), 0u);
    EXPECT_EQ(Pool.GetFallbackNodeCount(), 0u);
    EXPECT_EQ(Fallback.Allocations, Fallback.Releases);
    EXPECT_TRUE(Fallback.Live.empty());
}

TEST(HTNSafePooledAtomListAllocatorTest, NestedCopiesAndMovesPreserveAllocatorAndReleaseFallback)
{
    // A pooled fallback has its own allocation_cookie: routing must not overwrite it.
    TrackingAllocator Fallback;
    HTNSafePooledAtomListAllocator Pool(2, Fallback);
    const HTNAtomOwner Seed{HTNAtomListOwner{1, HTNAtomListOwner{2, 3}}};
    {
        HTNAtomOwner Value;
        ASSERT_TRUE(HTNAtom_CopyWithAllocator(Value.Get(), Seed.Get(), &Pool));
        HTNAtomOwner Copy(Value);
        const auto Allocations = Fallback.Allocations;
        HTNAtomOwner Moved(std::move(Copy));
        EXPECT_EQ(Allocations, Fallback.Allocations);
        ExpectAllocator(*Moved.Get(), &Pool);
        EXPECT_TRUE(HTNAtom_Equals(Value.Get(), Moved.Get()));
        EXPECT_GT(Pool.GetFallbackNodeCount(), 0u);
    }
    EXPECT_EQ(Pool.GetPooledNodeCount(), 0u);
    EXPECT_EQ(Pool.GetFallbackNodeCount(), 0u);
    EXPECT_TRUE(Fallback.Live.empty());
    EXPECT_EQ(Fallback.Allocations, Fallback.Releases);
}

class HTNInstanceAllocatorTest : public testing::TestWithParam<int>
{
protected:
    const HTNGeneratedPlannerDefinition* Definition() const
    {
        switch (GetParam())
        {
        case 1: return CreateInstanceAllocatorNoneHTN_GetDefinition();
        case 2: return CreateInstanceAllocatorOverflowHTN_GetDefinition();
        default: return CreateInstanceAllocatorHTN_GetDefinition();
        }
    }
};

TEST_P(HTNInstanceAllocatorTest, DefaultPolicyRemainsUsable)
{
    Instance Planner(Definition(), nullptr);
    ASSERT_EQ(Planner.Run("run", 7), HTN_DECOMPOSITION_SUCCEEDED);
    ExpectAllocator(*Planner.Plan.Get(), &HTNNewDeleteAtomListAllocator::Get());
}

TEST_P(HTNInstanceAllocatorTest, PlansNestedListsSplitsAndDeferredValuesUsePoolAndSurviveStorage)
{
    TrackingAllocator Allocator;
    HTNAtomOwner Retained;
    {
        Instance Planner(Definition(), &Allocator);
        ASSERT_EQ(Planner.Run("run", 7), HTN_DECOMPOSITION_SUCCEEDED);
        EXPECT_EQ(Planner.Observations, 1);
        EXPECT_GT(Allocator.Allocations, 0u);
        ExpectAllocator(*Planner.Plan.Get(), &Allocator);
        HTNAtomOwner DeferredCall(*HTNAtom_GetListElement(Planner.Plan.Get(), 1));
        HTNAtomOwner DeferredArguments(*HTNAtom_GetListElement(DeferredCall.Get(), 1));
        HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("later"), DeferredArguments));
        ASSERT_EQ(Planner.Run(*Call.Get(), false), HTN_DECOMPOSITION_SUCCEEDED);
        ExpectAllocator(*Planner.Plan.Get(), &Allocator);
        Retained = std::move(Planner.Plan);
    }
    EXPECT_GT(Allocator.Live.size(), 0u);
    ExpectAllocator(*Retained.Get(), &Allocator);
    HTNAtomOwner Copy(Retained);
    HTNAtomOwner Moved(std::move(Copy));
    ExpectAllocator(*Moved.Get(), &Allocator);
    HTNAtom_Unbind(Retained.Get());
    HTNAtom_Unbind(Moved.Get());
    EXPECT_TRUE(Allocator.Live.empty());
    EXPECT_EQ(Allocator.Allocations, Allocator.Releases);
    EXPECT_EQ(Allocator.ForeignReleases, 0u);
}

TEST_P(HTNInstanceAllocatorTest, ClientInputsAndCalltermResultsKeepTheirOwnersWhileCopiesUseInstancePool)
{
    TrackingAllocator Client, PlannerPool;
    HTNAtomOwner Input;
    const HTNAtomOwner Seed{HTNAtomListOwner{1, HTNAtomListOwner{2, 3}}};
    ASSERT_TRUE(HTNAtom_CopyWithAllocator(Input.Get(), Seed.Get(), &Client));
    {
        Instance Planner(Definition(), &PlannerPool);
        Planner.Supplied = Input;
        const auto ClientNodes = Client.Live.size();
        ASSERT_EQ(Planner.Run("run", 5), HTN_DECOMPOSITION_SUCCEEDED);
        ExpectAllocator(*Planner.Plan.Get(), &PlannerPool);
        ExpectAllocator(*Input.Get(), &Client);
        EXPECT_EQ(Client.Live.size(), ClientNodes);
        ASSERT_EQ(Planner.Run("copy_input", Input), HTN_DECOMPOSITION_SUCCEEDED);
        ExpectAllocator(*Planner.Plan.Get(), &PlannerPool);
        EXPECT_EQ(Client.Live.size(), ClientNodes);
    }
    HTNAtom_Unbind(Input.Get());
    EXPECT_TRUE(Client.Live.empty());
    EXPECT_TRUE(PlannerPool.Live.empty());
}

TEST_P(HTNInstanceAllocatorTest, FactAxiomAndBranchBacktrackingReleaseDiscardedLists)
{
    TrackingAllocator Allocator;
    Instance Planner(Definition(), &Allocator);
    auto& State = Planner.Database.GetWorldState();
    State.AddFact("candidate", std::vector<HTNAtomOwner>{HTNAtomListOwner{1, HTNAtomListOwner{11}}});
    State.AddFact("candidate", std::vector<HTNAtomOwner>{HTNAtomListOwner{2, HTNAtomListOwner{22}}});
    State.AddFact("accepted", std::vector<HTNAtomOwner>{HTNAtomListOwner{2, HTNAtomListOwner{22}}});
    ASSERT_EQ(Planner.Run("choices"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_NE(HTNAtomToString(Planner.Plan, false).find("(2 (22))"), std::string::npos);
    ExpectAllocator(*Planner.Plan.Get(), &Allocator);
    ASSERT_EQ(Planner.Run("fallback", 6), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(Planner.Fallbacks, 1);
    EXPECT_EQ(HTNAtomToString(Planner.Plan, false).find("discarded"), std::string::npos);
    HTNAtom_Unbind(Planner.Plan.Get());
    EXPECT_TRUE(Allocator.Live.empty());
    EXPECT_EQ(Planner.Run("fail", 3), HTN_DECOMPOSITION_NO_PLAN);
    EXPECT_TRUE(Allocator.Live.empty());
    EXPECT_EQ(Planner.Run("call_failure", 3), HTN_DECOMPOSITION_NO_PLAN);
    EXPECT_TRUE(Allocator.Live.empty());
}

TEST_P(HTNInstanceAllocatorTest, EveryAllocationFailureReturnsOutOfMemoryAndReleasesPartialValues)
{
    for (const char* Method : {"run", "fallback"})
    {
        size_t Attempts = 0;
        {
            TrackingAllocator Pool;
            Instance Planner(Definition(), &Pool);
            ASSERT_EQ(Planner.Run(Method, 7), HTN_DECOMPOSITION_SUCCEEDED);
            Attempts = Pool.Attempts;
        }
        for (size_t Failure = 0; Failure < Attempts; ++Failure)
        {
            SCOPED_TRACE(::testing::Message() << Method << " allocation " << Failure);
            TrackingAllocator Pool;
            Pool.FailAt = Failure;
            Instance Planner(Definition(), &Pool);
            ASSERT_EQ(Planner.Run(Method, 7), HTN_DECOMPOSITION_OUT_OF_MEMORY);
            EXPECT_FALSE(HTNAtom_IsBound(Planner.Plan.Get()));
            const auto* Info = Definition()->get_execution_info(Planner.Context.execution_storage);
            ASSERT_NE(Info->last_error, nullptr);
            EXPECT_NE(std::string(Info->last_error).find("list_allocator"), std::string::npos);
            EXPECT_TRUE(Pool.Live.empty());
            EXPECT_EQ(Pool.Allocations, Pool.Releases);
            Pool.FailAt = std::numeric_limits<size_t>::max();
            ASSERT_EQ(Planner.Run(Method, 8), HTN_DECOMPOSITION_SUCCEEDED);
        }
    }
    TrackingAllocator Empty(0);
    Instance Planner(Definition(), &Empty);
    EXPECT_EQ(Planner.Run("run", 7), HTN_DECOMPOSITION_OUT_OF_MEMORY);
    EXPECT_TRUE(Empty.Live.empty());
}

TEST_P(HTNInstanceAllocatorTest, ConcurrentInstancesNeverShareAllocationOrDeallocation)
{
    TrackingAllocator LeftPool, RightPool;
    Instance Left(Definition(), &LeftPool), Right(Definition(), &RightPool);
    HTNAtomOwner LeftCall(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("run"), 1));
    HTNAtomOwner RightCall(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("run"), 2));
    std::atomic<int> Ready{0};
    const auto Execute = [&](Instance& Planner, const HTNAtom& Call, TrackingAllocator& Pool) {
        ++Ready;
        while (Ready.load() < 2) std::this_thread::yield();
        for (int I = 0; I < 10; ++I)
        {
            EXPECT_EQ(Planner.Run(Call), HTN_DECOMPOSITION_SUCCEEDED);
            ExpectAllocator(*Planner.Plan.Get(), &Pool);
        }
        HTNAtom_Unbind(Planner.Plan.Get());
    };
    std::thread A(Execute, std::ref(Left), std::cref(*LeftCall.Get()), std::ref(LeftPool));
    std::thread B(Execute, std::ref(Right), std::cref(*RightCall.Get()), std::ref(RightPool));
    A.join(); B.join();
    EXPECT_TRUE(LeftPool.Live.empty());
    EXPECT_TRUE(RightPool.Live.empty());
    EXPECT_GT(LeftPool.Allocations, 0u);
    EXPECT_GT(RightPool.Allocations, 0u);
    EXPECT_EQ(LeftPool.Allocations, LeftPool.Releases);
    EXPECT_EQ(RightPool.Allocations, RightPool.Releases);
}

TEST_P(HTNInstanceAllocatorTest, SafePoolUsesHeapFallbackAndReportsFallbackExhaustion)
{
    size_t Attempts = 0;
    TrackingHeap Fallback;
    HTNSafePooledAtomListAllocator Pool(2, Fallback);
    {
        Instance Planner(Definition(), &Pool);
        ASSERT_EQ(Planner.Run("run", 7), HTN_DECOMPOSITION_SUCCEEDED);
        ExpectAllocator(*Planner.Plan.Get(), &Pool);
        EXPECT_GT(Pool.GetFallbackNodeCount(), 0u);
        EXPECT_GT(Fallback.Allocations, 0u);
        Attempts = Fallback.Attempts;
    }
    EXPECT_EQ(Pool.GetPooledNodeCount(), 0u);
    EXPECT_EQ(Pool.GetFallbackNodeCount(), 0u);
    EXPECT_TRUE(Fallback.Live.empty());
    EXPECT_EQ(Fallback.Allocations, Fallback.Releases);
    for (size_t Failure = 0; Failure < Attempts; ++Failure)
    {
        SCOPED_TRACE(Failure);
        TrackingHeap Failing;
        Failing.FailAt = Failure;
        HTNSafePooledAtomListAllocator Safe(2, Failing);
        Instance Planner(Definition(), &Safe);
        EXPECT_EQ(Planner.Run("run", 7), HTN_DECOMPOSITION_OUT_OF_MEMORY);
        EXPECT_FALSE(HTNAtom_IsBound(Planner.Plan.Get()));
        EXPECT_EQ(Safe.GetPooledNodeCount(), 0u);
        EXPECT_EQ(Safe.GetFallbackNodeCount(), 0u);
        EXPECT_TRUE(Failing.Live.empty());
        EXPECT_EQ(Failing.Allocations, Failing.Releases);
    }
    HTNSafePooledAtomListAllocator DefaultFallback(0);
    Instance Planner(Definition(), &DefaultFallback);
    ASSERT_EQ(Planner.Run("run", 7), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_GT(DefaultFallback.GetFallbackNodeCount(), 0u);
}

TEST_P(HTNInstanceAllocatorTest, AllocationFailuresInAxiomBacktrackingAndDeferredInputsAreFatal)
{
    const HTNAtomOwner Value{HTNAtomListOwner{2, HTNAtomListOwner{22}}};
    const HTNAtomOwner Choices(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("choices")));
    const HTNAtomOwner Deferred(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("later"), Value));
    for (const HTNAtomOwner* Call : {&Choices, &Deferred})
    {
        const auto Execute = [&](Instance& Planner) {
            auto& State = Planner.Database.GetWorldState();
            State.AddFact("candidate", std::vector<HTNAtomOwner>{HTNAtomListOwner{1}});
            State.AddFact("candidate", std::vector<HTNAtomOwner>{Value});
            State.AddFact("accepted", std::vector<HTNAtomOwner>{Value});
            return Planner.Run(*Call->Get(), Call == &Choices);
        };
        size_t Attempts = 0;
        {
            TrackingAllocator Pool;
            Instance Planner(Definition(), &Pool);
            ASSERT_EQ(Execute(Planner), HTN_DECOMPOSITION_SUCCEEDED);
            Attempts = Pool.Attempts;
        }
        for (size_t Failure = 0; Failure < Attempts; ++Failure)
        {
            SCOPED_TRACE(::testing::Message() << HTNAtomToString(*Call, false) << " allocation " << Failure);
            TrackingAllocator Pool;
            Pool.FailAt = Failure;
            Instance Planner(Definition(), &Pool);
            EXPECT_EQ(Execute(Planner), HTN_DECOMPOSITION_OUT_OF_MEMORY);
            EXPECT_FALSE(HTNAtom_IsBound(Planner.Plan.Get()));
            EXPECT_TRUE(Pool.Live.empty());
            EXPECT_EQ(Pool.Allocations, Pool.Releases);
        }
    }
}

TEST_P(HTNInstanceAllocatorTest, PlanningUnitUsesPoolForActiveAndDeferredPlansAndRejectsPartialCopies)
{
    size_t Attempts = 0;
    {
        TrackingAllocator Pool;
        Instance Planner(Definition(), &Pool);
        HTNPlanningUnit Unit(Planner.Database, Planner.Hook, "run");
        Unit.GetExecutionContext().ListAllocator = &Pool;
        Unit.GetExecutionContext().CallTermErrorPolicy = HTNCallTermErrorPolicy::FailSilently;
        ASSERT_EQ(Unit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("run"), 7), HTN_DECOMPOSITION_SUCCEEDED);
        Attempts = Pool.Attempts;
        for (const auto& Step : Unit.GetCurrentPlan()) ExpectAllocator(*Step.Get(), &Pool);
        ASSERT_EQ(Unit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::TaskReady);
        Unit.CompleteCurrentPrimitiveTask();
        ASSERT_EQ(Unit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::TaskReady);
        EXPECT_EQ(HTNGetCallHead(*Unit.GetCurrentPrimitiveTask()), HtnSymbol::sGetSymbol("!deferred"));
        for (const auto& Step : Unit.GetCurrentPlan()) ExpectAllocator(*Step.Get(), &Pool);
    }
    // Includes allocation failures after a successful decomposition, during the
    // reference integration's separate active-plan copy.
    for (size_t Failure = 0; Failure < Attempts; ++Failure)
    {
        SCOPED_TRACE(Failure);
        TrackingAllocator Pool;
        Pool.FailAt = Failure;
        {
            Instance Planner(Definition(), &Pool);
            HTNPlanningUnit Unit(Planner.Database, Planner.Hook, "run");
            Unit.GetExecutionContext().ListAllocator = &Pool;
            Unit.GetExecutionContext().CallTermErrorPolicy = HTNCallTermErrorPolicy::FailSilently;
            EXPECT_EQ(Unit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("run"), 7), HTN_DECOMPOSITION_OUT_OF_MEMORY);
            EXPECT_TRUE(Unit.GetCurrentPlan().empty());
        }
        EXPECT_TRUE(Pool.Live.empty());
        EXPECT_EQ(Pool.Allocations, Pool.Releases);
    }
}

TEST_P(HTNInstanceAllocatorTest, PreviousContextRevisionIsRejectedBeforeExecution)
{
    HTNGeneratedPlannerDefinition Previous = *Definition();
    --Previous.abi_version;
    EXPECT_FALSE(HTNGeneratedPlanner_ValidateDefinition(&Previous));
}

INSTANTIATE_TEST_SUITE_P(Generated, HTNInstanceAllocatorTest, ::testing::Values(0, 1, 2));
}
