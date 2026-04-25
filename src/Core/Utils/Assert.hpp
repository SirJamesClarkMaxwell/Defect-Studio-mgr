#pragma once

#include "Core/Platform/PlatformSystem.hpp"
#include "Core/Utils/Logger.hpp"

#define DS_DEBUGBREAK() ::DefectStudio::Platform::DebugBreak()

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
