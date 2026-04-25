#pragma once

#include <ctime>

namespace DefectStudio::Platform
{
	using NativeCrashCallback = void (*)(const char *message);

	void DebugBreak();
	void InstallNativeCrashHandler(NativeCrashCallback callback);
	[[nodiscard]] bool LocalTime(std::time_t time, std::tm &outLocalTime);
} // namespace DefectStudio::Platform
