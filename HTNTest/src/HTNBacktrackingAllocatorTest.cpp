// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNAtomListAllocator.h"
#include "Hook/HTNDatabaseHook.h"
#include "Hook/HTNPlannerHook.h"
#include "Hook/HTNPlanningUnit.h"
#include "Translator/HTNGeneratedDebugger.h"
#include "Translator/HTNGeneratedPlanner.h"
#include "HTNGTest.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

extern "C" const HTNGeneratedPlannerDefinition* CreateBacktrackingAllocatorHTN_GetDefinition(void);
extern "C" const HTNGeneratedPlannerDefinition* CreateBacktrackingAllocatorNoneHTN_GetDefinition(void);

namespace
{
// No-op deallocation deliberately models an arena with a caller-owned marker.
// Track exact ownership, size and alignment, not just the resulting plan.
struct ScratchArena
{
    struct Block { void* Pointer; size_t Size; size_t Alignment; bool Released = false; };
    std::vector<std::byte> Buffer = std::vector<std::byte>(1024u * 1024u);
    std::vector<Block> Blocks;
    size_t Used = 0, LiveBytes = 0, PeakBytes = 0, Requests = 0, RequestedBytes = 0, Releases = 0;
    size_t FailAt = std::numeric_limits<size_t>::max();
    size_t Capacity = Buffer.size();
    HTNBacktrackingAllocator Allocator{this, Allocate, Deallocate};

    static void* Allocate(void* inUser, size_t inSize, size_t inAlignment)
    {
        auto& Self = *static_cast<ScratchArena*>(inUser);
        Self.RequestedBytes += inSize;
        if (Self.Requests++ == Self.FailAt) return nullptr;
        void* Memory = Self.Buffer.data() + Self.Used;
        size_t Space = Self.Capacity - Self.Used;
        if (!std::align(inAlignment, inSize, Memory, Space)) return nullptr;
        Self.Used = static_cast<std::byte*>(Memory) - Self.Buffer.data() + inSize;
        Self.Blocks.push_back({Memory, inSize, inAlignment});
        Self.LiveBytes += inSize;
        Self.PeakBytes = std::max(Self.PeakBytes, Self.LiveBytes);
        return Memory;
    }

    static void Deallocate(void* inUser, void* inMemory, size_t inSize, size_t inAlignment)
    {
        auto& Self = *static_cast<ScratchArena*>(inUser);
        const auto It = std::find_if(Self.Blocks.begin(), Self.Blocks.end(),
            [inMemory](const Block& Entry) { return Entry.Pointer == inMemory && !Entry.Released; });
        ASSERT_NE(It, Self.Blocks.end()) << "Foreign or duplicate release";
        EXPECT_EQ(It->Size, inSize);
        EXPECT_EQ(It->Alignment, inAlignment);
        It->Released = true;
        Self.LiveBytes -= inSize;
        ++Self.Releases;
    }

    void Reset()
    {
        ASSERT_EQ(LiveBytes, 0u);
        EXPECT_EQ(Releases, Blocks.size());
        std::memset(Buffer.data(), 0xcd, Used);
        Used = LiveBytes = PeakBytes = Requests = RequestedBytes = Releases = 0;
        Blocks.clear();
    }

    void ExpectStats(const HTNBacktrackingAllocationStats& inStats) const
    {
        EXPECT_EQ(inStats.current_bytes, 0u);
        EXPECT_EQ(inStats.peak_bytes, PeakBytes);
        EXPECT_EQ(inStats.requested_bytes, RequestedBytes);
        EXPECT_EQ(inStats.allocation_count, Blocks.size());
        EXPECT_EQ(inStats.failed_allocation_count, Requests - Blocks.size());
        EXPECT_EQ(LiveBytes, 0u);
        EXPECT_EQ(Releases, Blocks.size());
        for (const auto& Block : Blocks)
        {
            EXPECT_GE(inStats.largest_request_bytes, Block.Size);
            EXPECT_GE(inStats.max_alignment, Block.Alignment);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(Block.Pointer) % Block.Alignment, 0u);
        }
    }
};

struct Planner
{
    HTNDatabaseHook Database;
    HTNCallTermRegistry Registry;
    HTNPlannerHook Hook{Database.GetWorldState(), Registry};
    const HTNGeneratedPlannerDefinition* Definition;
    HTNGeneratedPlannerContext Context{};
    HTNAtomOwner Result;
#ifdef HTN_DEBUG_DECOMPOSITION
    HTNGeneratedDebugger Debugger;
#endif

    Planner(const HTNGeneratedPlannerDefinition* inDefinition, const HTNBacktrackingAllocator* inAllocator)
        : Definition(inDefinition)
    {
        EXPECT_TRUE(Hook.SetGeneratedPlannerDefinition(Definition));
        Database.GetWorldState().SetFactRegistry(&Hook.GetFactRegistry());
        Context.world_state = &Database.GetWorldState();
        Context.callterm_binding_context = &Hook.GetCallTermBindingContext();
        Context.prepared_storage = Hook.GetGeneratedPreparedStorage();
        Context.execution_storage = ::operator new(Definition->execution_storage_size);
        EXPECT_TRUE(Definition->initialize_execution_storage(Context.execution_storage));
        Context.backtracking_mode = HTN_BACKTRACKING_ALL;
        Context.callterm_error_policy = HTNCallTermErrorPolicy::FailSilently;
        Context.backtracking_allocator = inAllocator;
#ifdef HTN_DEBUG_DECOMPOSITION
        Debugger.SetEnabled(true);
        Context.debugger = &Debugger;
#endif
    }
    ~Planner()
    {
        Definition->destroy_execution_storage(Context.execution_storage);
        ::operator delete(Context.execution_storage);
    }
    const HTNGeneratedExecutionInfo& Info() const { return *Definition->get_execution_info(Context.execution_storage); }
    HTNDecompositionStatus Run(const HTNAtom& inCall, bool inPublic = true)
    {
        HTNAtom_Unbind(Result.Get());
        return Definition->decompose_call(&Context, &inCall, inPublic, Result.Get());
    }
    template<typename... Args> HTNDecompositionStatus Run(const char* inMethod, Args&&... inArgs)
    {
        HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol(inMethod), std::forward<Args>(inArgs)...));
        return Run(*Call.Get());
    }
};

class HTNBacktrackingAllocatorTest : public testing::TestWithParam<bool>
{
protected:
    const HTNGeneratedPlannerDefinition* Definition() const
    {
        return GetParam() ? CreateBacktrackingAllocatorNoneHTN_GetDefinition() : CreateBacktrackingAllocatorHTN_GetDefinition();
    }
};

TEST_P(HTNBacktrackingAllocatorTest, DefaultRetainsBlocksAndReportsWarmReuse)
{
    Planner P(Definition(), nullptr);
    ASSERT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_SUCCEEDED);
    const auto First = P.Info().backtracking_allocations;
    EXPECT_GT(First.allocation_count, 0u);
    EXPECT_GT(First.current_bytes, 0u);
    EXPECT_EQ(First.peak_bytes, First.current_bytes);
    ASSERT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_SUCCEEDED);
    const auto& Warm = P.Info().backtracking_allocations;
    EXPECT_EQ(Warm.allocation_count, 0u);
    EXPECT_EQ(Warm.requested_bytes, 0u);
    EXPECT_EQ(Warm.peak_bytes, First.peak_bytes);
    EXPECT_EQ(Warm.current_bytes, First.current_bytes);
}

TEST_P(HTNBacktrackingAllocatorTest, ScratchIsReleasedOnReturnAndPlanSurvivesMarkerReset)
{
    ScratchArena Arena;
    HTNPooledAtomListAllocator Lists(4096);
    Planner P(Definition(), &Arena.Allocator);
    P.Context.list_allocator = &Lists;
    for (int Iteration = 0; Iteration < 3; ++Iteration)
    {
        ASSERT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_SUCCEEDED);
        const auto Stats = P.Info().backtracking_allocations;
        EXPECT_GT(Stats.allocation_count, 1u);
        Arena.ExpectStats(Stats);
        HTNAtomOwner Retained(std::move(P.Result));
        Arena.Reset();
        EXPECT_EQ(P.Info().backtracking_allocations.peak_bytes, Stats.peak_bytes);
        EXPECT_EQ(Retained.GetListSize(), 161);
        const HTNAtom* Payload = HTNAtom_GetListElement(&Retained.GetListElement(0), 1);
        ASSERT_NE(Payload, nullptr);
        ASSERT_EQ(Payload->type, HTN_ATOM_TYPE_LIST);
        EXPECT_EQ(HTNAtom_GetListElement(Payload, 1)->value.int_value, 80);
    }
    EXPECT_EQ(Lists.GetAllocatedNodeCount(), 0u);
}

TEST_P(HTNBacktrackingAllocatorTest, FailureAndBranchBacktrackingReleaseScratch)
{
    ScratchArena Arena;
    Planner P(Definition(), &Arena.Allocator);
    ASSERT_EQ(P.Run("broken", 80), HTN_DECOMPOSITION_NO_PLAN);
    EXPECT_FALSE(P.Result.IsBound());
    EXPECT_GT(P.Info().backtracking_allocations.allocation_count, 0u);
    Arena.ExpectStats(P.Info().backtracking_allocations);
    Arena.Reset();
    ASSERT_EQ(P.Run("fallback", 80), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(P.Result.GetListSize(), 1);
    EXPECT_EQ(HTNGetCallHead(&P.Result.GetListElement(0)), HtnSymbol::sGetSymbol("!kept"));
    Arena.ExpectStats(P.Info().backtracking_allocations);
    Arena.Reset();
}

TEST_P(HTNBacktrackingAllocatorTest, AxiomAlternativesAndDeferredCallsUseCurrentAllocator)
{
    ScratchArena Arena;
    Planner P(Definition(), &Arena.Allocator);
    auto& World = P.Database.GetWorldState();
    ASSERT_TRUE(World.WriteFact(HtnSymbol::sGetSymbol("candidate"), 5));
    ASSERT_TRUE(World.WriteFact(HtnSymbol::sGetSymbol("candidate"), 80));
    ASSERT_TRUE(World.WriteFact(HtnSymbol::sGetSymbol("accepted"), 80));
    ASSERT_EQ(P.Run("choices"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(P.Result.GetListSize(), 162);
    Arena.ExpectStats(P.Info().backtracking_allocations);
    Arena.Reset();
    ASSERT_EQ(P.Run("deferred", 80), HTN_DECOMPOSITION_SUCCEEDED);
    HTNAtomOwner Call = HTNMakeCallFromDeferredPlanStep(HTNAtomOwner(P.Result.GetListElement(0)));
    Arena.ExpectStats(P.Info().backtracking_allocations);
    Arena.Reset();
    ASSERT_EQ(P.Run(*Call.Get(), false), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_GT(P.Info().backtracking_allocations.allocation_count, 0u);
    Arena.ExpectStats(P.Info().backtracking_allocations);
    Arena.Reset();
}

TEST_P(HTNBacktrackingAllocatorTest, EveryAllocationCanFailWithoutLeaksOrFallback)
{
    ScratchArena Arena;
    Planner P(Definition(), &Arena.Allocator);
    ASSERT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_SUCCEEDED);
    const size_t Attempts = Arena.Requests;
    Arena.Reset();
    for (size_t Failure = 0; Failure < Attempts; ++Failure)
    {
        SCOPED_TRACE(Failure);
        Arena.FailAt = Failure;
        EXPECT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_OUT_OF_MEMORY);
        EXPECT_FALSE(P.Result.IsBound());
        ASSERT_NE(P.Info().last_error, nullptr);
        EXPECT_NE(std::string(P.Info().last_error).find("backtracking_allocator"), std::string::npos);
        EXPECT_EQ(P.Info().backtracking_allocations.failed_allocation_count, 1u);
        Arena.ExpectStats(P.Info().backtracking_allocations);
        Arena.Reset();
    }
    Arena.FailAt = std::numeric_limits<size_t>::max();
    Arena.Capacity = 1;
    EXPECT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_OUT_OF_MEMORY);
    Arena.ExpectStats(P.Info().backtracking_allocations);
    Arena.Reset();
    Arena.Capacity = Arena.Buffer.size();
    ASSERT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_SUCCEEDED);
    Arena.ExpectStats(P.Info().backtracking_allocations);
    Arena.Reset();
}

TEST_P(HTNBacktrackingAllocatorTest, SwitchingAllocatorsAndEarlyReturnsResetStatistics)
{
    ScratchArena A, B;
    Planner P(Definition(), nullptr);
    ASSERT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_SUCCEEDED);
    for (auto* Arena : {&A, &B})
    {
        P.Context.backtracking_allocator = &Arena->Allocator;
        ASSERT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_SUCCEEDED);
        Arena->ExpectStats(P.Info().backtracking_allocations);
        Arena->Reset();
    }
    ASSERT_EQ(P.Run("no_overflow"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(P.Info().backtracking_allocations.peak_bytes, 0u);
    EXPECT_EQ(P.Info().backtracking_allocations.allocation_count, 0u);
    HTNAtomOwner Invalid(42);
    EXPECT_EQ(P.Run(*Invalid.Get()), HTN_DECOMPOSITION_INVALID_CALL);
    EXPECT_EQ(P.Info().backtracking_allocations.peak_bytes, 0u);
    P.Context.prepared_storage = nullptr;
    EXPECT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_INVALID_CONTEXT);
    EXPECT_EQ(P.Info().backtracking_allocations.allocation_count, 0u);
    P.Context.prepared_storage = P.Hook.GetGeneratedPreparedStorage();
    P.Context.backtracking_allocator = nullptr;
    ASSERT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_GT(P.Info().backtracking_allocations.current_bytes, 0u);
}

TEST_P(HTNBacktrackingAllocatorTest, IncompleteCallbacksAreRejectedBeforeInvocation)
{
    ScratchArena Arena;
    HTNBacktrackingAllocator Invalid = Arena.Allocator;
    Planner P(Definition(), &Invalid);
    for (bool MissingAllocate : {false, true})
    {
        Invalid = Arena.Allocator;
        if (MissingAllocate) Invalid.allocate = nullptr;
        else Invalid.deallocate = nullptr;
        EXPECT_EQ(P.Run("walk", 80), HTN_DECOMPOSITION_INVALID_CONTEXT);
        EXPECT_FALSE(P.Result.IsBound());
        ASSERT_NE(P.Info().last_error, nullptr);
        EXPECT_NE(std::string(P.Info().last_error).find("both allocate and deallocate"), std::string::npos);
    }
    EXPECT_EQ(Arena.Requests, 0u);
}

TEST_P(HTNBacktrackingAllocatorTest, FrameCapacityFailureAlsoReleasesScratch)
{
    ScratchArena Arena;
    Planner P(Definition(), &Arena.Allocator);
#ifdef HTN_DEBUG_DECOMPOSITION
    P.Debugger.SetEnabled(false);
#endif
    EXPECT_EQ(P.Run("walk", 10000), HTN_DECOMPOSITION_CALL_FRAME_CAPACITY_EXCEEDED);
    EXPECT_FALSE(P.Result.IsBound());
    EXPECT_GT(P.Info().backtracking_allocations.allocation_count, 0u);
    Arena.ExpectStats(P.Info().backtracking_allocations);
    Arena.Reset();
    ASSERT_EQ(P.Run("no_overflow"), HTN_DECOMPOSITION_SUCCEEDED);
    EXPECT_EQ(P.Info().backtracking_allocations.peak_bytes, 0u);
}

TEST_P(HTNBacktrackingAllocatorTest, PlanningUnitForwardsAllocatorIncludingDeferredExpansion)
{
    ScratchArena Arena;
    Planner P(Definition(), nullptr);
    HTNPlanningUnit Unit(P.Database, P.Hook, "deferred");
    EXPECT_EQ(Unit.GetGeneratedExecutionInfo(), nullptr);
    Unit.GetExecutionContext().BacktrackingAllocator = &Arena.Allocator;
    Unit.GetExecutionContext().CallTermErrorPolicy = HTNCallTermErrorPolicy::FailSilently;
    ASSERT_EQ(Unit.DecomposeTopLevelMethod(HtnSymbol::sGetSymbol("deferred"), 80), HTN_DECOMPOSITION_SUCCEEDED);
    Arena.Reset();
    EXPECT_EQ(Unit.ResolveCurrentPrimitiveTask(), HTNPrimitiveTaskResolution::TaskReady);
    EXPECT_GT(Arena.Requests, 0u);
    EXPECT_EQ(Arena.LiveBytes, 0u);
    ASSERT_NE(Unit.GetGeneratedExecutionInfo(), nullptr);
    Arena.ExpectStats(Unit.GetGeneratedExecutionInfo()->backtracking_allocations);
    Arena.Reset();
    EXPECT_EQ(Unit.GetCurrentPlan().size(), 161u);
}

TEST_P(HTNBacktrackingAllocatorTest, ConcurrentInstancesHaveIndependentScratchAndStatistics)
{
    ScratchArena A, B;
    Planner Left(Definition(), &A.Allocator), Right(Definition(), &B.Allocator);
    const HTNAtomOwner Call(HTNAtom::sCreateCall(HtnSymbol::sGetSymbol("walk"), 80));
    auto Execute = [&Call](Planner& P, ScratchArena& Arena) {
        for (int I = 0; I < 8; ++I)
        {
            EXPECT_EQ(P.Run(*Call.Get()), HTN_DECOMPOSITION_SUCCEEDED);
            Arena.ExpectStats(P.Info().backtracking_allocations);
            Arena.Reset();
        }
    };
    std::thread First(Execute, std::ref(Left), std::ref(A));
    std::thread Second(Execute, std::ref(Right), std::ref(B));
    First.join(); Second.join();
    EXPECT_GT(Left.Info().backtracking_allocations.peak_bytes, 0u);
    EXPECT_EQ(Left.Info().backtracking_allocations.peak_bytes, Right.Info().backtracking_allocations.peak_bytes);
}

INSTANTIATE_TEST_SUITE_P(Generated, HTNBacktrackingAllocatorTest, ::testing::Bool());
}
