// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.
#pragma once

#include "Core/HTNAtomCpp.h"

#include <cstring>
#include <string>
#include <utility>

class HtnSymbol;
class HTNAtomListOwner;

// C++ ownership bridge for the C-compatible HTNAtom representation.
//
// HTNAtom remains the data/API type shared with C. HTNAtomOwner only supplies
// RAII lifetime semantics so owning C++ containers can migrate independently
// from generated execution. It deliberately stores HTNAtom in raw storage so
// the wrapper does not depend on HTNAtom having C++ special members.
class HTNAtomOwner
{
public:
    HTNAtomOwner()
    {
        HTNAtom_Init(Get());
    }

    HTNAtomOwner(const bool inValue)
        : HTNAtomOwner()
    {
        HTNAtom_SetBool(Get(), inValue ? 1u : 0u);
    }

    HTNAtomOwner(const int32_t inValue)
        : HTNAtomOwner()
    {
        HTNAtom_SetInt(Get(), inValue);
    }

    HTNAtomOwner(const float inValue)
        : HTNAtomOwner()
    {
        HTNAtom_SetFloat(Get(), inValue);
    }

    HTNAtomOwner(const char* inValue)
        : HTNAtomOwner()
    {
        const char* Value = inValue ? inValue : "";
        HTNAtom_SetString(Get(), Value, static_cast<uint32_t>(std::strlen(Value)));
    }

    HTNAtomOwner(const std::string& inValue)
        : HTNAtomOwner()
    {
        HTNAtom_SetString(Get(), inValue.data(), static_cast<uint32_t>(inValue.size()));
    }

    HTNAtomOwner(const HtnSymbol* inValue)
        : HTNAtomOwner()
    {
        HTNAtom_SetSymbol(Get(), inValue);
    }

    HTNAtomOwner(const HTNAtomList& inValue)
        : HTNAtomOwner()
    {
        HTNAtom_SetListCopy(Get(), &inValue);
    }

    HTNAtomOwner(HTNAtomList&& inValue)
        : HTNAtomOwner()
    {
        HTNAtom_SetListMove(Get(), &inValue);
    }

    HTNAtomOwner(const HTNAtomListOwner& inValue);
    HTNAtomOwner(HTNAtomListOwner&& inValue) noexcept;


    HTNAtomOwner(const HTNAtom& inValue)
    {
        HTNAtom_Copy(Get(), &inValue);
    }

    HTNAtomOwner(HTNAtom&& inValue) noexcept
    {
        HTNAtom_Move(Get(), &inValue);
    }

    HTNAtomOwner(const HTNAtomOwner& inOther)
    {
        HTNAtom_Copy(Get(), inOther.Get());
    }

    HTNAtomOwner(HTNAtomOwner&& inOther) noexcept
    {
        HTNAtom_Move(Get(), inOther.Get());
    }

    ~HTNAtomOwner()
    {
        HTNAtom_Destroy(Get());
    }

    HTNAtomOwner& operator=(const HTNAtomOwner& inOther)
    {
        if (this != &inOther)
        {
            HTNAtom_AssignCopy(Get(), inOther.Get());
        }
        return *this;
    }

    HTNAtomOwner& operator=(HTNAtomOwner&& inOther) noexcept
    {
        if (this != &inOther)
        {
            HTNAtom_AssignMove(Get(), inOther.Get());
        }
        return *this;
    }

    HTNAtomOwner& operator=(const HTNAtom& inValue)
    {
        if (Get() != &inValue)
        {
            HTNAtom_AssignCopy(Get(), &inValue);
        }
        return *this;
    }

    HTNAtomOwner& operator=(HTNAtom&& inValue) noexcept
    {
        if (Get() != &inValue)
        {
            HTNAtom_AssignMove(Get(), &inValue);
        }
        return *this;
    }

    HTNAtom* Get()
    {
        return reinterpret_cast<HTNAtom*>(mStorage);
    }

    const HTNAtom* Get() const
    {
        return reinterpret_cast<const HTNAtom*>(mStorage);
    }

    template<typename T> decltype(auto) GetValue() const { return HTNAtomGetValue<T>(*Get()); }
    template<typename T> decltype(auto) GetValue() { return HTNAtomGetValue<T>(*Get()); }
    template<typename T> bool IsType() const { return HTNAtomIsType<T>(*Get()); }

    HTNAtomType GetType() const { return HTNAtom_GetType(Get()); }
    bool IsBound() const { return HTNAtom_IsBound(Get()) != 0; }
    void Unbind() { HTNAtom_Unbind(Get()); }
    bool PushBackElementToList(const HTNAtom& inValue) { return HTNAtom_PushBackListElement(Get(), &inValue) != 0; }
    const HTNAtom& GetListElement(uint32_t inIndex) const { return *HTNAtom_GetListElement(Get(), inIndex); }
    int32_t GetListSize() const { return HTNAtom_GetListSize(Get()); }
    bool IsListEmpty() const { return HTNAtom_IsListEmpty(Get()) != 0; }
    const char* GetStringData() const { return HTNAtom_GetStringData(Get()); }
    uint32_t GetStringSize() const { return HTNAtom_GetStringSize(Get()); }


    HTNAtomOwner& operator=(const bool inValue) { HTNAtom_SetBool(Get(), inValue ? 1u : 0u); return *this; }
    HTNAtomOwner& operator=(const int32_t inValue) { HTNAtom_SetInt(Get(), inValue); return *this; }
    HTNAtomOwner& operator=(const float inValue) { HTNAtom_SetFloat(Get(), inValue); return *this; }
    HTNAtomOwner& operator=(const HtnSymbol* inValue) { HTNAtom_SetSymbol(Get(), inValue); return *this; }
    HTNAtomOwner& operator=(const std::string& inValue) { HTNAtom_SetString(Get(), inValue.data(), static_cast<uint32_t>(inValue.size())); return *this; }

    bool operator==(const HTNAtomOwner& inOther) const { return HTNAtom_Equals(Get(), inOther.Get()) != 0; }
    bool operator!=(const HTNAtomOwner& inOther) const { return !(*this == inOther); }
    bool operator==(const HTNAtom& inOther) const { return HTNAtom_Equals(Get(), &inOther) != 0; }
    bool operator!=(const HTNAtom& inOther) const { return !(*this == inOther); }

    std::string ToString(bool inShouldDoubleQuoteString) const { return HTNAtomToString(*Get(), inShouldDoubleQuoteString); }
    std::string GetString() const { return HTNAtomGetString(*Get()); }
    void SetString(const char* inData, uint32_t inSize) { HTNAtom_SetString(Get(), inData, inSize); }
    void SetString(const std::string& inValue) { HTNAtom_SetString(Get(), inValue.data(), static_cast<uint32_t>(inValue.size())); }

    HTNAtom* operator->() { return Get(); }
    const HTNAtom* operator->() const { return Get(); }

    operator HTNAtom&() { return *Get(); }
    operator const HTNAtom&() const { return *Get(); }

private:
    alignas(HTNAtom) unsigned char mStorage[sizeof(HTNAtom)];
};

static_assert(sizeof(HTNAtomOwner) == sizeof(HTNAtom));
static_assert(alignof(HTNAtomOwner) == alignof(HTNAtom));
