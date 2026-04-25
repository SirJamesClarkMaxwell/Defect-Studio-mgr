#include "Core/dspch.hpp"

#include <csignal>

#include "Core/Platform/PlatformPaths.hpp"
#include "Core/Platform/PlatformSystem.hpp"
#include "Core/Platform/PlatformWindow.hpp"

#if !defined(DS_PLATFORM_WINDOWS) && !defined(DS_PLATFORM_LINUX) && !defined(DS_PLATFORM_MACOS)
namespace DefectStudio::Platform
{
	void DebugBreak()
	{
		std::raise(SIGTRAP);
	}

	void InstallNativeCrashHandler(NativeCrashCallback callback)
	{
		(void)callback;
	}

	bool LocalTime(std::time_t time, std::tm &outLocalTime)
	{
		const std::tm *local = std::localtime(&time);
		if (local == nullptr)
			return false;

		outLocalTime = *local;
		return true;
	}

	std::vector<FilePath> GetSystemFontDirectories()
	{
		return {};
	}

	void InitializeWindowPlatform(GLFWwindow *window, const Path &iconPath)
	{
		(void)window;
		(void)iconPath;
	}

	void ShutdownWindowPlatform(GLFWwindow *window)
	{
		(void)window;
	}
} // namespace DefectStudio::Platform
#endif
