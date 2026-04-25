#include "Core/dspch.hpp"

#if defined(DS_PLATFORM_MACOS)

#include <csignal>

#include "Core/Platform/PlatformPaths.hpp"
#include "Core/Platform/PlatformSystem.hpp"
#include "Core/Platform/PlatformWindow.hpp"
#include "Core/Utils/Path.hpp"

namespace
{
	std::optional<FilePath> EnvironmentDirectory(const char *variableName, const FilePath &suffix = {})
	{
		const char *value = std::getenv(variableName);
		if (value == nullptr || *value == '\0')
			return std::nullopt;

		FilePath directory(value);
		if (!suffix.empty())
			directory /= suffix;
		return directory;
	}

	void AppendEnvironmentDirectory(std::vector<FilePath> &directories, const char *variableName, const FilePath &suffix = {})
	{
		if (auto directory = EnvironmentDirectory(variableName, suffix))
			directories.push_back(std::move(*directory));
	}
}

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
		return localtime_r(&time, &outLocalTime) != nullptr;
	}

	std::vector<FilePath> GetSystemFontDirectories()
	{
		std::vector<FilePath> directories;
		AppendEnvironmentDirectory(directories, "HOME", "Library/Fonts");
		directories.emplace_back("/Library/Fonts");
		directories.emplace_back("/System/Library/Fonts");
		return directories;
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
