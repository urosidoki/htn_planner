// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

struct HTNSourcePosition
{
    size_t Offset = 0;
    int Line = 1;
    int Column = 1;
};

struct HTNSourceRange
{
    HTNSourcePosition Begin;
    HTNSourcePosition End;
};

class HTNSourceText final
{
public:
    HTNSourceText() = default;
    explicit HTNSourceText(std::string inText);

    void SetText(std::string inText);

    const std::string& GetText() const { return mText; }
    std::string_view GetTextView() const { return mText; }
    size_t GetSize() const { return mText.size(); }

    HTNSourcePosition GetPosition(size_t inOffset) const;
    size_t GetOffset(int inLine, int inColumn) const;

private:
    void RebuildLineStarts();

private:
    std::string mText;
    std::vector<size_t> mLineStarts { 0 };
};
