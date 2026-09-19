// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Domain/Source/HTNSourceText.h"

#include <algorithm>

HTNSourceText::HTNSourceText(std::string inText)
    : mText(std::move(inText))
{
    RebuildLineStarts();
}

void HTNSourceText::SetText(std::string inText)
{
    mText = std::move(inText);
    RebuildLineStarts();
}

void HTNSourceText::RebuildLineStarts()
{
    mLineStarts.clear();
    mLineStarts.push_back(0);

    for (size_t Index = 0; Index < mText.size(); ++Index)
    {
        if (mText[Index] == '\n')
            mLineStarts.push_back(Index + 1);
    }
}

HTNSourcePosition HTNSourceText::GetPosition(size_t inOffset) const
{
    inOffset = std::min(inOffset, mText.size());

    const auto It = std::upper_bound(
        mLineStarts.begin(),
        mLineStarts.end(),
        inOffset);

    const size_t LineIndex =
        It == mLineStarts.begin()
            ? 0
            : static_cast<size_t>(std::distance(mLineStarts.begin(), It) - 1);

    const size_t LineStart = mLineStarts[LineIndex];

    return {
        inOffset,
        static_cast<int>(LineIndex + 1),
        static_cast<int>(inOffset - LineStart + 1)
    };
}

size_t HTNSourceText::GetOffset(int inLine, int inColumn) const
{
    if (mLineStarts.empty())
        return 0;

    const size_t LineIndex = static_cast<size_t>(
        std::clamp(inLine, 1, static_cast<int>(mLineStarts.size())) - 1);

    const size_t LineStart = mLineStarts[LineIndex];
    const size_t LineEnd =
        LineIndex + 1 < mLineStarts.size()
            ? mLineStarts[LineIndex + 1] - 1
            : mText.size();

    const size_t ColumnOffset =
        static_cast<size_t>(std::max(inColumn, 1) - 1);

    return std::min(LineStart + ColumnOffset, LineEnd);
}

