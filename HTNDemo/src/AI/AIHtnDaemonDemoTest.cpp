// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "AI/AIHtnDaemonDemoTest.h"

#include "Core/HtnSymbol.h"
#include "Core/HTNCallTermBinding.h"
#include "Hook/HTNPlannerHook.h"
#include "WorldState/HTNWorldState.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
// Fact identities are interned once. Runtime writes never perform string-based
// fact lookup; HTNWorldState resolves these symbols through the active domain's
// fact registry and ignores symbols that the domain does not use.
const HtnSymbol* sControlledEntity = HtnSymbol::sGetSymbol("controlled_entity");
const HtnSymbol* sEntityCount = HtnSymbol::sGetSymbol("entity_count");
const HtnSymbol* sEntityDisabled = HtnSymbol::sGetSymbol("entity_disabled");
const HtnSymbol* sHealthValue = HtnSymbol::sGetSymbol("health_value");
const HtnSymbol* sSpeedValue = HtnSymbol::sGetSymbol("speed_value");

const HtnSymbol* sEnemyVisible = HtnSymbol::sGetSymbol("enemy_visible");
const HtnSymbol* sEnemyRecentlySeen = HtnSymbol::sGetSymbol("enemy_recently_seen");
const HtnSymbol* sEnemyHostile = HtnSymbol::sGetSymbol("enemy_hostile");
const HtnSymbol* sCriticalThreat = HtnSymbol::sGetSymbol("critical_threat");
const HtnSymbol* sEnemyClose = HtnSymbol::sGetSymbol("enemy_close");
const HtnSymbol* sLowCover = HtnSymbol::sGetSymbol("low_cover");
const HtnSymbol* sEnemyInvulnerable = HtnSymbol::sGetSymbol("enemy_invulnerable");

const HtnSymbol* sHasWeapon = HtnSymbol::sGetSymbol("has_weapon");
const HtnSymbol* sHasBackupWeapon = HtnSymbol::sGetSymbol("has_backup_weapon");
const HtnSymbol* sAmmoAvailable = HtnSymbol::sGetSymbol("ammo_available");
const HtnSymbol* sAmmoReserve = HtnSymbol::sGetSymbol("ammo_reserve");
const HtnSymbol* sWeaponJammed = HtnSymbol::sGetSymbol("weapon_jammed");

const HtnSymbol* sCoverAvailable = HtnSymbol::sGetSymbol("cover_available");
const HtnSymbol* sCoverCompromised = HtnSymbol::sGetSymbol("cover_compromised");
const HtnSymbol* sRetreatRoute = HtnSymbol::sGetSymbol("retreat_route");
const HtnSymbol* sRouteBlocked = HtnSymbol::sGetSymbol("route_blocked");

const HtnSymbol* sNeedsHealing = HtnSymbol::sGetSymbol("needs_healing");
const HtnSymbol* sUnderFire = HtnSymbol::sGetSymbol("under_fire");
const HtnSymbol* sMedkit = HtnSymbol::sGetSymbol("medkit");
const HtnSymbol* sHealingStationNearby = HtnSymbol::sGetSymbol("healing_station_nearby");
const HtnSymbol* sHealingStation = HtnSymbol::sGetSymbol("healing_station");
const HtnSymbol* sStationOffline = HtnSymbol::sGetSymbol("station_offline");

const HtnSymbol* sObjectiveFar = HtnSymbol::sGetSymbol("objective_far");
const HtnSymbol* sPathOpen = HtnSymbol::sGetSymbol("path_open");
const HtnSymbol* sAlternatePathOpen = HtnSymbol::sGetSymbol("alternate_path_open");
const HtnSymbol* sMovementBlocked = HtnSymbol::sGetSymbol("movement_blocked");
const HtnSymbol* sRouteClear = HtnSymbol::sGetSymbol("route_clear");
const HtnSymbol* sRouteDangerous = HtnSymbol::sGetSymbol("route_dangerous");
const HtnSymbol* sAlternateRoute = HtnSymbol::sGetSymbol("alternate_route");

struct DynamicFact
{
    const HtnSymbol* Symbol;
    std::size_t ArgumentCount;
};

// These are the facts owned by this daemon. Rebuilding them every tick makes
// WriteWorldState idempotent and ensures a phase transition cannot inherit stale
// rows from either the source .worldstate file or the previous phase.
const std::array<DynamicFact, 31> sDynamicFacts = {{
    {sControlledEntity, 1u},
    {sEntityCount, 1u},
    {sEntityDisabled, 1u},
    {sHealthValue, 2u},
    {sSpeedValue, 2u},
    {sEnemyVisible, 2u},
    {sEnemyRecentlySeen, 2u},
    {sEnemyHostile, 1u},
    {sCriticalThreat, 1u},
    {sEnemyClose, 1u},
    {sLowCover, 1u},
    {sEnemyInvulnerable, 1u},
    {sHasWeapon, 1u},
    {sHasBackupWeapon, 1u},
    {sAmmoAvailable, 1u},
    {sAmmoReserve, 1u},
    {sWeaponJammed, 1u},
    {sCoverAvailable, 2u},
    {sCoverCompromised, 1u},
    {sRetreatRoute, 2u},
    {sRouteBlocked, 1u},
    {sNeedsHealing, 1u},
    {sUnderFire, 1u},
    {sMedkit, 1u},
    {sHealingStationNearby, 1u},
    {sHealingStation, 2u},
    {sStationOffline, 1u},
    {sObjectiveFar, 1u},
    {sPathOpen, 1u},
    {sAlternatePathOpen, 1u},
    {sMovementBlocked, 1u}
}};

// Route facts are separated only to keep the fixed-size table above readable.
const std::array<DynamicFact, 3> sDynamicRouteFacts = {{
    {sRouteClear, 2u},
    {sRouteDangerous, 1u},
    {sAlternateRoute, 2u}
}};

constexpr std::int32_t kControlledEntity = 7;
constexpr std::int32_t kEmergencyEnemy = 101;
constexpr std::int32_t kCombatEnemy = 202;
constexpr std::int32_t kHealth = 50;
constexpr float kMaxSpeed = 1.0f;

void ClearDaemonFacts(HTNWorldState& ioWorldState)
{
    for (const DynamicFact& Fact : sDynamicFacts)
        (void)ioWorldState.ClearFact(Fact.Symbol, Fact.ArgumentCount);
    for (const DynamicFact& Fact : sDynamicRouteFacts)
        (void)ioWorldState.ClearFact(Fact.Symbol, Fact.ArgumentCount);
}
}

void AIHtnDaemonDemoTest::BindCallTerms(HTNCallTermRegistry& ioRegistry)
{
    HTN_CALLTERM_BIND_MEMBER(
        ioRegistry, "add_target_available", AIHtnDaemonDemoTest, AddTargetAvailable);
}

bool AIHtnDaemonDemoTest::AddTargetAvailable()
{
    if (!mWorldState)
        return false;

    const std::vector<HTNAtomOwner> Arguments{HTNAtomOwner(std::string("enemy0"))};
    mWorldState->AddFact("target_available", Arguments);
    return true;
}

void AIHtnDaemonDemoTest::OnWriteWorldState(HTNWorldState& ioWorldState)
{
    ClearDaemonFacts(ioWorldState);

    // Keep the entry method deterministic in every selected source world state.
    // In particular, clearing entity_count disables the recursive stress branch
    // so the five daemon phases exercise the same single-entity decision tree.
    (void)ioWorldState.WriteFact(sControlledEntity, kControlledEntity);
    (void)ioWorldState.WriteFact(sHealthValue, kControlledEntity, kHealth);
    (void)ioWorldState.WriteFact(sSpeedValue, kControlledEntity, kMaxSpeed);

    switch (mScenario)
    {
    case Scenario::Combat:
        (void)ioWorldState.WriteFact(sEnemyRecentlySeen, kControlledEntity, kCombatEnemy);
        (void)ioWorldState.WriteFact(sEnemyHostile, kCombatEnemy);
        (void)ioWorldState.WriteFact(sHasBackupWeapon, kControlledEntity);
        (void)ioWorldState.WriteFact(sAmmoAvailable, kControlledEntity);
        break;

    case Scenario::Emergency:
        (void)ioWorldState.WriteFact(sEnemyVisible, kControlledEntity, kEmergencyEnemy);
        (void)ioWorldState.WriteFact(sEnemyHostile, kEmergencyEnemy);
        (void)ioWorldState.WriteFact(sCriticalThreat, kEmergencyEnemy);
        (void)ioWorldState.WriteFact(sEnemyClose, kEmergencyEnemy);
        (void)ioWorldState.WriteFact(sHasWeapon, kControlledEntity);
        (void)ioWorldState.WriteFact(sAmmoAvailable, kControlledEntity);
        (void)ioWorldState.WriteFact(sCoverAvailable, kControlledEntity, "daemon_cover_A");
        break;

    case Scenario::Recovery:
        (void)ioWorldState.WriteFact(sNeedsHealing, kControlledEntity);
        (void)ioWorldState.WriteFact(sMedkit, kControlledEntity);
        break;

    case Scenario::Mobility:
        (void)ioWorldState.WriteFact(sObjectiveFar, kControlledEntity);
        (void)ioWorldState.WriteFact(sPathOpen, kControlledEntity);
        (void)ioWorldState.WriteFact(sRouteClear, kControlledEntity, "daemon_route_A");
        break;

    case Scenario::Idle:
    case Scenario::Count:
        // Base facts alone deliberately select branch_idle.
        break;
    }
}

void AIHtnDaemonDemoTest::Update(float inDeltaTime)
{
    (void)inDeltaTime;

    ++mTickCount;
    if ((mTickCount % TicksPerScenario) != 0u)
        return;

    const std::uint8_t NextScenario =
        (static_cast<std::uint8_t>(mScenario) + 1u) % static_cast<std::uint8_t>(Scenario::Count);
    mScenario = static_cast<Scenario>(NextScenario);
}

const char* AIHtnDaemonDemoTest::GetScenarioName() const
{
    switch (mScenario)
    {
    case Scenario::Combat: return "Combat";
    case Scenario::Emergency: return "Emergency";
    case Scenario::Recovery: return "Recovery";
    case Scenario::Mobility: return "Mobility";
    case Scenario::Idle: return "Idle";
    case Scenario::Count: break;
    }
    return "Unknown";
}
