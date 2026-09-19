// Copyright (c) 2026 Jose Antonio Escribano. All rights reserved.

#pragma once

#include "Core/HTNAtom.h"

class HTNCallTermRegistry;

/**
 * Stateless product daemon that exposes the standard HTNAtomList operations as call terms.
 * Bind once while configuring a planner hook, before executing planners concurrently.
 */
class AIHtnListDaemon
{
public:
    static void BindCallTerms(HTNCallTermRegistry& ioRegistry);

    static HTNAtomOwner ListAddFunction(const HTNAtomList& inList, const HTNAtom& inValue);
    static HTNAtomOwner ListRemoveAtFunction(const HTNAtomList& inList, int32 inIndex);
    static HTNAtomOwner ListGetFunction(const HTNAtomList& inList, int32 inIndex);
    static HTNAtomOwner ListSizeFunction(const HTNAtomList& inList);
    static HTNAtomOwner ListClearFunction(const HTNAtomList& inList);
};
