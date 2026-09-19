// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "HTNLspJson.h"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace
{
class JsonParser final
{
public:
    explicit JsonParser(std::string_view inText)
        : mText(inText)
    {
    }

    std::optional<HTNLspJson> Parse(std::string& outError)
    {
        SkipWhitespace();
        auto Result = ParseValue(outError);
        if (!Result)
            return std::nullopt;

        SkipWhitespace();
        if (mOffset != mText.size())
        {
            outError = "Unexpected trailing JSON content";
            return std::nullopt;
        }

        return Result;
    }

private:
    std::optional<HTNLspJson> ParseValue(std::string& outError)
    {
        SkipWhitespace();
        if (mOffset >= mText.size())
        {
            outError = "Unexpected end of JSON input";
            return std::nullopt;
        }

        const char C = mText[mOffset];
        if (C == '{') return ParseObject(outError);
        if (C == '[') return ParseArray(outError);
        if (C == '"')
        {
            std::string S;
            if (!ParseString(S, outError)) return std::nullopt;
            return HTNLspJson(std::move(S));
        }
        if (C == 't' && ConsumeLiteral("true")) return HTNLspJson(true);
        if (C == 'f' && ConsumeLiteral("false")) return HTNLspJson(false);
        if (C == 'n' && ConsumeLiteral("null")) return HTNLspJson(nullptr);
        if (C == '-' || (C >= '0' && C <= '9')) return ParseNumber(outError);

        outError = "Unexpected JSON token";
        return std::nullopt;
    }

    std::optional<HTNLspJson> ParseObject(std::string& outError)
    {
        ++mOffset;
        HTNLspJson::Object Result;

        SkipWhitespace();
        if (Consume('}'))
            return HTNLspJson(std::move(Result));

        while (mOffset < mText.size())
        {
            std::string Key;
            if (!ParseString(Key, outError))
                return std::nullopt;

            SkipWhitespace();
            if (!Consume(':'))
            {
                outError = "Expected ':' in JSON object";
                return std::nullopt;
            }

            auto Value = ParseValue(outError);
            if (!Value)
                return std::nullopt;
            Result.emplace(std::move(Key), std::move(*Value));

            SkipWhitespace();
            if (Consume('}'))
                return HTNLspJson(std::move(Result));
            if (!Consume(','))
            {
                outError = "Expected ',' or '}' in JSON object";
                return std::nullopt;
            }
            SkipWhitespace();
        }

        outError = "Unterminated JSON object";
        return std::nullopt;
    }

    std::optional<HTNLspJson> ParseArray(std::string& outError)
    {
        ++mOffset;
        HTNLspJson::Array Result;

        SkipWhitespace();
        if (Consume(']'))
            return HTNLspJson(std::move(Result));

        while (mOffset < mText.size())
        {
            auto Value = ParseValue(outError);
            if (!Value)
                return std::nullopt;
            Result.emplace_back(std::move(*Value));

            SkipWhitespace();
            if (Consume(']'))
                return HTNLspJson(std::move(Result));
            if (!Consume(','))
            {
                outError = "Expected ',' or ']' in JSON array";
                return std::nullopt;
            }
        }

        outError = "Unterminated JSON array";
        return std::nullopt;
    }

    std::optional<HTNLspJson> ParseNumber(std::string& outError)
    {
        const size_t Begin = mOffset;
        if (mText[mOffset] == '-') ++mOffset;
        while (mOffset < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mOffset]))) ++mOffset;

        bool IsFloatingPoint = false;
        if (mOffset < mText.size() && mText[mOffset] == '.')
        {
            IsFloatingPoint = true;
            ++mOffset;
            while (mOffset < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mOffset]))) ++mOffset;
        }

        if (mOffset < mText.size() && (mText[mOffset] == 'e' || mText[mOffset] == 'E'))
        {
            IsFloatingPoint = true;
            ++mOffset;
            if (mOffset < mText.size() && (mText[mOffset] == '+' || mText[mOffset] == '-')) ++mOffset;
            while (mOffset < mText.size() && std::isdigit(static_cast<unsigned char>(mText[mOffset]))) ++mOffset;
        }

        const std::string Number(mText.substr(Begin, mOffset - Begin));
        if (IsFloatingPoint)
        {
            double Value = 0.0;
            const auto Parsed = std::from_chars(Number.data(), Number.data() + Number.size(),
                                                Value, std::chars_format::general);
            if (Parsed.ec == std::errc{} && Parsed.ptr == Number.data() + Number.size() &&
                std::isfinite(Value))
                return HTNLspJson(Value);
        }
        else
        {
            std::int64_t Value = 0;
            const auto Parsed = std::from_chars(Number.data(), Number.data() + Number.size(), Value);
            if (Parsed.ec == std::errc{} && Parsed.ptr == Number.data() + Number.size())
                return HTNLspJson(Value);
        }
        outError = "Invalid JSON number";
        return std::nullopt;
    }

    bool ParseString(std::string& outString, std::string& outError)
    {
        SkipWhitespace();
        if (!Consume('"'))
        {
            outError = "Expected JSON string";
            return false;
        }

        outString.clear();
        while (mOffset < mText.size())
        {
            const char C = mText[mOffset++];
            if (C == '"')
                return true;

            if (C != '\\')
            {
                outString.push_back(C);
                continue;
            }

            if (mOffset >= mText.size())
            {
                outError = "Invalid JSON escape";
                return false;
            }

            const char Escape = mText[mOffset++];
            switch (Escape)
            {
            case '"': outString.push_back('"'); break;
            case '\\': outString.push_back('\\'); break;
            case '/': outString.push_back('/'); break;
            case 'b': outString.push_back('\b'); break;
            case 'f': outString.push_back('\f'); break;
            case 'n': outString.push_back('\n'); break;
            case 'r': outString.push_back('\r'); break;
            case 't': outString.push_back('\t'); break;
            case 'u':
            {
                if (mOffset + 4 > mText.size())
                {
                    outError = "Invalid JSON unicode escape";
                    return false;
                }

                unsigned Code = 0;
                for (int I = 0; I < 4; ++I)
                {
                    const char H = mText[mOffset++];
                    Code <<= 4;
                    if (H >= '0' && H <= '9') Code |= static_cast<unsigned>(H - '0');
                    else if (H >= 'a' && H <= 'f') Code |= static_cast<unsigned>(H - 'a' + 10);
                    else if (H >= 'A' && H <= 'F') Code |= static_cast<unsigned>(H - 'A' + 10);
                    else
                    {
                        outError = "Invalid JSON unicode escape";
                        return false;
                    }
                }

                if (Code <= 0x7F)
                    outString.push_back(static_cast<char>(Code));
                else if (Code <= 0x7FF)
                {
                    outString.push_back(static_cast<char>(0xC0 | (Code >> 6)));
                    outString.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
                }
                else
                {
                    outString.push_back(static_cast<char>(0xE0 | (Code >> 12)));
                    outString.push_back(static_cast<char>(0x80 | ((Code >> 6) & 0x3F)));
                    outString.push_back(static_cast<char>(0x80 | (Code & 0x3F)));
                }
                break;
            }
            default:
                outError = "Unsupported JSON escape";
                return false;
            }
        }

        outError = "Unterminated JSON string";
        return false;
    }

    void SkipWhitespace()
    {
        while (mOffset < mText.size() &&
               std::isspace(static_cast<unsigned char>(mText[mOffset])))
            ++mOffset;
    }

    bool Consume(const char inCharacter)
    {
        if (mOffset < mText.size() && mText[mOffset] == inCharacter)
        {
            ++mOffset;
            return true;
        }
        return false;
    }

    bool ConsumeLiteral(std::string_view inLiteral)
    {
        if (mText.substr(mOffset, inLiteral.size()) != inLiteral)
            return false;
        mOffset += inLiteral.size();
        return true;
    }

private:
    std::string_view mText;
    size_t mOffset = 0;
};

std::string EscapeJsonString(std::string_view inText)
{
    std::ostringstream Stream;
    Stream << '"';
    for (const unsigned char C : inText)
    {
        switch (C)
        {
        case '"': Stream << "\\\""; break;
        case '\\': Stream << "\\\\"; break;
        case '\b': Stream << "\\b"; break;
        case '\f': Stream << "\\f"; break;
        case '\n': Stream << "\\n"; break;
        case '\r': Stream << "\\r"; break;
        case '\t': Stream << "\\t"; break;
        default:
            if (C < 0x20)
            {
                Stream << "\\u"
                       << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(C)
                       << std::dec;
            }
            else
            {
                Stream << static_cast<char>(C);
            }
            break;
        }
    }
    Stream << '"';
    return Stream.str();
}
}

HTNLspJson::HTNLspJson(std::nullptr_t) : mValue(nullptr) {}
HTNLspJson::HTNLspJson(bool inValue) : mValue(inValue) {}
HTNLspJson::HTNLspJson(std::int64_t inValue) : mValue(inValue) {}
HTNLspJson::HTNLspJson(double inValue) : mValue(inValue) {}
HTNLspJson::HTNLspJson(std::string inValue) : mValue(std::move(inValue)) {}
HTNLspJson::HTNLspJson(const char* inValue) : mValue(std::string(inValue ? inValue : "")) {}
HTNLspJson::HTNLspJson(Array inValue) : mValue(std::move(inValue)) {}
HTNLspJson::HTNLspJson(Object inValue) : mValue(std::move(inValue)) {}

std::optional<HTNLspJson> HTNLspJson::Parse(std::string_view inText, std::string& outError)
{
    outError.clear();
    return JsonParser(inText).Parse(outError);
}

std::string HTNLspJson::Serialize() const
{
    if (IsNull()) return "null";
    if (IsBool()) return AsBool() ? "true" : "false";
    if (IsInteger()) return std::to_string(AsInteger());
    if (std::holds_alternative<double>(mValue))
    {
        std::ostringstream S;
        S << std::setprecision(17) << AsNumber();
        return S.str();
    }
    if (IsString()) return EscapeJsonString(AsString());

    if (IsArray())
    {
        std::string Out = "[";
        bool First = true;
        for (const HTNLspJson& Value : AsArray())
        {
            if (!First) Out += ',';
            First = false;
            Out += Value.Serialize();
        }
        Out += ']';
        return Out;
    }

    std::string Out = "{";
    bool First = true;
    for (const auto& [Key, Value] : AsObject())
    {
        if (!First) Out += ',';
        First = false;
        Out += EscapeJsonString(Key);
        Out += ':';
        Out += Value.Serialize();
    }
    Out += '}';
    return Out;
}

bool HTNLspJson::IsNull() const { return std::holds_alternative<std::nullptr_t>(mValue); }
bool HTNLspJson::IsBool() const { return std::holds_alternative<bool>(mValue); }
bool HTNLspJson::IsInteger() const { return std::holds_alternative<std::int64_t>(mValue); }
bool HTNLspJson::IsNumber() const { return IsInteger() || std::holds_alternative<double>(mValue); }
bool HTNLspJson::IsString() const { return std::holds_alternative<std::string>(mValue); }
bool HTNLspJson::IsArray() const { return std::holds_alternative<Array>(mValue); }
bool HTNLspJson::IsObject() const { return std::holds_alternative<Object>(mValue); }

bool HTNLspJson::AsBool(bool inDefault) const
{
    return IsBool() ? std::get<bool>(mValue) : inDefault;
}

std::int64_t HTNLspJson::AsInteger(std::int64_t inDefault) const
{
    return IsInteger() ? std::get<std::int64_t>(mValue) : inDefault;
}

double HTNLspJson::AsNumber(double inDefault) const
{
    if (IsInteger()) return static_cast<double>(std::get<std::int64_t>(mValue));
    return std::holds_alternative<double>(mValue) ? std::get<double>(mValue) : inDefault;
}

const std::string& HTNLspJson::AsString() const
{
    static const std::string Empty;
    return IsString() ? std::get<std::string>(mValue) : Empty;
}

const HTNLspJson::Array& HTNLspJson::AsArray() const
{
    static const Array Empty;
    return IsArray() ? std::get<Array>(mValue) : Empty;
}

const HTNLspJson::Object& HTNLspJson::AsObject() const
{
    static const Object Empty;
    return IsObject() ? std::get<Object>(mValue) : Empty;
}

const HTNLspJson* HTNLspJson::Find(std::string_view inKey) const
{
    if (!IsObject()) return nullptr;
    const auto& ObjectValue = std::get<Object>(mValue);
    const auto It = ObjectValue.find(std::string(inKey));
    return It == ObjectValue.end() ? nullptr : &It->second;
}

HTNLspJson* HTNLspJson::Find(std::string_view inKey)
{
    if (!IsObject()) return nullptr;
    auto& ObjectValue = std::get<Object>(mValue);
    const auto It = ObjectValue.find(std::string(inKey));
    return It == ObjectValue.end() ? nullptr : &It->second;
}
