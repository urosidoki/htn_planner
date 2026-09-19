// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#include "Parser/HTNTokenHelpers.h"

#include "Parser/HTNTokenType.h"

namespace HTNTokenHelpers
{
std::string GetTokenTypeString(const HTNTokenType inTokenType)
{
    static const std::unordered_map<HTNTokenType, std::string> TokenTypesToStrings = {{HTNTokenType::COLON, "colon"},
                                                                                      {HTNTokenType::LEFT_PARENTHESIS, "left parenthesis"},
                                                                                      {HTNTokenType::RIGHT_PARENTHESIS, "right parenthesis"},
                                                                                      {HTNTokenType::EXCLAMATION_MARK, "exclamation mark"},
                                                                                      {HTNTokenType::QUESTION_MARK, "question mark"},
                                                                                      {HTNTokenType::HASH, "hash"},
                                                                                      {HTNTokenType::AT, "at"},
                                                                                      {HTNTokenType::EQUAL_EQUAL, "=="},
                                                                                      {HTNTokenType::NOT_EQUAL, "!="},
                                                                                      {HTNTokenType::LESS, "<"},
                                                                                      {HTNTokenType::LESS_EQUAL, "<="},
                                                                                      {HTNTokenType::GREATER, ">"},
                                                                                      {HTNTokenType::GREATER_EQUAL, ">="},
                                                                                      {HTNTokenType::HTN_DOMAIN, "domain"},
                                                                                      {HTNTokenType::HTN_TOP_LEVEL_DOMAIN, "top-level domain"},
                                                                                      {HTNTokenType::HTN_BASE, "base"},
                                                                                      {HTNTokenType::HTN_OVERRIDES, "overrides"},
                                                                                      {HTNTokenType::HTN_METHOD, "method"},
                                                                                      {HTNTokenType::HTN_TOP_LEVEL_METHOD, "top-level method"},
                                                                                      {HTNTokenType::HTN_AXIOM, "axiom"},
                                                                                      {HTNTokenType::HTN_CONSTANTS, "constants"},
                                                                                      {HTNTokenType::AND, "and"},
                                                                                      {HTNTokenType::OR, "or"},
                                                                                      {HTNTokenType::ALT, "alt"},
                                                                                      {HTNTokenType::NOT, "not"},
                                                                                      {HTNTokenType::IDENTIFIER, "identifier"},
                                                                                      {HTNTokenType::NUMBER, "number"},
                                                                                      {HTNTokenType::STRING, "string"},
                                                                                      {HTNTokenType::END_OF_FILE, "end of file"}};
    const auto                                                 It                  = TokenTypesToStrings.find(inTokenType);
    return (It != TokenTypesToStrings.cend()) ? It->second : "";
}
} // namespace HTNTokenHelpers
