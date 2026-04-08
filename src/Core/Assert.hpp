#pragma once

#include <csignal>

#include "Core/Logger.hpp"

#if defined(_WIN32)
#define DS_DEBUGBREAK() __debugbreak()
#else
#define DS_DEBUGBREAK() raise(SIGTRAP)
#endif

#if defined(DS_DEBUG)
#define DS_ENABLE_ASSERTS
#endif

#ifdef DS_ENABLE_ASSERTS
#define DS_ASSERT(check, message)                                                                                      \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(check))                                                                                                  \
		{                                                                                                              \
			DS_LOG_ERROR("Assertion failed: '{}' | {}:{}", message, __FILE__, __LINE__);                               \
			DS_DEBUGBREAK();                                                                                           \
		}                                                                                                              \
	} while (false)
#else
#define DS_ASSERT(check, message) ((void)0)
#endif
