// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNTypeConversion.h"
#include "HTNCoreMinimal.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Shared coordinate type used by the demo world, pathfinder and HTN marshalling.
struct Cell
{
    int32 X = 0;
    int32 Y = 0;

    bool operator==(const Cell& inOther) const
    {
        return X == inOther.X && Y == inOther.Y;
    }
};

template<>
struct HTNTypeTraits<Cell>
{
    static constexpr bool IsSupported      = true;
    static constexpr bool HasFixedAtomType = true;
    static constexpr HTNAtomType AtomType  = HTNAtomType::HTN_ATOM_TYPE_LIST;
    static constexpr const char* Name      = "Cell";
};

template<>
struct HTNTypeConverter<Cell>
{
    static bool FromAtom([[maybe_unused]] void* inClientContext, const HTNAtom& inAtom, Cell& outValue)
    {
        HTNAtomListOwner List;
        if (!HTNTryParseType(inClientContext, inAtom, List) || HTNAtomList_GetSize(List.Get()) != 2u)
            return false;

        return HTNTryParseType(inClientContext, *HTNAtomList_Get(List.Get(), 0u), outValue.X) &&
               HTNTryParseType(inClientContext, *HTNAtomList_Get(List.Get(), 1u), outValue.Y);
    }

    static bool ToAtom([[maybe_unused]] void* inClientContext, const Cell& inValue, HTNAtom& outAtom)
    {
        HTNAtomOwner X;
        HTNAtomOwner Y;
        if (!HTNTryToAtom(inClientContext, inValue.X, *X.Get()) || !HTNTryToAtom(inClientContext, inValue.Y, *Y.Get()))
            return false;

        const HTNAtomListOwner List({X, Y});
        return HTNAtom_SetListCopy(&outAtom, List.Get()) != 0;
    }
};

enum class DemoGridCellType : std::uint8_t
{
    Walkable,
    Blocked,
    Interactable
};

enum class DemoGridInteractableType : std::uint8_t
{
    Viewpoint,
    Bench,
    ShopWindow
};

struct DemoGridInteractable
{
    std::string Id;
    DemoGridInteractableType Type = DemoGridInteractableType::Viewpoint;
    Cell Location;
    const char* ContextAnimation = nullptr;
    float UsageTimeSeconds = 0.0f;
};

// Shared world terrain for the demo. Geometry and environmental affordances live
// here, independently from every NPC/daemon instance.
class DemoGridTerrain
{
public:
    static constexpr int DefaultWidth = 64;
    static constexpr int DefaultHeight = 64;
    static constexpr std::uint32_t DefaultSeed = 0xA57A1234u;
    static constexpr std::size_t DefaultInteractableCount = 12u;

    explicit DemoGridTerrain(
        int inWidth = DefaultWidth,
        int inHeight = DefaultHeight,
        std::uint32_t inSeed = DefaultSeed,
        std::size_t inInteractableCount = DefaultInteractableCount);

    [[nodiscard]] int GetWidth() const { return mWidth; }
    [[nodiscard]] int GetHeight() const { return mHeight; }
    [[nodiscard]] bool IsInside(const Cell& inCell) const;
    [[nodiscard]] DemoGridCellType GetCellType(const Cell& inCell) const;
    [[nodiscard]] bool IsBlocked(const Cell& inCell) const;
    [[nodiscard]] bool IsInteractable(const Cell& inCell) const;

    [[nodiscard]] const std::vector<DemoGridInteractable>& GetInteractables() const { return mInteractables; }
    [[nodiscard]] const DemoGridInteractable* GetInteractableAt(const Cell& inCell) const;
    [[nodiscard]] static const char* GetInteractableTypeName(DemoGridInteractableType inType);

private:
    [[nodiscard]] std::size_t ToIndex(const Cell& inCell) const;
    void BuildRandomTerrain(std::uint32_t inSeed);
    void GenerateInteractables(std::uint32_t inSeed, std::size_t inInteractableCount);
    void SetCellType(const Cell& inCell, DemoGridCellType inType);

    int mWidth = DefaultWidth;
    int mHeight = DefaultHeight;
    std::vector<DemoGridCellType> mCells;
    std::vector<DemoGridInteractable> mInteractables;
};
