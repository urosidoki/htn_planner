// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNLspTransport.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <string>

HTNLspTransport::HTNLspTransport(
    std::istream& inInput,
    std::ostream& inOutput)
    : mInput(inInput)
    , mOutput(inOutput)
{
}

bool HTNLspTransport::ReadMessage(std::string& outJsonPayload)
{
    outJsonPayload.clear();

    std::string Line;
    size_t ContentLength = 0;
    bool HasContentLength = false;

    while (std::getline(mInput, Line))
    {
        if (!Line.empty() && Line.back() == '\r')
            Line.pop_back();

        if (Line.empty())
            break;

        constexpr std::string_view Prefix = "Content-Length:";
        if (Line.size() >= Prefix.size() &&
            std::equal(Prefix.begin(), Prefix.end(), Line.begin(),
                [](char A, char B)
                {
                    return std::tolower(static_cast<unsigned char>(A)) ==
                           std::tolower(static_cast<unsigned char>(B));
                }))
        {
            const std::string_view Value(Line.data() + Prefix.size(), Line.size() - Prefix.size());
            const size_t Begin = Value.find_first_not_of(" \t");
            if (Begin == std::string_view::npos)
                return false;
            const std::string_view Digits = Value.substr(Begin);
            unsigned long long ParsedLength = 0;
            const auto Parsed = std::from_chars(Digits.data(), Digits.data() + Digits.size(), ParsedLength);
            if (Parsed.ec != std::errc{} || Parsed.ptr != Digits.data() + Digits.size() ||
                ParsedLength > std::numeric_limits<size_t>::max())
                return false;
            ContentLength = static_cast<size_t>(ParsedLength);
            HasContentLength = true;
        }
    }

    if (!HasContentLength)
        return false;

    outJsonPayload.resize(ContentLength);
    mInput.read(outJsonPayload.data(), static_cast<std::streamsize>(ContentLength));
    return mInput.good() || static_cast<size_t>(mInput.gcount()) == ContentLength;
}

void HTNLspTransport::WriteMessage(const std::string& inJsonPayload)
{
    mOutput << "Content-Length: " << inJsonPayload.size() << "\r\n\r\n";
    mOutput.write(inJsonPayload.data(), static_cast<std::streamsize>(inJsonPayload.size()));
    mOutput.flush();
}
