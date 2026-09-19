// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"
#include "Core/HTNDecompositionStatus.h"
#include "Core/HTNPlannerExecutionContext.h"
#include "Core/HTNAtomOwner.h"

#include "Core/HTNCallTermBindingContext.h"
#include "Core/HTNCallTermRegistry.h"
#include "WorldState/HTNFactRegistry.h"

#include <memory>
#include <string>

class HTNWorldState;
struct HTNGeneratedPlannerDefinition;

/**
 * Per-entity planner facade. Each entity owns its own hook and daemon binding context.
 * The referenced WorldState is permanent and must outlive the hook.
 *
 * Threading contract:
 * - Configure the hook (SetGeneratedPlannerDefinition and daemon bindings)
 *   before starting planning for that entity.
 * - The referenced callterm registry is configured once and may be shared read-only
 *   by any number of entity-owned hooks.
 * - Mutating configuration while any planning call is in flight is unsupported and
 *   intentionally not synchronized.
 */
class HTNPlannerHook
{
public:
    explicit HTNPlannerHook(HTNWorldState& inWorldState);
    HTNPlannerHook(HTNWorldState& inWorldState, const HTNCallTermRegistry& inCallTermRegistry);

    // Performs a decomposition on this entity's domain and WorldState.
    // Different entity-owned hooks may execute in parallel; concurrent decomposition
    // through the same hook is unsupported.
    HTNDecompositionStatus Decompose(const HTNPlannerExecutionContext& inExecutionContext, HTNAtomOwner& outPlan, bool inRequireTopLevel = true) const;

    // Selects the generated backend. The definition is immutable and may be shared
    // by any number of planning units/threads.
    // Configuration-time only: do not change it while planning calls are in flight.
    // Selects a generated definition only when its ABI and lifecycle contract match this runtime.
    // Passing nullptr clears the generated backend.
    HTN_NODISCARD bool SetGeneratedPlannerDefinition(
        const HTNGeneratedPlannerDefinition* inGeneratedPlannerDefinition);
    HTN_NODISCARD const HTNGeneratedPlannerDefinition* GetGeneratedPlannerDefinition() const;
    HTN_NODISCARD bool HasGeneratedPlannerDefinition() const;
    HTN_NODISCARD const void* GetGeneratedPreparedStorage() const;

    HTN_NODISCARD HTNCallTermBindingContext& GetCallTermBindingContext();
    HTN_NODISCARD const HTNCallTermBindingContext& GetCallTermBindingContext() const;
    HTN_NODISCARD HTNWorldState& GetWorldState();
    HTN_NODISCARD const HTNWorldState& GetWorldState() const;

    // Returns the compact slot assigned by the configured fact registry.
    // Unregistered facts return HTN_INVALID_FACT_SLOT.
    HTN_NODISCARD HTNFactSlot FindFactSlot(const HtnSymbol* inSymbol) const;

    // Returns the domain-specific fact registry.
    HTN_NODISCARD const HTNFactRegistry& GetFactRegistry() const;
    // Configure host-published facts before daemon updates/planning. Never mutate
    // this registry concurrently with its readers.
    HTN_NODISCARD HTNFactRegistry& GetFactRegistry();

private:
    HTNWorldState* mWorldState;

    const HTNGeneratedPlannerDefinition* mGeneratedPlannerDefinition = nullptr;
    std::shared_ptr<void> mGeneratedPreparedStorage;

    HTNCallTermBindingContext mCallTermBindingContext;
    HTNFactRegistry mFactRegistry;
};

inline const HTNGeneratedPlannerDefinition* HTNPlannerHook::GetGeneratedPlannerDefinition() const
{
    return mGeneratedPlannerDefinition;
}

inline bool HTNPlannerHook::HasGeneratedPlannerDefinition() const
{
    return mGeneratedPlannerDefinition != nullptr;
}

inline const void* HTNPlannerHook::GetGeneratedPreparedStorage() const
{
    return mGeneratedPreparedStorage.get();
}

inline HTNCallTermBindingContext& HTNPlannerHook::GetCallTermBindingContext()
{
    return mCallTermBindingContext;
}

inline const HTNCallTermBindingContext& HTNPlannerHook::GetCallTermBindingContext() const
{
    return mCallTermBindingContext;
}

inline HTNWorldState& HTNPlannerHook::GetWorldState()
{
    return *mWorldState;
}

inline const HTNWorldState& HTNPlannerHook::GetWorldState() const
{
    return *mWorldState;
}

inline HTNFactSlot HTNPlannerHook::FindFactSlot(const HtnSymbol* inSymbol) const
{
    return mFactRegistry.FindSlot(inSymbol);
}

inline const HTNFactRegistry& HTNPlannerHook::GetFactRegistry() const
{
    return mFactRegistry;
}

inline HTNFactRegistry& HTNPlannerHook::GetFactRegistry()
{
    return mFactRegistry;
}
