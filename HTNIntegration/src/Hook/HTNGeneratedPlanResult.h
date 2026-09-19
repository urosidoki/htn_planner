// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "Core/HTNAtom.h"
#include <utility>

// Result owned by generated execution.
class HTNGeneratedPlanResult
{
public:
    void SetResult(HTNAtomOwner inResult) { mResult = std::move(inResult); }
    const HTNAtomOwner& GetResult() const { return mResult; }

private:
    HTNAtomOwner mResult = HTNAtomOwner(HTNAtomListOwner{});
};
