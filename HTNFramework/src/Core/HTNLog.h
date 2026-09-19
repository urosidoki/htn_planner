// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#ifdef HTN_ENABLE_LOGGING
#include <format>
#include <iostream>
#endif

#ifdef HTN_ENABLE_LOGGING
#define HTN_LOG(Message, ...) std::cout << std::format("{}({}): {}", __func__, __LINE__, std::format(Message, __VA_ARGS__)) << std::endl;
#define HTN_CLOG(Condition, Message, ...)                                                                                                            \
    if (Condition)                                                                                                                                   \
    HTN_LOG(Message, __VA_ARGS__)
#define HTN_LOG_ERROR(Message, ...) std::cout << std::format("{}({}): error: {}", __func__, __LINE__, std::format(Message, __VA_ARGS__)) << std::endl;
#define HTN_CLOG_ERROR(Condition, Message, ...)                                                                                                      \
    if (Condition)                                                                                                                                   \
    HTN_LOG_ERROR(Message, __VA_ARGS__)
#define HTN_DOMAIN_LOG(Row, Column, Message, ...)                                                                                                    \
    std::cout << std::format("({},{}): {}", Row, Column, std::format(Message, __VA_ARGS__)) << std::endl;
#define HTN_DOMAIN_CLOG(Condition, Row, Column, Message, ...)                                                                                        \
    if (Condition)                                                                                                                                   \
    HTN_DOMAIN_LOG(Row, Column, Message, __VA_ARGS__)
#define HTN_DOMAIN_LOG_ERROR(Row, Column, Message, ...)                                                                                              \
    std::cout << std::format("({},{}): error: {}", Row, Column, std::format(Message, __VA_ARGS__)) << std::endl;
#define HTN_DOMAIN_CLOG_ERROR(Condition, Row, Column, Message, ...)                                                                                  \
    if (Condition)                                                                                                                                   \
    HTN_DOMAIN_LOG_ERROR(Row, Column, Message, __VA_ARGS__)
#else
#define HTN_LOG(Message, ...)
#define HTN_CLOG(Condition, Message, ...)
#define HTN_LOG_ERROR(Message, ...)
#define HTN_CLOG_ERROR(Condition, Message, ...)
#define HTN_DOMAIN_LOG(Row, Column, Message, ...)
#define HTN_DOMAIN_CLOG(Condition, Row, Column, Message, ...)
#define HTN_DOMAIN_LOG_ERROR(Row, Column, Message, ...)
#define HTN_DOMAIN_CLOG_ERROR(Condition, Row, Column, Message, ...)
#endif
