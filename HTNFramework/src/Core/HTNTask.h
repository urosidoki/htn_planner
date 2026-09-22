// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include "Core/HTNAtom.h"
#include "Core/HTNDomainSyntax.h"
#include "Core/HtnSymbol.h"
#include "HTNCoreMinimal.h"

#include <string>
#include <vector>

// A runtime HTN call has one representation everywhere: a list whose first
// element is the call head symbol and whose remaining elements are arguments:
// (head arg0 ... argN). Plan steps reuse that same representation. Their head
// prefix carries the plan-step semantics: !primitive and &deferred-call.
enum class HTNPlanStepKind : uint8
{
    Invalid = 0,
    PrimitiveTask,
    DeferredCall
};

HTN_NODISCARD inline bool HTNIsPrimitiveTaskHead(const HtnSymbol* inHead)
{
    return inHead && !inHead->GetString().empty() && inHead->GetString().front() == HTNPrimitiveTaskPrefix;
}

HTN_NODISCARD inline bool HTNIsDeferredCallHead(const HtnSymbol* inHead)
{
    return inHead && !inHead->GetString().empty() && inHead->GetString().front() == HTNDeferredCallPrefix;
}

HTN_NODISCARD inline const HtnSymbol* HTNMakeDeferredCallHead(const HtnSymbol* inUnprefixedHead)
{
    if (!inUnprefixedHead)
        return nullptr;

    if (HTNIsDeferredCallHead(inUnprefixedHead))
        return inUnprefixedHead;

    return HtnSymbol::sGetSymbol(std::string(1u, HTNDeferredCallPrefix) + inUnprefixedHead->GetString());
}

HTN_NODISCARD inline const HtnSymbol* HTNMakeDeferredCallHead(const std::string& inUnprefixedHead)
{
    if (!inUnprefixedHead.empty() && inUnprefixedHead.front() == HTNDeferredCallPrefix)
        return HtnSymbol::sGetSymbol(inUnprefixedHead);

    return HtnSymbol::sGetSymbol(std::string(1u, HTNDeferredCallPrefix) + inUnprefixedHead);
}

HTN_NODISCARD inline const HtnSymbol* HTNMakePrimitiveTaskHead(const HtnSymbol* inUnprefixedHead)
{
    if (!inUnprefixedHead)
        return nullptr;

    if (HTNIsPrimitiveTaskHead(inUnprefixedHead))
        return inUnprefixedHead;

    return HtnSymbol::sGetSymbol(std::string(1u, HTNPrimitiveTaskPrefix) + inUnprefixedHead->GetString());
}

HTN_NODISCARD inline const HtnSymbol* HTNMakePrimitiveTaskHead(const std::string& inUnprefixedHead)
{
    if (!inUnprefixedHead.empty() && inUnprefixedHead.front() == HTNPrimitiveTaskPrefix)
        return HtnSymbol::sGetSymbol(inUnprefixedHead);

    return HtnSymbol::sGetSymbol(std::string(1u, HTNPrimitiveTaskPrefix) + inUnprefixedHead);
}

HTN_NODISCARD inline HTNAtomOwner HTNMakeCall(const HtnSymbol* inHead, const std::vector<HTNAtomOwner>& inArguments)
{
    HTNAtomOwner Call;
    Call.PushBackElementToList(HTNAtomOwner(inHead));
    for (const HTNAtomOwner& Argument : inArguments)
        Call.PushBackElementToList(Argument);
    return Call;
}

HTN_NODISCARD inline bool HTNIsValidCall(const HTNAtom* inCall)
{
    if (!inCall || HTNAtom_GetType(inCall) != HTN_ATOM_TYPE_LIST || HTNAtom_GetListSize(inCall) < 1)
        return false;

    const HTNAtom* Head = HTNAtom_GetListElement(inCall, 0u);
    return Head && HTNAtom_GetType(Head) == HTN_ATOM_TYPE_SYMBOL && Head->value.symbol_value != nullptr;
}

HTN_NODISCARD inline const HtnSymbol* HTNGetCallHead(const HTNAtom* inCall)
{
    if (!HTNIsValidCall(inCall))
        return nullptr;

    return static_cast<const HtnSymbol*>(HTNAtom_GetListElement(inCall, 0u)->value.symbol_value);
}

HTN_NODISCARD inline uint32 HTNGetCallArgumentCount(const HTNAtom* inCall)
{
    return HTNIsValidCall(inCall) ? static_cast<uint32>(HTNAtom_GetListSize(inCall) - 1) : 0u;
}

HTN_NODISCARD inline const HTNAtom* HTNFindCallArgument(const HTNAtom* inCall, const uint32 inArgumentIndex)
{
    if (!HTNIsValidCall(inCall) || inArgumentIndex >= HTNGetCallArgumentCount(inCall))
        return nullptr;

    return HTNAtom_GetListElement(inCall, inArgumentIndex + 1u);
}

HTN_NODISCARD inline bool HTNIsValidCall(const HTNAtomOwner& inCall)
{
    return HTNIsValidCall(inCall.Get());
}

HTN_NODISCARD inline const HtnSymbol* HTNGetCallHead(const HTNAtomOwner& inCall)
{
    return HTNGetCallHead(inCall.Get());
}

HTN_NODISCARD inline uint32 HTNGetCallArgumentCount(const HTNAtomOwner& inCall)
{
    return HTNGetCallArgumentCount(inCall.Get());
}

HTN_NODISCARD inline const HTNAtom* HTNFindCallArgument(const HTNAtomOwner& inCall, const uint32 inArgumentIndex)
{
    return HTNFindCallArgument(inCall.Get(), inArgumentIndex);
}

HTN_NODISCARD inline HTNPlanStepKind HTNGetPlanStepKind(const HTNAtom* inStep)
{
    const HtnSymbol* Head = HTNGetCallHead(inStep);
    if (HTNIsPrimitiveTaskHead(Head))
        return HTNPlanStepKind::PrimitiveTask;
    if (HTNIsDeferredCallHead(Head))
        return HTNPlanStepKind::DeferredCall;
    return HTNPlanStepKind::Invalid;
}

HTN_NODISCARD inline HTNPlanStepKind HTNGetPlanStepKind(const HTNAtomOwner& inStep)
{
    return HTNGetPlanStepKind(inStep.Get());
}

HTN_NODISCARD inline HTNAtomOwner HTNMakeCallFromDeferredPlanStep(const HTNAtomOwner& inStep)
{
    if (HTNGetPlanStepKind(inStep) != HTNPlanStepKind::DeferredCall)
        return HTNAtomOwner{};

    const HtnSymbol* DeferredHead = HTNGetCallHead(inStep);
    const std::string& DeferredName = DeferredHead->GetString();
    HTNAtomOwner Call;
    Call.PushBackElementToList(HTNAtomOwner(HtnSymbol::sGetSymbol(DeferredName.substr(1u))));

    const uint32 ArgumentCount = HTNGetCallArgumentCount(inStep);
    for (uint32 ArgumentIndex = 0u; ArgumentIndex < ArgumentCount; ++ArgumentIndex)
    {
        const HTNAtom* Argument = HTNFindCallArgument(inStep, ArgumentIndex);
        if (Argument)
            Call.PushBackElementToList(*Argument);
    }
    return Call;
}

HTN_NODISCARD inline bool HTNIsValidTask(const HTNAtomOwner& inTask)
{
    return HTNIsValidCall(inTask);
}

HTN_NODISCARD inline const HtnSymbol* HTNGetTaskHead(const HTNAtomOwner& inTask)
{
    return HTNGetCallHead(inTask);
}

HTN_NODISCARD inline bool HTNIsPrimitiveTask(const HTNAtomOwner& inTask)
{
    return HTNIsPrimitiveTaskHead(HTNGetTaskHead(inTask));
}

HTN_NODISCARD inline uint32 HTNGetTaskArgumentCount(const HTNAtomOwner& inTask)
{
    return HTNGetCallArgumentCount(inTask);
}

HTN_NODISCARD inline const HTNAtom* HTNFindTaskArgument(const HTNAtomOwner& inTask, const uint32 inArgumentIndex)
{
    return HTNFindCallArgument(inTask, inArgumentIndex);
}

HTN_NODISCARD inline const HTNAtom& HTNGetTaskArgument(const HTNAtomOwner& inTask, const uint32 inArgumentIndex)
{
    return inTask.GetListElement(inArgumentIndex + 1u);
}
