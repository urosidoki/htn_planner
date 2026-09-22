// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtomOwner.h"

#include "Core/HTNAtom.h"
#include "Core/HTNPlannerExecutionContext.h"
#include "Translator/HTNCallTermBridge.h"
#include "HTNCoreMinimal.h"

#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

class HTNCallTermBindingContext;

class HTNCallTermArguments
{
public:
    HTNCallTermArguments(const std::vector<HTNAtomOwner>& inAtoms)
        : mOwners(inAtoms.data()), mSize(inAtoms.size())
    {
    }

    HTNCallTermArguments(const HTNAtom* const* inAtoms, const size_t inSize)
        : mAtoms(inAtoms), mSize(inSize)
    {
    }

    const HTNAtom& operator[](const size_t inIndex) const
    {
        return mAtoms ? *mAtoms[inIndex] : *mOwners[inIndex].Get();
    }

    // Borrowed execution context, populated by the registry for each invocation.
    void* GetClientContext() const { return mClientContext; }

    size_t size() const { return mSize; }
    bool empty() const { return mSize == 0u; }

private:
    friend class HTNCallTermRegistry;
    void* mClientContext = nullptr;
    const HTNAtomOwner* mOwners = nullptr;
    const HTNAtom* const* mAtoms = nullptr;
    size_t mSize = 0u;
};

using HTNCallTermFunction = std::function<HTNAtomOwner(void*, const HTNCallTermArguments&)>;
using HTNCallTermSignature = std::vector<std::optional<HTNAtomType>>;

/**
 * Callterm registry configured before planning begins.
 *
 * Bind() mutates the registry and is not synchronized with concurrent reads.
 * After configuration, IsBound()/Resolve()/Execute() may be called concurrently as long as
 * the registered callterm functions themselves are safe for that usage.
 */
class HTNCallTermRegistry
{
public:
    // Legacy/untyped binding. Kept for callers that intentionally consume raw HTNAtom arguments.
    // Configuration-time only. Do not call while planners are executing.
    template<typename TFunction>
    void Bind(const std::string& inID, TFunction&& inFunction)
    {
        using FunctionType = std::decay_t<TFunction>;
        using ReturnType = std::invoke_result_t<FunctionType, const HTNCallTermArguments&>;
        static_assert(!std::is_void_v<ReturnType>, "HTN callterms must return HTNAtom or a type supported by HTNAtom");

        FunctionType Function = std::forward<TFunction>(inFunction);

        Entry NewEntry;
        NewEntry.Function = [Function = std::move(Function)](void*, const HTNCallTermArguments& inArguments) mutable -> HTNAtomOwner
        {
            if constexpr (std::is_same_v<ReturnType, HTNAtom>)
                return Function(inArguments);
            else
                return HTNAtomOwner(Function(inArguments));
        };

        mEntries[inID] = std::move(NewEntry);
    }

    // Typed binding. The expected HTN representation is stored as metadata and the
    // resolved callable performs validation too, which is important because the
    // generated execution storage caches the function returned by Resolve().
    template<typename TFunction>
    void Bind(const std::string& inID, TFunction&& inFunction, HTNCallTermSignature inSignature)
    {
        using FunctionType = std::decay_t<TFunction>;
        using ReturnType = std::invoke_result_t<FunctionType, const HTNCallTermArguments&>;
        static_assert(!std::is_void_v<ReturnType>, "HTN callterms must return HTNAtom or a type supported by HTNAtom");

        FunctionType Function = std::forward<TFunction>(inFunction);
        HTNCallTermSignature ValidationSignature = inSignature;
        std::string CallTermID = inID;

        Entry NewEntry;
        NewEntry.Signature = std::move(inSignature);
        NewEntry.Function = [Function = std::move(Function),
                             Signature = std::move(ValidationSignature),
                             CallTermID = std::move(CallTermID)](void*, const HTNCallTermArguments& inArguments) mutable -> HTNAtomOwner
        {
            if (!ValidateArguments(CallTermID, Signature, inArguments))
                return HTNAtomOwner();

            if constexpr (std::is_same_v<ReturnType, HTNAtom>)
                return Function(inArguments);
            else
                return HTNAtomOwner(Function(inArguments));
        };

        mEntries[inID] = std::move(NewEntry);
    }

    bool BindMember(const std::string& inID,
                    const std::string& inDaemonID,
                    HTNCallTermFunction inFunction,
                    HTNCallTermSignature inSignature);

    HTN_NODISCARD bool IsBound(const std::string& inID) const;
    HTN_NODISCARD const HTNCallTermFunction* Resolve(const std::string& inID) const;
    HTN_NODISCARD const HTNCallTermSignature* ResolveSignature(const std::string& inID) const;
    HTN_NODISCARD HTNAtomOwner Execute(const std::string& inID,
                                       const HTNPlannerExecutionContext& inContext,
                                       const HTNCallTermArguments& inArguments,
                                       const HTNCallTermSource* inSource = nullptr) const;

private:
    struct Entry
    {
        HTNCallTermFunction Function;
        std::optional<HTNCallTermSignature> Signature;
        std::size_t DaemonSlot = std::numeric_limits<std::size_t>::max();
        std::string DaemonID;
    };

    static HTNAtomOwner InvokeEntry(const Entry* inEntry, const char* inName,
                                    const HTNCallTermBindingContext* inContext,
                                    const HTNCallTermArguments& inArguments,
                                    const HTNCallTermSource* inSource, void* inClientContext,
                                    HTNMissingCallTermPolicy inPolicy, HTNMissingCallTermCallback inCallback);

    friend int HTNCallTermRegistry_InvokeGeneratedCallTermWithSource(
        const HTNGeneratedPlannerContext*, const HTNGeneratedCallTerm*, const HTNAtom* const*,
        uint32_t, HTNAtom*, const HTNCallTermSource*);

    HTN_NODISCARD std::size_t FindDaemonSlot(const std::string& inID) const;

    static bool ValidateArguments(const std::string& inID,
                                  const HTNCallTermSignature& inSignature,
                                  const HTNCallTermArguments& inArguments);

    friend HTNGeneratedCallTerm HTNCallTermRegistry_ResolveGeneratedCallTerm(
        const HTNCallTermBindingContext* callterm_context,
        const char* name);
    friend int HTNCallTermRegistry_InvokeGeneratedCallTerm(
        const HTNGeneratedPlannerContext* context,
        const HTNGeneratedCallTerm* callterm,
        const HTNAtom* const* arguments,
        uint32_t argument_count,
        HTNAtom* out_result);

    std::unordered_map<std::string, Entry> mEntries;
    std::unordered_map<std::string, std::size_t> mDaemonSlots;

    friend class HTNCallTermBindingContext;
};
