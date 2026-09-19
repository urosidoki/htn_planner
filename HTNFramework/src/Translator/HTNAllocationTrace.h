// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <cstdint>

namespace HTNAllocationTrace
{
// Independent of Source: a materialization tag must not hide its execution phase.
enum class Phase : std::uint8_t
{
    None, CallConstruction, ContextAndStorage, GeneratedExecution,
    ActivePlanCopy, Count
};

#ifdef HTN_PROFILE_DETAILED
inline thread_local Phase CurrentPhase = Phase::None;
class PhaseScope
{
public:
    explicit PhaseScope(Phase inPhase) noexcept : mPrevious(CurrentPhase) { CurrentPhase = inPhase; }
    ~PhaseScope() noexcept { CurrentPhase = mPrevious; }
    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;
private:
    Phase mPrevious;
};
inline Phase GetCurrentPhase() noexcept { return CurrentPhase; }
#else
class PhaseScope { public: explicit PhaseScope(Phase) noexcept {} };
inline Phase GetCurrentPhase() noexcept { return Phase::None; }
#endif

inline const char* GetPhaseName(Phase inPhase) noexcept
{
    switch (inPhase)
    {
    case Phase::None: return "Unattributed";
    case Phase::CallConstruction: return "CallConstruction";
    case Phase::ContextAndStorage: return "ContextAndStorage";
    case Phase::GeneratedExecution: return "GeneratedExecution";
    case Phase::ActivePlanCopy: return "ActivePlanCopy";
    case Phase::Count: break;
    }
    return "Unknown";
}

enum class Source : std::uint8_t
{
    None = 0,
    FactIndexKey,
    LiteralMaterialization,
    ListMaterialization,
    CallArgumentMaterialization,
    CompoundArgumentMaterialization,
    TailCompoundArgumentMaterialization,
    AxiomArgumentMaterialization,
    PendingContinuationSnapshot,
    Count
};

#ifdef HTN_PROFILE_DETAILED
inline thread_local Source CurrentSource = Source::None;

class Scope
{
public:
    explicit Scope(const Source inSource) noexcept
        : mPrevious(CurrentSource)
    {
        CurrentSource = inSource;
    }

    ~Scope() noexcept
    {
        CurrentSource = mPrevious;
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Source mPrevious;
};

inline Source GetCurrentSource() noexcept { return CurrentSource; }
#else
class Scope
{
public:
    explicit Scope(Source) noexcept {}
};

inline Source GetCurrentSource() noexcept { return Source::None; }
#endif

inline const char* GetSourceName(const Source inSource) noexcept
{
    switch (inSource)
    {
    case Source::None: return "Unattributed";
    case Source::FactIndexKey: return "FactIndexKey";
    case Source::LiteralMaterialization: return "LiteralMaterialization";
    case Source::ListMaterialization: return "ListMaterialization";
    case Source::CallArgumentMaterialization: return "CallArgumentMaterialization";
    case Source::CompoundArgumentMaterialization: return "CompoundArgumentMaterialization";
    case Source::TailCompoundArgumentMaterialization: return "TailCompoundArgumentMaterialization";
    case Source::AxiomArgumentMaterialization: return "AxiomArgumentMaterialization";
    case Source::PendingContinuationSnapshot: return "PendingContinuationSnapshot";
    case Source::Count: break;
    }
    return "Unknown";
}
}
