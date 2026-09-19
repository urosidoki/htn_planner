// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com
#pragma once
#include "Core/HTNAtomC.h"
#include "HTNCoreMinimal.h"

#include <string>

std::string HTNAtomListToString(const HTNAtomList& inList, bool inShouldDoubleQuoteString);

inline bool operator==(const HTNAtomList& inLeft, const HTNAtomList& inRight)
{
    return HTNAtomList_Equals(&inLeft, &inRight) != 0;
}

inline bool operator!=(const HTNAtomList& inLeft, const HTNAtomList& inRight)
{
    return !(inLeft == inRight);
}
