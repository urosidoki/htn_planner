// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#ifdef HTN_ENABLE_LOGGING
#include <format>
#include <iostream>
#endif

#ifdef HTN_ENABLE_LOGGING
#define HTN_LOG(...) std::cout << std::format("{}({}): {}", __func__, __LINE__, std::format(__VA_ARGS__)) << std::endl;
#define HTN_CLOG(Condition, ...)                                                                                                           \
    if (Condition)                                                                                                                         \
    HTN_LOG(__VA_ARGS__)
#define HTN_LOG_ERROR(...) std::cout << std::format("{}({}): error: {}", __func__, __LINE__, std::format(__VA_ARGS__)) << std::endl;
#define HTN_CLOG_ERROR(Condition, ...)                                                                                                     \
    if (Condition)                                                                                                                         \
    HTN_LOG_ERROR(__VA_ARGS__)
#define HTN_DOMAIN_LOG(Row, Column, ...)                                                                                                   \
    std::cout << std::format("({},{}): {}", Row, Column, std::format(__VA_ARGS__)) << std::endl;
#define HTN_DOMAIN_CLOG(Condition, Row, Column, ...)                                                                                       \
    if (Condition)                                                                                                                         \
    HTN_DOMAIN_LOG(Row, Column, __VA_ARGS__)
#define HTN_DOMAIN_LOG_ERROR(Row, Column, ...)                                                                                             \
    std::cout << std::format("({},{}): error: {}", Row, Column, std::format(__VA_ARGS__)) << std::endl;
#define HTN_DOMAIN_CLOG_ERROR(Condition, Row, Column, ...)                                                                                 \
    if (Condition)                                                                                                                         \
    HTN_DOMAIN_LOG_ERROR(Row, Column, __VA_ARGS__)
#else
#define HTN_LOG(...)
#define HTN_CLOG(Condition, ...)
#define HTN_LOG_ERROR(...)
#define HTN_CLOG_ERROR(Condition, ...)
#define HTN_DOMAIN_LOG(Row, Column, ...)
#define HTN_DOMAIN_CLOG(Condition, Row, Column, ...)
#define HTN_DOMAIN_LOG_ERROR(Row, Column, ...)
#define HTN_DOMAIN_CLOG_ERROR(Condition, Row, Column, ...)
#endif
