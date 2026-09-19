// Copyright (c) 2023 Sandra Alvarez sandruskiag@gmail.com Jose Antonio Escribano joseantonioescribanoayllon@gmail.com

#pragma once

#define HTN_MAYBE_UNUSED [[maybe_unused]]
#define HTN_NODISCARD [[nodiscard]]

#ifdef HTN_ENABLE_LOGGING
#define HTN_LOG_ONLY(...) __VA_ARGS__
#else
#define HTN_LOG_ONLY(...)
#endif
