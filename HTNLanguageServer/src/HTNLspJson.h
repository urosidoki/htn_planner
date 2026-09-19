// Copyright (c) 2026 Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class HTNLspJson final
{
public:
    using Array = std::vector<HTNLspJson>;
    using Object = std::map<std::string, HTNLspJson>;

    HTNLspJson() = default;
    HTNLspJson(std::nullptr_t);
    HTNLspJson(bool inValue);
    HTNLspJson(std::int64_t inValue);
    HTNLspJson(double inValue);
    HTNLspJson(std::string inValue);
    HTNLspJson(const char* inValue);
    HTNLspJson(Array inValue);
    HTNLspJson(Object inValue);

    static std::optional<HTNLspJson> Parse(
        std::string_view inText,
        std::string& outError);

    std::string Serialize() const;

    bool IsNull() const;
    bool IsBool() const;
    bool IsInteger() const;
    bool IsNumber() const;
    bool IsString() const;
    bool IsArray() const;
    bool IsObject() const;

    bool AsBool(bool inDefault = false) const;
    std::int64_t AsInteger(std::int64_t inDefault = 0) const;
    double AsNumber(double inDefault = 0.0) const;
    const std::string& AsString() const;
    const Array& AsArray() const;
    const Object& AsObject() const;

    const HTNLspJson* Find(std::string_view inKey) const;
    HTNLspJson* Find(std::string_view inKey);

private:
    using Value = std::variant<
        std::nullptr_t,
        bool,
        std::int64_t,
        double,
        std::string,
        Array,
        Object>;

    Value mValue = nullptr;
};
