// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

#include <cstdint>
#include <string>

/**
 * Interned HTN symbol.
 *
 * A symbol string is resolved once through sGetSymbol() and can then be
 * compared by pointer identity. Returned symbols live for the lifetime of the
 * process.
 */
class HtnSymbol final
{
public:
    HTN_NODISCARD static const HtnSymbol* sGetSymbol(const char* inText);
    HTN_NODISCARD static const HtnSymbol* sGetSymbol(const std::string& inText);

    HTN_NODISCARD const std::string& GetString() const { return mText; }
    HTN_NODISCARD std::uint64_t GetHash() const { return mHash; }
    HTN_NODISCARD std::uint64_t GetID() const { return mID; }

private:
    HtnSymbol(std::string inText, std::uint64_t inHash, std::uint64_t inID)
        : mText(std::move(inText)), mHash(inHash), mID(inID) {}

    std::string mText;
    std::uint64_t mHash = 0u;
    std::uint64_t mID = 0u;
};
