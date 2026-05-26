#pragma once

#include <ctime>

namespace DefectStudio::Platform
{
	using NativeCrashCallback = void (*)(const char *message);

	void DebugBreak();
	void InstallNativeCrashHandler(NativeCrashCallback callback);
	void AppendNativeCrashStackTrace(NativeCrashCallback callback, unsigned int framesToSkip = 0);
	[[nodiscard]] bool LocalTime(std::time_t time, std::tm &outLocalTime);
} // namespace DefectStudio::Platform
