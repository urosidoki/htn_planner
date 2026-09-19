// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "AI/AIHtnDaemonBase.h"

#include <cstdint>

class HTNCallTermRegistry;

class AIHtnDaemonDemoTest final : public AIHtnDaemonBase
{
public:
    static void BindCallTerms(HTNCallTermRegistry& ioRegistry);

    void OnWriteWorldState(HTNWorldState& ioWorldState) override;
    void Update(float inDeltaTime) override;

    [[nodiscard]] std::uint64_t GetTickCount() const { return mTickCount; }
    [[nodiscard]] const char* GetScenarioName() const;

private:
    bool AddTargetAvailable();

    enum class Scenario : std::uint8_t
    {
        Combat = 0,
        Emergency,
        Recovery,
        Mobility,
        Idle,
        Count
    };

    static constexpr std::uint64_t TicksPerScenario = 10u;

    std::uint64_t mTickCount = 0u;
    Scenario mScenario = Scenario::Combat;
};
