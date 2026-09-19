// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#include "AI/AIHtnListDaemon.h"

#include "Core/HTNAtomListOwner.h"
#include "Core/HTNCallTermBinding.h"

void AIHtnListDaemon::BindCallTerms(HTNCallTermRegistry& ioRegistry)
{
    HTN_CALLTERM_BIND(ioRegistry, "list_add", AIHtnListDaemon, ListAddFunction);
    HTN_CALLTERM_BIND(ioRegistry, "list_remove_at", AIHtnListDaemon, ListRemoveAtFunction);
    HTN_CALLTERM_BIND(ioRegistry, "list_get", AIHtnListDaemon, ListGetFunction);
    HTN_CALLTERM_BIND(ioRegistry, "list_size", AIHtnListDaemon, ListSizeFunction);
    HTN_CALLTERM_BIND(ioRegistry, "list_clear", AIHtnListDaemon, ListClearFunction);
}

HTNAtomOwner AIHtnListDaemon::ListAddFunction(const HTNAtomList& inList, const HTNAtom& inValue)
{
    HTNAtomListOwner Result(inList);
    if (!HTNAtomList_PushBack(Result.Get(), &inValue))
        return {};
    return HTNAtomOwner(std::move(Result));
}

HTNAtomOwner AIHtnListDaemon::ListRemoveAtFunction(const HTNAtomList& inList, const int32 inIndex)
{
    if (inIndex < 0)
        return {};

    HTNAtomListOwner Result(inList);
    if (!HTNAtomList_RemoveAt(Result.Get(), static_cast<uint32>(inIndex)))
        return {};
    return HTNAtomOwner(std::move(Result));
}

HTNAtomOwner AIHtnListDaemon::ListGetFunction(const HTNAtomList& inList, const int32 inIndex)
{
    if (inIndex < 0)
        return {};

    const HTNAtom* Value = HTNAtomList_Get(&inList, static_cast<uint32>(inIndex));
    return Value ? HTNAtomOwner(*Value) : HTNAtomOwner();
}

HTNAtomOwner AIHtnListDaemon::ListSizeFunction(const HTNAtomList& inList)
{
    return HTNAtomOwner(static_cast<int32>(HTNAtomList_GetSize(&inList)));
}

HTNAtomOwner AIHtnListDaemon::ListClearFunction(const HTNAtomList&)
{
    return HTNAtomOwner(HTNAtomListOwner());
}
