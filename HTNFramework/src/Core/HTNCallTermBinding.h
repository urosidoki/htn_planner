// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include "Core/HTNAtomOwner.h"

#include "Core/HTNTypeConversion.h"
#include "Core/HTNCallTermBindingContext.h"
#include "Core/HTNCallTermRegistry.h"

#include <initializer_list>
#include <tuple>
#include <type_traits>
#include <utility>

namespace HTNCallTermBindingDetail
{
template<typename T>
using BareType = HTNTypeConversionDetail::BareType<T>;

template<typename Tuple, size_t... Indices>
HTNCallTermSignature MakeSignature(std::index_sequence<Indices...>)
{
    static_assert((HTNIsTypeConvertible<std::tuple_element_t<Indices, Tuple>> && ...),
                  "One or more HTN callterm arguments do not have an HTN type converter");

    HTNCallTermSignature Signature;
    Signature.reserve(sizeof...(Indices));
    (Signature.emplace_back(HTNGetExpectedAtomType<std::tuple_element_t<Indices, Tuple>>()), ...);
    return Signature;
}

template<typename ArgumentType>
bool TryParseArgument(const HTNAtom& inAtom, BareType<ArgumentType>& outValue)
{
    static_assert(HTNIsTypeConvertible<ArgumentType>,
                  "No HTN type conversion registered for this callterm argument type");

    if (HTNTryParseType(inAtom, outValue))
        return true;

    HTN_LOG_ERROR("Could not convert HTN callterm argument to [{}]", HTNTypeTraits<BareType<ArgumentType>>::Name);
    return false;
}

template<typename ArgumentsTuple, size_t... Indices>
bool TryParseArguments(
    const HTNCallTermArguments& inArguments,
    std::tuple<BareType<std::tuple_element_t<Indices, ArgumentsTuple>>...>& outValues,
    std::index_sequence<Indices...>)
{
    bool Success = true;

    // Deliberately use an initializer-list expansion so argument conversion is
    // evaluated from left to right and we can stop performing meaningful work
    // after the first failure without relying on function argument evaluation order.
    (void)std::initializer_list<int>{
        (Success
             ? (Success = TryParseArgument<std::tuple_element_t<Indices, ArgumentsTuple>>(
                    inArguments[Indices], std::get<Indices>(outValues)),
                0)
             : 0)...};

    return Success;
}

template<typename ReturnType>
HTNAtomOwner MakeResult(ReturnType&& inResult)
{
    using ValueType = BareType<ReturnType>;
    static_assert(HTNIsTypeConvertible<ValueType>,
                  "No HTN type conversion registered for this callterm return type");

    HTNAtomOwner Result;
    if (!HTNTryToAtom(inResult, Result))
    {
        HTN_LOG_ERROR("Could not convert callterm return value from [{}] to HTNAtom",
                      HTNTypeTraits<ValueType>::Name);
        return {};
    }

    return Result;
}

template<auto Function>
struct StaticFunctionBinding;

template<typename ReturnType, typename... ArgumentTypes, ReturnType (*Function)(ArgumentTypes...)>
struct StaticFunctionBinding<Function>
{
    using ArgumentsTuple = std::tuple<ArgumentTypes...>;
    static_assert(!std::is_void_v<ReturnType>, "HTN call terms cannot return void");
    static_assert(HTNIsTypeConvertible<ReturnType>,
                  "No HTN type conversion registered for this callterm return type");

    static constexpr size_t CallTermArgumentCount = sizeof...(ArgumentTypes);

    static void Bind(HTNCallTermRegistry& ioRegistry, const char* inID)
    {
        ioRegistry.Bind(
            inID,
            [](const HTNCallTermArguments& inArguments) -> HTNAtomOwner
            {
                return Invoke(inArguments, std::make_index_sequence<CallTermArgumentCount>{});
            },
            MakeSignature<ArgumentsTuple>(std::make_index_sequence<CallTermArgumentCount>{}));
    }

private:
    template<size_t... Indices>
    static HTNAtomOwner Invoke(const HTNCallTermArguments& inArguments,
                               std::index_sequence<Indices...> inIndices)
    {
        std::tuple<BareType<std::tuple_element_t<Indices, ArgumentsTuple>>...> ParsedArguments;

        if (!TryParseArguments<ArgumentsTuple>(inArguments, ParsedArguments, inIndices))
            return {};

        return MakeResult(Function(std::get<Indices>(ParsedArguments)...));
    }
};

template<auto Function>
struct MemberFunctionBinding;

template<typename ClassType, typename ReturnType, typename... ArgumentTypes,
         ReturnType (ClassType::*Function)(ArgumentTypes...)>
struct MemberFunctionBinding<Function>
{
    using ArgumentsTuple = std::tuple<ArgumentTypes...>;
    static_assert(!std::is_void_v<ReturnType>, "HTN call terms cannot return void");
    static_assert(HTNIsTypeConvertible<ReturnType>,
                  "No HTN type conversion registered for this callterm return type");

    static constexpr size_t CallTermArgumentCount = sizeof...(ArgumentTypes);

    static bool Bind(HTNCallTermRegistry& ioRegistry, const char* inID, const char* inDaemonID)
    {
        return ioRegistry.BindMember(
            inID,
            inDaemonID,
            [](void* inInstance, const HTNCallTermArguments& inArguments) -> HTNAtomOwner
            {
                return Invoke(*static_cast<ClassType*>(inInstance),
                              inArguments,
                              std::make_index_sequence<CallTermArgumentCount>{});
            },
            MakeSignature<ArgumentsTuple>(std::make_index_sequence<CallTermArgumentCount>{}));
    }

private:
    template<size_t... Indices>
    static HTNAtomOwner Invoke(ClassType& inInstance,
                               const HTNCallTermArguments& inArguments,
                               std::index_sequence<Indices...> inIndices)
    {
        std::tuple<BareType<std::tuple_element_t<Indices, ArgumentsTuple>>...> ParsedArguments;

        if (!TryParseArguments<ArgumentsTuple>(inArguments, ParsedArguments, inIndices))
            return {};

        return MakeResult((inInstance.*Function)(std::get<Indices>(ParsedArguments)...));
    }
};
}

// Keep the common/static binding macro fixed-arity. Avoid variadic macro
// dispatch here: MSVC's traditional preprocessor does not handle the
// argument-count selector reliably in all project configurations.
#define HTN_CALLTERM_BIND(Registry, ID, Class, Function) \
    HTNCallTermBindingDetail::StaticFunctionBinding<&Class::Function>::Bind(Registry, ID)

// Stateful daemon functions are registered once; each planner hook supplies its
// own daemon instance through HTN_CALLTERM_SET_DAEMON.
#define HTN_CALLTERM_BIND_MEMBER(Registry, ID, Class, Function) \
    HTNCallTermBindingDetail::MemberFunctionBinding<&Class::Function>::Bind(Registry, ID, #Class)

#define HTN_CALLTERM_SET_DAEMON(BindingContext, Class, Instance) \
    (BindingContext).SetDaemon(#Class, Instance)
