// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Domain/Source/HTNDomainFileSyntax.h"

#include <algorithm>
#include <cctype>

namespace
{
void SkipTrivia(const std::string& inText, size_t& ioPosition)
{
    while (ioPosition < inText.size())
    {
        if (std::isspace(static_cast<unsigned char>(inText[ioPosition])))
            ++ioPosition;
        else if (inText.compare(ioPosition, 2u, "//") == 0)
        {
            const size_t End = inText.find('\n', ioPosition);
            ioPosition = End == std::string::npos ? inText.size() : End + 1u;
        }
        else
            break;
    }
}

bool ReadIncludePrefix(const std::string& inText, size_t inPosition, size_t& outPosition)
{
    if (inPosition >= inText.size() || inText[inPosition++] != '(') return false;
    SkipTrivia(inText, inPosition);
    if (inPosition >= inText.size() || inText[inPosition++] != ':') return false;
    SkipTrivia(inText, inPosition);
    if (inText.compare(inPosition, 7u, "include") != 0) return false;
    inPosition += 7u;
    if (inPosition < inText.size() &&
        (std::isalnum(static_cast<unsigned char>(inText[inPosition])) || inText[inPosition] == '_'))
        return false;
    outPosition = inPosition;
    return true;
}
}

bool HTNSplitDomainFile(const std::string& inText, std::vector<HTNDomainInclude>& outIncludes,
                        std::string& outDomainText, HTNParserError& outError)
{
    outIncludes.clear();
    outDomainText = inText;
    outError = {};
    const HTNSourceText Source(inText);
    const auto Fail = [&](HTNParserErrorCode inCode, const char* inMessage, size_t inBegin, size_t inEnd) {
        outError = {inCode, inMessage, {Source.GetPosition(inBegin), Source.GetPosition(inEnd)}};
        return false;
    };
    size_t Cursor = 0u;
    while (true)
    {
        SkipTrivia(inText, Cursor);
        size_t Position = Cursor;
        if (!ReadIncludePrefix(inText, Cursor, Position)) break;
        SkipTrivia(inText, Position);
        if (Position >= inText.size() || inText[Position] != '"')
            return Fail(HTNParserErrorCode::ExpectedIncludePath, "Expected quoted path after :include",
                        Position, std::min(Position + 1u, inText.size()));
        const size_t Quote = Position++;
        const size_t End = inText.find('"', Position);
        if (End == std::string::npos)
            return Fail(HTNParserErrorCode::UnterminatedIncludePath, "Unterminated :include path", Quote, inText.size());
        const std::string Path = inText.substr(Position, End - Position);
        if (Path.empty())
            return Fail(HTNParserErrorCode::ExpectedIncludePath, "Expected non-empty :include path", Quote, End + 1u);
        Position = End + 1u;
        SkipTrivia(inText, Position);
        if (Position >= inText.size() || inText[Position] != ')')
            return Fail(HTNParserErrorCode::ExpectedIncludeEnd, "Expected ')' after :include path",
                        Position, std::min(Position + 1u, inText.size()));
        ++Position;
        outIncludes.push_back({Path, {Source.GetPosition(Cursor), Source.GetPosition(Position)}});
        for (size_t I = Cursor; I < Position; ++I)
            if (outDomainText[I] != '\n' && outDomainText[I] != '\r') outDomainText[I] = ' ';
        Cursor = Position;
    }
    if (Cursor == inText.size())
        return Fail(HTNParserErrorCode::ExpectedDomain, "Missing :domain form", Cursor, Cursor);

    // Ignore quoted text and comments when checking for misplaced directives.
    while (Cursor < inText.size())
    {
        SkipTrivia(inText, Cursor);
        if (Cursor == inText.size()) break;
        if (inText[Cursor] == '"')
        {
            const size_t End = inText.find('"', Cursor + 1u);
            if (End == std::string::npos) break; // The lexer diagnoses an unclosed string.
            Cursor = End + 1u;
            continue;
        }
        size_t End = Cursor;
        if (ReadIncludePrefix(inText, Cursor, End))
            return Fail(HTNParserErrorCode::MisplacedInclude,
                        ":include directives are only allowed before :domain", Cursor, End);
        ++Cursor;
    }
    return true;
}
