// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.
#pragma once

#include "Core/HTNAtomC.h"
#include "Core/HtnSymbol.h"
#include "HTNCoreMinimal.h"

#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>


// Forward declarations for the optional C++ marshalling layer. External/game-engine
// types can register HTNTypeTraits/HTNTypeConverter specializations in
// HTNTypeConversion.h without making HTNAtom depend on any engine type.
template<typename T> struct HTNTypeTraits;
template<typename T> struct HTNTypeConverter;

static_assert(std::is_standard_layout_v<HTNAtom>);
static_assert(std::is_trivially_copyable_v<HTNAtom>);
static_assert(std::is_trivially_destructible_v<HTNAtom>);
static_assert(std::is_standard_layout_v<HTNAtomList>);
static_assert(std::is_trivially_copyable_v<HTNAtomList>);
static_assert(std::is_trivially_destructible_v<HTNAtomList>);

template<typename T>
inline decltype(auto) HTNAtomGetValue(const HTNAtom& inAtom)
{
    if constexpr (std::is_same_v<T, bool>) return inAtom.value.bool_value != 0u;
    else if constexpr (std::is_same_v<T, int32>) return (inAtom.value.int_value);
    else if constexpr (std::is_same_v<T, float>) return (inAtom.value.float_value);
    else if constexpr (std::is_same_v<T, std::string>)
    {
        const char* Data = HTNAtom_GetStringData(&inAtom);
        return Data ? std::string(Data, HTNAtom_GetStringSize(&inAtom)) : std::string();
    }
    else if constexpr (std::is_same_v<T, const HtnSymbol*>) return static_cast<const HtnSymbol*>(inAtom.value.symbol_value);
    else if constexpr (std::is_same_v<T, HTNAtomList>) return (inAtom.value.list_value);
    else static_assert(!sizeof(T), "Unsupported HTNAtomGetValue type");
}

template<typename T>
inline decltype(auto) HTNAtomGetValue(HTNAtom& ioAtom)
{
    if constexpr (std::is_same_v<T, bool>) return ioAtom.value.bool_value != 0u;
    else if constexpr (std::is_same_v<T, int32>) return (ioAtom.value.int_value);
    else if constexpr (std::is_same_v<T, float>) return (ioAtom.value.float_value);
    else if constexpr (std::is_same_v<T, std::string>)
    {
        const char* Data = HTNAtom_GetStringData(&ioAtom);
        return Data ? std::string(Data, HTNAtom_GetStringSize(&ioAtom)) : std::string();
    }
    else if constexpr (std::is_same_v<T, const HtnSymbol*>) return static_cast<const HtnSymbol*>(ioAtom.value.symbol_value);
    else if constexpr (std::is_same_v<T, HTNAtomList>) return (ioAtom.value.list_value);
    else static_assert(!sizeof(T), "Unsupported HTNAtomGetValue type");
}

template<typename T>
inline bool HTNAtomIsType(const HTNAtom& inAtom)
{
    const HTNAtomType Type = HTNAtom_GetType(&inAtom);
    if constexpr (std::is_same_v<T, bool>) return Type == HTN_ATOM_TYPE_BOOL;
    else if constexpr (std::is_same_v<T, int32>) return Type == HTN_ATOM_TYPE_INT;
    else if constexpr (std::is_same_v<T, float>) return Type == HTN_ATOM_TYPE_FLOAT;
    else if constexpr (std::is_same_v<T, std::string>) return Type == HTN_ATOM_TYPE_STRING;
    else if constexpr (std::is_same_v<T, const HtnSymbol*>) return Type == HTN_ATOM_TYPE_SYMBOL;
    else if constexpr (std::is_same_v<T, HTNAtomList>) return Type == HTN_ATOM_TYPE_LIST;
    else return false;
}

inline HTNAtomType HTNAtomGetType(const HTNAtom& inAtom) { return HTNAtom_GetType(&inAtom); }
inline bool HTNAtomIsBound(const HTNAtom& inAtom) { return HTNAtom_IsBound(&inAtom) != 0; }
inline void HTNAtomUnbind(HTNAtom& ioAtom) { HTNAtom_Unbind(&ioAtom); }
inline const char* HTNAtomGetStringData(const HTNAtom& inAtom) { return HTNAtom_GetStringData(&inAtom); }
inline uint32_t HTNAtomGetStringSize(const HTNAtom& inAtom) { return HTNAtom_GetStringSize(&inAtom); }
inline void HTNAtomSetString(HTNAtom& ioAtom, const char* inData, uint32_t inSize) { HTNAtom_SetString(&ioAtom, inData, inSize); }
inline void HTNAtomSetString(HTNAtom& ioAtom, const std::string& inValue) { HTNAtom_SetString(&ioAtom, inValue.data(), static_cast<uint32_t>(inValue.size())); }
inline bool HTNAtomPushBackElementToList(HTNAtom& ioAtom, const HTNAtom& inValue) { return HTNAtom_PushBackListElement(&ioAtom, &inValue) != 0; }
inline const HTNAtom& HTNAtomGetListElement(const HTNAtom& inAtom, uint32_t inIndex) { return *HTNAtom_GetListElement(&inAtom, inIndex); }
inline int32_t HTNAtomGetListSize(const HTNAtom& inAtom) { return HTNAtom_GetListSize(&inAtom); }
inline bool HTNAtomIsListEmpty(const HTNAtom& inAtom) { return HTNAtom_IsListEmpty(&inAtom) != 0; }


namespace HTNAtomDetail
{
template<typename T, typename = void>
struct HasRegisteredTypeConversion : std::false_type {};

template<typename T>
struct HasRegisteredTypeConversion<T, std::void_t<decltype(HTNTypeTraits<T>::IsSupported)>>
    : std::bool_constant<HTNTypeTraits<T>::IsSupported> {};

template<typename T>
inline int AssignCallArgument(void* inClientContext, HTNAtom& outAtom, T&& inValue)
{
    using TValue = std::decay_t<T>;

    if constexpr (std::is_same_v<TValue, HTNAtom>)
    {
        return HTNAtom_AssignCopy(&outAtom, &inValue);
    }
    else if constexpr (std::is_same_v<TValue, bool>)
    {
        HTNAtom_SetBool(&outAtom, inValue ? 1u : 0u);
        return 1;
    }
    else if constexpr (std::is_enum_v<TValue>)
    {
        HTNAtom_SetInt(&outAtom, static_cast<int32_t>(inValue));
        return 1;
    }
    else if constexpr (std::is_integral_v<TValue> && !std::is_same_v<TValue, bool>)
    {
        static_assert(sizeof(TValue) <= sizeof(int32_t), "HTNAtom call integer arguments must fit in int32");
        HTNAtom_SetInt(&outAtom, static_cast<int32_t>(inValue));
        return 1;
    }
    else if constexpr (std::is_same_v<TValue, float>)
    {
        HTNAtom_SetFloat(&outAtom, inValue);
        return 1;
    }
    else if constexpr (std::is_same_v<TValue, const HtnSymbol*> || std::is_same_v<TValue, HtnSymbol*>)
    {
        HTNAtom_SetSymbol(&outAtom, inValue);
        return inValue ? 1 : 0;
    }
    else if constexpr (std::is_same_v<TValue, std::string>)
    {
        return HTNAtom_SetString(&outAtom, inValue.data(), static_cast<uint32_t>(inValue.size()));
    }
    else if constexpr (std::is_same_v<TValue, std::string_view>)
    {
        return HTNAtom_SetString(&outAtom, inValue.data(), static_cast<uint32_t>(inValue.size()));
    }
    else if constexpr (std::is_same_v<TValue, const char*> || std::is_same_v<TValue, char*>)
    {
        const char* Value = inValue ? inValue : "";
        return HTNAtom_SetString(&outAtom, Value, static_cast<uint32_t>(std::char_traits<char>::length(Value)));
    }
    else if constexpr (HasRegisteredTypeConversion<TValue>::value)
    {
        return HTNTypeConverter<TValue>::ToAtom(inClientContext, inValue, outAtom) ? 1 : 0;
    }
    else
    {
        static_assert(!sizeof(TValue), "Unsupported HTNAtom::sCreateCall argument type; register HTNTypeTraits/HTNTypeConverter for custom types");
    }
}
}

template<typename... TArguments>
inline HTNAtom HTNAtom::sCreateCall(const HtnSymbol* inHead, TArguments&&... inArguments)
{
    return sCreateCallWithContext(nullptr, inHead, std::forward<TArguments>(inArguments)...);
}

template<typename... TArguments>
inline HTNAtom HTNAtom::sCreateCallWithContext(void* inClientContext, const HtnSymbol* inHead, TArguments&&... inArguments)
{
    constexpr size_t ArgumentCount = sizeof...(TArguments);
    std::array<HTNAtom, ArgumentCount> Arguments{};
    HTNAtom_InitRange(Arguments.data(), static_cast<uint32_t>(ArgumentCount));

    size_t ArgumentIndex = 0u;
    int ArgumentsValid = 1;
    ((ArgumentsValid = ArgumentsValid && HTNAtomDetail::AssignCallArgument(
        inClientContext, Arguments[ArgumentIndex++], std::forward<TArguments>(inArguments))), ...);

    HTNAtom Result;
    if (ArgumentsValid)
        HTNAtom_CreateCall(&Result, inHead, Arguments.data(), static_cast<uint32_t>(ArgumentCount));
    else
        HTNAtom_Init(&Result);

    HTNAtom_DestroyRange(Arguments.data(), static_cast<uint32_t>(ArgumentCount));
    return Result;
}

inline void HTNAtom::sDestroy(HTNAtom& ioAtom)
{
    HTNAtom_Destroy(&ioAtom);
}

std::string HTNAtomGetString(const HTNAtom& inAtom);
std::string HTNAtomToString(const HTNAtom& inAtom, bool inShouldDoubleQuoteString);

inline bool operator==(const HTNAtom& inLeft, const HTNAtom& inRight) { return HTNAtom_Equals(&inLeft, &inRight) != 0; }
inline bool operator!=(const HTNAtom& inLeft, const HTNAtom& inRight) { return !(inLeft == inRight); }
