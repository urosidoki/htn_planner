// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Core/HTNCallTermRegistry.h"
#include "Core/HTNCallTermBindingContext.h"
#include "Translator/HTNCallTermBridge.h"

bool HTNCallTermRegistry::IsBound(const std::string& inID) const
{
    return Resolve(inID) != nullptr;
}

const HTNCallTermFunction* HTNCallTermRegistry::Resolve(const std::string& inID) const
{
    const auto It = mEntries.find(inID);
    return It == mEntries.end() ? nullptr : &It->second.Function;
}

const HTNCallTermSignature* HTNCallTermRegistry::ResolveSignature(const std::string& inID) const
{
    const auto It = mEntries.find(inID);
    if (It == mEntries.end() || !It->second.Signature)
        return nullptr;

    return &*It->second.Signature;
}

HTNAtomOwner HTNCallTermRegistry::Execute(const std::string& inID,
                                          const HTNCallTermBindingContext& inContext,
                                          const HTNCallTermArguments& inArguments) const
{
    const auto It = mEntries.find(inID);
    if (It == mEntries.end())
    {
        HTN_LOG_ERROR("Callterm [{}] is not bound", inID);
        return HTNAtomOwner();
    }

    const Entry& CallTerm = It->second;
    void* Daemon = nullptr;
    if (CallTerm.DaemonSlot != std::numeric_limits<std::size_t>::max())
    {
        Daemon = inContext.GetDaemon(CallTerm.DaemonSlot);
        if (!Daemon)
        {
            HTN_LOG_ERROR("Callterm [{}] requires daemon [{}], but no instance is configured",
                          inID,
                          CallTerm.DaemonID);
            return HTNAtomOwner();
        }
    }

    return CallTerm.Function(Daemon, inArguments);
}

bool HTNCallTermRegistry::BindMember(const std::string& inID,
                                     const std::string& inDaemonID,
                                     HTNCallTermFunction inFunction,
                                     HTNCallTermSignature inSignature)
{
    auto DaemonIt = mDaemonSlots.find(inDaemonID);
    if (DaemonIt == mDaemonSlots.end())
    {
        if (mDaemonSlots.size() >= HTN_MAX_CALLTERM_DAEMON_TYPES)
        {
            HTN_LOG_ERROR("Cannot register callterm daemon type [{}]: capacity [{}] has been reached",
                          inDaemonID,
                          HTN_MAX_CALLTERM_DAEMON_TYPES);
            return false;
        }

        DaemonIt = mDaemonSlots.emplace(inDaemonID, mDaemonSlots.size()).first;
    }

    Entry NewEntry;
    NewEntry.Function = std::move(inFunction);
    NewEntry.Signature = std::move(inSignature);
    NewEntry.DaemonSlot = DaemonIt->second;
    NewEntry.DaemonID = inDaemonID;
    mEntries[inID] = std::move(NewEntry);
    return true;
}

std::size_t HTNCallTermRegistry::FindDaemonSlot(const std::string& inID) const
{
    const auto It = mDaemonSlots.find(inID);
    return It == mDaemonSlots.end() ? std::numeric_limits<std::size_t>::max() : It->second;
}

bool HTNCallTermRegistry::ValidateArguments([[maybe_unused]]const std::string& inID,
                                            const HTNCallTermSignature& inSignature,
                                            const HTNCallTermArguments& inArguments)
{
    if (inArguments.size() != inSignature.size())
    {
        HTN_LOG_ERROR("Callterm [{}] expects [{}] argument(s), received [{}]",
                      inID,
                      inSignature.size(),
                      inArguments.size());
        return false;
    }

    for (size_t Index = 0u; Index < inSignature.size(); ++Index)
    {
        const std::optional<HTNAtomType>& ExpectedType = inSignature[Index];
        if (ExpectedType && HTNAtomGetType(inArguments[Index]) != *ExpectedType)
        {
            HTN_LOG_ERROR("Callterm [{}] argument [{}] has incompatible HTN atom type",
                          inID,
                          Index);
            return false;
        }
    }

    return true;
}

extern "C" HTNGeneratedCallTerm HTNCallTermRegistry_ResolveGeneratedCallTerm(
    const HTNCallTermBindingContext* inContext,
    const char* inName)
{
    HTNGeneratedCallTerm Result = {nullptr, inName};
    if (!inContext || !inName)
        return Result;

    const HTNCallTermRegistry* inRegistry = &inContext->mRegistry;
    const auto It = inRegistry->mEntries.find(inName);
    if (It != inRegistry->mEntries.end())
    {
        Result.registry_entry = &It->second;
        Result.name = It->first.c_str();
    }
    return Result;
}

extern "C" int HTNCallTermRegistry_InvokeGeneratedCallTerm(
    const HTNCallTermBindingContext* inContext,
    const HTNGeneratedCallTerm* inCallTerm,
    const HTNAtom* const* inArguments,
    const std::uint32_t inArgumentCount,
    HTNAtom* outResult)
{
    const auto* Entry = inCallTerm
        ? static_cast<const HTNCallTermRegistry::Entry*>(inCallTerm->registry_entry)
        : nullptr;
    if (!Entry)
    {
        HTN_LOG_ERROR("Callterm [{}] is not bound", inCallTerm && inCallTerm->name ? inCallTerm->name : "<unknown>");
        return 0;
    }

    void* Daemon = nullptr;
    if (Entry->DaemonSlot != std::numeric_limits<std::size_t>::max())
    {
        Daemon = inContext ? inContext->GetDaemon(Entry->DaemonSlot) : nullptr;
        if (!Daemon)
        {
            HTN_LOG_ERROR("Callterm [{}] requires daemon [{}], but no instance is configured",
                          inCallTerm && inCallTerm->name ? inCallTerm->name : "<unknown>",
                          Entry->DaemonID);
            return 0;
        }
    }

    const HTNCallTermArguments Arguments(inArguments, inArgumentCount);
    HTNAtomOwner Result = Entry->Function(Daemon, Arguments);
    if (!Result.IsBound())
        return 0;

    HTNAtom_AssignMove(outResult, Result.Get());
    return 1;
}
