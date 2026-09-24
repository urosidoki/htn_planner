// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include "Core/HTNAtom.h"
#include "Core/HTNAtomOwner.h"
#include "Core/HTNAtomListOwner.h"

#include <cstring>
#include <optional>
#include <string>
#include <type_traits>

// Type information used by the HTN/C++ marshalling layer.
//
// Game and engine integrations can specialize HTNTypeTraits<T> and
// HTNTypeConverter<T> for their own native types without introducing a
// dependency from HTNFramework back to the engine.
template<typename T>
struct HTNTypeTraits
{
    static constexpr bool IsSupported = false;
};

template<typename T>
struct HTNTypeConverter;

namespace HTNTypeConversionDetail
{
template<typename T>
using BareType = std::remove_cv_t<std::remove_reference_t<T>>;
}

template<typename T>
inline constexpr bool HTNIsTypeConvertible = HTNTypeTraits<HTNTypeConversionDetail::BareType<T>>::IsSupported;

// Returns the HTNAtom representation expected by T when T has a single fixed
// representation. HTNAtom itself intentionally returns std::nullopt because it
// can contain any atom type.
template<typename T>
constexpr std::optional<HTNAtomType> HTNGetExpectedAtomType()
{
    using ValueType = HTNTypeConversionDetail::BareType<T>;
    static_assert(HTNTypeTraits<ValueType>::IsSupported, "No HTN type conversion registered for this C++ type");

    if constexpr (HTNTypeTraits<ValueType>::HasFixedAtomType)
        return HTNTypeTraits<ValueType>::AtomType;
    else
        return std::nullopt;
}

// Converts an HTNAtom into a native C++ value. Failure is a data/domain error,
// not a programming invariant, so converters report it rather than asserting.
template<typename T>
bool HTNTryParseType(void* inClientContext, const HTNAtom& inAtom, T& outValue)
{
    using ValueType = HTNTypeConversionDetail::BareType<T>;
    static_assert(HTNTypeTraits<ValueType>::IsSupported, "No HTN type conversion registered for this C++ type");
    return HTNTypeConverter<ValueType>::FromAtom(inClientContext, inAtom, outValue);
}

// Converts a native C++ value into its HTNAtom representation.
template<typename T>
bool HTNTryToAtom(void* inClientContext, const T& inValue, HTNAtom& outAtom)
{
    using ValueType = HTNTypeConversionDetail::BareType<T>;
    static_assert(HTNTypeTraits<ValueType>::IsSupported, "No HTN type conversion registered for this C++ type");
    return HTNTypeConverter<ValueType>::ToAtom(inClientContext, inValue, outAtom);
}

// Context-free convenience overloads explicitly supply a null client context.
template<typename T>
bool HTNTryParseType(const HTNAtom& inAtom, T& outValue)
{
    return HTNTryParseType(nullptr, inAtom, outValue);
}

template<typename T>
bool HTNTryToAtom(const T& inValue, HTNAtom& outAtom)
{
    return HTNTryToAtom(nullptr, inValue, outAtom);
}

// ---- HTN native representations ------------------------------------------------

template<>
struct HTNTypeTraits<HTNAtom>
{
    static constexpr bool IsSupported      = true;
    static constexpr bool HasFixedAtomType = false;
    static constexpr const char* Name      = "HTNAtom";
};

template<>
struct HTNTypeConverter<HTNAtom>
{
    static bool FromAtom(void*, const HTNAtom& inAtom, HTNAtom& outValue)
    {
        return HTNAtom_AssignCopy(&outValue, &inAtom) != 0;
    }

    static bool ToAtom(void*, const HTNAtom& inValue, HTNAtom& outAtom)
    {
        return HTNAtom_AssignCopy(&outAtom, &inValue) != 0;
    }
};


template<>
struct HTNTypeTraits<HTNAtomOwner>
{
    static constexpr bool IsSupported      = true;
    static constexpr bool HasFixedAtomType = false;
    static constexpr const char* Name      = "HTNAtomOwner";
};

template<>
struct HTNTypeConverter<HTNAtomOwner>
{
    static bool FromAtom(void*, const HTNAtom& inAtom, HTNAtomOwner& outValue)
    {
        outValue = inAtom;
        return true;
    }

    static bool ToAtom(void*, const HTNAtomOwner& inValue, HTNAtom& outAtom)
    {
        return HTNAtom_AssignCopy(&outAtom, inValue.Get()) != 0;
    }
};


template<>
struct HTNTypeTraits<HTNAtomListOwner>
{
    static constexpr bool IsSupported      = true;
    static constexpr bool HasFixedAtomType = true;
    static constexpr HTNAtomType AtomType  = HTNAtomType::HTN_ATOM_TYPE_LIST;
    static constexpr const char* Name      = "HTNAtomListOwner";
};

template<>
struct HTNTypeConverter<HTNAtomListOwner>
{
    static bool FromAtom(void*, const HTNAtom& inAtom, HTNAtomListOwner& outValue)
    {
        if (!HTNAtomIsType<HTNAtomList>(inAtom))
            return false;

        outValue = HTNAtomGetValue<HTNAtomList>(inAtom);
        return true;
    }

    static bool ToAtom(void*, const HTNAtomListOwner& inValue, HTNAtom& outAtom)
    {
        return HTNAtom_SetListCopy(&outAtom, inValue.Get()) != 0;
    }
};

#define HTN_DECLARE_DIRECT_TYPE_CONVERSION(CppType, HtnAtomType, TypeName) \
    template<> \
    struct HTNTypeTraits<CppType> \
    { \
        static constexpr bool IsSupported      = true; \
        static constexpr bool HasFixedAtomType = true; \
        static constexpr HTNAtomType AtomType  = HtnAtomType; \
        static constexpr const char* Name      = TypeName; \
    }; \
    template<> \
    struct HTNTypeConverter<CppType> \
    { \
        static bool FromAtom(void*, const HTNAtom& inAtom, CppType& outValue) \
        { \
            if (!HTNAtomIsType<CppType>(inAtom)) \
                return false; \
            outValue = HTNAtomGetValue<CppType>(inAtom); \
            return true; \
        } \
        static bool ToAtom(void*, const CppType& inValue, HTNAtom& outAtom) \
        { \
            HTNAtomOwner Value(inValue); \
            return HTNAtom_AssignCopy(&outAtom, Value.Get()) != 0; \
        } \
    }

HTN_DECLARE_DIRECT_TYPE_CONVERSION(bool, HTNAtomType::HTN_ATOM_TYPE_BOOL, "bool");
HTN_DECLARE_DIRECT_TYPE_CONVERSION(int32, HTNAtomType::HTN_ATOM_TYPE_INT, "int32");
HTN_DECLARE_DIRECT_TYPE_CONVERSION(float, HTNAtomType::HTN_ATOM_TYPE_FLOAT, "float");
HTN_DECLARE_DIRECT_TYPE_CONVERSION(std::string, HTNAtomType::HTN_ATOM_TYPE_STRING, "string");
HTN_DECLARE_DIRECT_TYPE_CONVERSION(HTNAtomList, HTNAtomType::HTN_ATOM_TYPE_LIST, "HTNAtomList");

#undef HTN_DECLARE_DIRECT_TYPE_CONVERSION

template<>
struct HTNTypeTraits<const HtnSymbol*>
{
    static constexpr bool IsSupported      = true;
    static constexpr bool HasFixedAtomType = true;
    static constexpr HTNAtomType AtomType  = HTNAtomType::HTN_ATOM_TYPE_SYMBOL;
    static constexpr const char* Name      = "HtnSymbol";
};

template<>
struct HTNTypeConverter<const HtnSymbol*>
{
    static bool FromAtom(void*, const HTNAtom& inAtom, const HtnSymbol*& outValue)
    {
        if (!HTNAtomIsType<const HtnSymbol*>(inAtom))
            return false;

        outValue = HTNAtomGetValue<const HtnSymbol*>(inAtom);
        return true;
    }

    static bool ToAtom(void*, const HtnSymbol* const& inValue, HTNAtom& outAtom)
    {
        HTNAtomOwner Value(inValue);
        return HTNAtom_AssignCopy(&outAtom, Value.Get()) != 0;
    }
};

// C text is accepted as an input representation. Copy into an owned HTN string;
// parsing into a borrowed C pointer is deliberately unsupported.
template<>
struct HTNTypeTraits<const char*> : HTNTypeTraits<std::string> {};
template<>
struct HTNTypeTraits<char*> : HTNTypeTraits<const char*> {};
template<size_t N>
struct HTNTypeTraits<char[N]> : HTNTypeTraits<const char*> {};

template<>
struct HTNTypeConverter<const char*>
{
    static bool ToAtom(void*, const char* inValue, HTNAtom& outAtom)
    {
        const char* Text = inValue ? inValue : "";
        return HTNAtom_SetString(&outAtom, Text, static_cast<uint32_t>(std::strlen(Text))) != 0;
    }
};
template<>
struct HTNTypeConverter<char*> : HTNTypeConverter<const char*> {};
template<size_t N>
struct HTNTypeConverter<char[N]> : HTNTypeConverter<const char*> {};
