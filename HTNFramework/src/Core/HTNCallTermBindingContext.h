// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include "Translator/HTNCallTermBridge.h"
#include "Core/HTNMissingCallTerm.h"
#include "HTNCoreMinimal.h"

#include <array>
#include <cstddef>
#include <string>

class HTNCallTermRegistry;

inline constexpr std::size_t HTN_MAX_CALLTERM_DAEMON_TYPES = 32u;

/**
 * Per-planner-hook daemon instances used by shared callterm bindings.
 *
 * The referenced registry and every configured daemon must outlive this context.
 * Configure daemon slots before planning; concurrent mutation and execution are unsupported.
 */
class HTNCallTermBindingContext
{
public:
    explicit HTNCallTermBindingContext(const HTNCallTermRegistry& inRegistry);

    bool SetDaemon(const std::string& inID, void* inDaemon);
    HTN_NODISCARD const HTNCallTermRegistry& GetRegistry() const;

private:
    HTN_NODISCARD void* GetDaemon(std::size_t inSlot) const;

    const HTNCallTermRegistry& mRegistry;
    std::array<void*, HTN_MAX_CALLTERM_DAEMON_TYPES> mDaemonSlots{};

    friend class HTNCallTermRegistry;
    friend HTNGeneratedCallTerm HTNCallTermRegistry_ResolveGeneratedCallTerm(
        const HTNCallTermBindingContext* callterm_context,
        const char* name);
};
