// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.
#pragma once

#include "Core/HTNAtomC.h"
#include "Core/HTNAtomOwner.h"

#include <initializer_list>
#include <utility>

class HTNAtomListAllocator;

// C++ RAII owner for the plain C HTNAtomList representation.
// Generated/runtime C storage uses HTNAtomList directly with the explicit C API.
class HTNAtomListOwner
{
public:
    HTNAtomListOwner()
    {
        HTNAtomList_Init(Get());
    }

    explicit HTNAtomListOwner(HTNAtomListAllocator& inAllocator)
    {
        HTNAtomList_InitWithAllocator(Get(), &inAllocator);
    }

    HTNAtomListOwner(std::initializer_list<HTNAtomOwner> inElements)
        : HTNAtomListOwner()
    {
        for (const HTNAtomOwner& Element : inElements)
            HTNAtomList_PushBack(Get(), Element.Get());
    }

    HTNAtomListOwner(HTNAtomListAllocator& inAllocator, std::initializer_list<HTNAtomOwner> inElements)
        : HTNAtomListOwner(inAllocator)
    {
        for (const HTNAtomOwner& Element : inElements)
            HTNAtomList_PushBack(Get(), Element.Get());
    }

    HTNAtomListOwner(const HTNAtomList& inList)
    {
        HTNAtomList_InitWithAllocator(Get(), inList.allocator);
        HTNAtomList_Copy(Get(), &inList);
    }

    HTNAtomListOwner(HTNAtomList&& inList) noexcept
    {
        HTNAtomList_InitWithAllocator(Get(), inList.allocator);
        HTNAtomList_Move(Get(), &inList);
    }

    HTNAtomListOwner(const HTNAtomListOwner& inOther)
    {
        HTNAtomList_InitWithAllocator(Get(), inOther.Get()->allocator);
        HTNAtomList_Copy(Get(), inOther.Get());
    }

    HTNAtomListOwner(HTNAtomListOwner&& inOther) noexcept
    {
        HTNAtomList_InitWithAllocator(Get(), inOther.Get()->allocator);
        HTNAtomList_Move(Get(), inOther.Get());
    }

    ~HTNAtomListOwner()
    {
        HTNAtomList_Destroy(Get());
    }

    HTNAtomListOwner& operator=(const HTNAtomListOwner& inOther)
    {
        if (this != &inOther)
            HTNAtomList_Copy(Get(), inOther.Get());
        return *this;
    }

    HTNAtomListOwner& operator=(HTNAtomListOwner&& inOther) noexcept
    {
        if (this != &inOther)
            HTNAtomList_Move(Get(), inOther.Get());
        return *this;
    }

    HTNAtomListOwner& operator=(const HTNAtomList& inList)
    {
        if (Get() != &inList)
            HTNAtomList_Copy(Get(), &inList);
        return *this;
    }

    bool PushBack(const HTNAtom& inAtom)
    {
        return HTNAtomList_PushBack(Get(), &inAtom) != 0;
    }

    HTNAtomList* Get() { return reinterpret_cast<HTNAtomList*>(mStorage); }
    const HTNAtomList* Get() const { return reinterpret_cast<const HTNAtomList*>(mStorage); }

    operator HTNAtomList&() { return *Get(); }
    operator const HTNAtomList&() const { return *Get(); }

private:
    alignas(HTNAtomList) unsigned char mStorage[sizeof(HTNAtomList)];
};

static_assert(sizeof(HTNAtomListOwner) == sizeof(HTNAtomList));
static_assert(alignof(HTNAtomListOwner) == alignof(HTNAtomList));

inline HTNAtomOwner::HTNAtomOwner(const HTNAtomListOwner& inValue)
    : HTNAtomOwner()
{
    HTNAtom_SetListCopy(Get(), inValue.Get());
}

inline HTNAtomOwner::HTNAtomOwner(HTNAtomListOwner&& inValue) noexcept
    : HTNAtomOwner()
{
    HTNAtom_SetListMove(Get(), inValue.Get());
}
