// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Translator/HTNGeneratedProfiling.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

struct HTNGeneratedProfilingState
{
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    HTNGeneratedPreparationTimingBreakdown LastPreparationTiming{};
    std::chrono::steady_clock::time_point PreparationTimingStart{};
    std::chrono::steady_clock::time_point PreparationTimingStageStart{};
    HTNGeneratedStructuralCounters StructuralCounters{};
#endif
#ifdef HTN_PROFILE_DETAILED
    struct ProfileBucket
    {
        std::uint64_t Nanoseconds = 0u;
        std::uint64_t SelfNanoseconds = 0u;
        std::uint64_t Calls = 0u;
    };

    struct ActiveProfileFrame
    {
        HTNGeneratedProfileCategory Category = HTN_GENERATED_PROFILE_FACT;
        std::chrono::steady_clock::time_point Start{};
        std::uint64_t ChildNanoseconds = 0u;
    };

    bool ProfilingEnabled = false;
    std::array<ProfileBucket, HTN_GENERATED_PROFILE_CATEGORY_COUNT> Profile{};
    std::vector<ActiveProfileFrame> ActiveProfileFrames;
#endif
};

namespace
{
#ifdef HTN_PROFILE_DETAILED
void ProfileBeginInternal(HTNGeneratedProfilingState* inState, const HTNGeneratedProfileCategory inCategory)
{
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    if (inState && inCategory < HTN_GENERATED_PROFILE_CATEGORY_COUNT)
        ++inState->StructuralCounters.calls[static_cast<size_t>(inCategory)];
#endif
    if (!inState || !inState->ProfilingEnabled || inCategory >= HTN_GENERATED_PROFILE_CATEGORY_COUNT)
        return;

    HTNGeneratedProfilingState::ActiveProfileFrame Frame;
    Frame.Category = inCategory;
    Frame.Start = std::chrono::steady_clock::now();
    inState->ActiveProfileFrames.emplace_back(std::move(Frame));
}

void ProfileEndInternal(HTNGeneratedProfilingState* inState, const HTNGeneratedProfileCategory inCategory)
{
    if (!inState || !inState->ProfilingEnabled || inState->ActiveProfileFrames.empty())
        return;

    auto Frame = std::move(inState->ActiveProfileFrames.back());
    inState->ActiveProfileFrames.pop_back();
    if (Frame.Category != inCategory)
    {
        inState->ActiveProfileFrames.clear();
        return;
    }

    const auto End = std::chrono::steady_clock::now();
    const std::uint64_t InclusiveNanoseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(End - Frame.Start).count());
    const std::uint64_t SelfNanoseconds = InclusiveNanoseconds > Frame.ChildNanoseconds
        ? InclusiveNanoseconds - Frame.ChildNanoseconds : 0u;

    auto& Bucket = inState->Profile[static_cast<size_t>(inCategory)];
    Bucket.Nanoseconds += InclusiveNanoseconds;
    Bucket.SelfNanoseconds += SelfNanoseconds;
    ++Bucket.Calls;
    if (!inState->ActiveProfileFrames.empty())
        inState->ActiveProfileFrames.back().ChildNanoseconds += InclusiveNanoseconds;
}
#endif
}

extern "C" HTNGeneratedProfilingState* HTNGeneratedProfiling_Create(void)
{
#if defined(HTN_PROFILE_DETAILED) || defined(HTN_GENERATED_EXECUTION_PROFILING)
    auto* State = new (std::nothrow) HTNGeneratedProfilingState();
#ifdef HTN_PROFILE_DETAILED
    if (State)
        State->ActiveProfileFrames.reserve(64u);
#endif
    return State;
#else
    return nullptr;
#endif
}

extern "C" void HTNGeneratedProfiling_Destroy(HTNGeneratedProfilingState* inState)
{
    delete inState;
}

extern "C" void HTNGeneratedProfiling_ResetExecution(HTNGeneratedProfilingState* inState)
{
    if (!inState)
        return;
#ifdef HTN_GENERATED_EXECUTION_PROFILING
    inState->StructuralCounters = HTNGeneratedStructuralCounters{};
#endif
#ifdef HTN_PROFILE_DETAILED
    inState->ActiveProfileFrames.clear();
#endif
}

#ifdef HTN_GENERATED_EXECUTION_PROFILING
extern "C" void HTNGeneratedProfiling_BeginPreparationTiming(HTNGeneratedProfilingState* inState,
                                                              const int inFactSlotsCacheHit,
                                                              const int inCallTermSlotsCacheHit)
{
    if (!inState)
        return;

    inState->LastPreparationTiming = HTNGeneratedPreparationTimingBreakdown{};
    inState->LastPreparationTiming.fact_slots_cache_hit = inFactSlotsCacheHit ? 1 : 0;
    inState->LastPreparationTiming.callterm_slots_cache_hit = inCallTermSlotsCacheHit ? 1 : 0;
    inState->PreparationTimingStart = std::chrono::steady_clock::now();
    inState->PreparationTimingStageStart = inState->PreparationTimingStart;
}

extern "C" void HTNGeneratedProfiling_MarkPreparationTiming(HTNGeneratedProfilingState* inState,
                                                             const HTNGeneratedPreparationStage inStage)
{
    if (!inState)
        return;

    const auto Now = std::chrono::steady_clock::now();
    const double StageMilliseconds =
        std::chrono::duration<double, std::milli>(Now - inState->PreparationTimingStageStart).count();

    switch (inStage)
    {
    case HTN_GENERATED_PREPARATION_RESET:
        inState->LastPreparationTiming.reset_milliseconds = StageMilliseconds;
        break;
    case HTN_GENERATED_PREPARATION_FACT_SLOTS:
        inState->LastPreparationTiming.fact_slots_milliseconds = StageMilliseconds;
        break;
    case HTN_GENERATED_PREPARATION_CALLTERM_SLOTS:
        inState->LastPreparationTiming.callterm_slots_milliseconds = StageMilliseconds;
        break;
    case HTN_GENERATED_PREPARATION_TOP_LEVEL_METHOD:
        inState->LastPreparationTiming.top_level_method_milliseconds = StageMilliseconds;
        break;
    case HTN_GENERATED_PREPARATION_COMPLETE:
        inState->LastPreparationTiming.total_milliseconds =
            std::chrono::duration<double, std::milli>(Now - inState->PreparationTimingStart).count();
        return;
    default:
        return;
    }

    inState->PreparationTimingStageStart = Now;
}

extern "C" int HTNGeneratedProfiling_GetLastPreparationTiming(
    const HTNGeneratedProfilingState* inState,
    HTNGeneratedPreparationTimingBreakdown* outTiming)
{
    if (!inState || !outTiming)
        return 0;
    *outTiming = inState->LastPreparationTiming;
    return 1;
}

extern "C" HTNGeneratedStructuralCounters* HTNGeneratedProfiling_GetStructuralCountersMutable(
    HTNGeneratedProfilingState* inState)
{
    return inState ? &inState->StructuralCounters : nullptr;
}

extern "C" int HTNGeneratedProfiling_GetStructuralCounters(
    const HTNGeneratedProfilingState* inState,
    HTNGeneratedStructuralCounters* outCounters)
{
    if (!inState || !outCounters)
        return 0;
    *outCounters = inState->StructuralCounters;
    return 1;
}
#endif

extern "C" void HTNGeneratedProfiling_SetEnabled(HTNGeneratedProfilingState* inState, const int inEnabled)
{
#ifdef HTN_PROFILE_DETAILED
    if (inState)
        inState->ProfilingEnabled = inEnabled != 0;
#else
    (void)inState;
    (void)inEnabled;
#endif
}

extern "C" void HTNGeneratedProfiling_ResetProfile(HTNGeneratedProfilingState* inState)
{
#ifdef HTN_PROFILE_DETAILED
    if (!inState)
        return;
    for (auto& Bucket : inState->Profile)
        Bucket = {};
    inState->ActiveProfileFrames.clear();
#else
    (void)inState;
#endif
}

extern "C" int HTNGeneratedProfiling_GetProfileSample(const HTNGeneratedProfilingState* inState,
                                                        const uint32_t inCategory,
                                                        HTNGeneratedProfileSample* outSample)
{
#ifdef HTN_PROFILE_DETAILED
    if (!inState || !outSample || inCategory >= HTN_GENERATED_PROFILE_CATEGORY_COUNT)
        return 0;
    const auto& Bucket = inState->Profile[inCategory];
    outSample->nanoseconds = Bucket.Nanoseconds;
    outSample->self_nanoseconds = Bucket.SelfNanoseconds;
    outSample->calls = Bucket.Calls;
    return 1;
#else
    (void)inState;
    (void)inCategory;
    if (outSample)
        *outSample = {};
    return 0;
#endif
}

extern "C" void HTNGeneratedProfiling_ProfileBegin(HTNGeneratedProfilingState* inState, const uint32_t inCategory)
{
#ifdef HTN_PROFILE_DETAILED
    if (inCategory < HTN_GENERATED_PROFILE_CATEGORY_COUNT)
        ProfileBeginInternal(inState, static_cast<HTNGeneratedProfileCategory>(inCategory));
#else
    (void)inState;
    (void)inCategory;
#endif
}

extern "C" void HTNGeneratedProfiling_ProfileEnd(HTNGeneratedProfilingState* inState, const uint32_t inCategory)
{
#ifdef HTN_PROFILE_DETAILED
    if (inCategory < HTN_GENERATED_PROFILE_CATEGORY_COUNT)
        ProfileEndInternal(inState, static_cast<HTNGeneratedProfileCategory>(inCategory));
#else
    (void)inState;
    (void)inCategory;
#endif
}
