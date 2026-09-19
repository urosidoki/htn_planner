// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#include "HTNCoreMinimal.h"

/**
 * Type of a token
 */
enum class HTNTokenType : uint8
{
    COLON,
    LEFT_PARENTHESIS,
    RIGHT_PARENTHESIS,
    EXCLAMATION_MARK,
    QUESTION_MARK,
    HASH,
    AT,

    EQUAL_EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,

    // Keywords
    HTN_DOMAIN,
    HTN_TOP_LEVEL_DOMAIN,
    HTN_BASE,
    HTN_OVERRIDES,
    HTN_METHOD,
    HTN_TOP_LEVEL_METHOD,
    HTN_AXIOM,
    HTN_CONSTANTS,
    AND,
    OR,
    ALT,
    NOT,
    CALL,
    TRUE,
    FALSE,

    IDENTIFIER,
    NUMBER,
    STRING,

    END_OF_FILE
};
