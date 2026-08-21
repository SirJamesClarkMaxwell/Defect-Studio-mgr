#include "Core/dspch.hpp"

#if defined(__linux__)

#include <climits>
#include <csignal>

#include <unistd.h>

#include <GLFW/glfw3.h>

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

	void AppendNativeCrashStackTrace(NativeCrashCallback callback, unsigned int framesToSkip)
	{
		(void)callback;
		(void)framesToSkip;
	}

	bool LocalTime(std::time_t time, std::tm &outLocalTime)
	{
		return localtime_r(&time, &outLocalTime) != nullptr;
	}

	std::vector<FilePath> GetSystemFontDirectories()
	{
		std::vector<FilePath> directories;
		AppendEnvironmentDirectory(directories, "HOME", ".local/share/fonts");
		AppendEnvironmentDirectory(directories, "HOME", ".fonts");
		directories.emplace_back("/usr/local/share/fonts");
		directories.emplace_back("/usr/share/fonts");
		return directories;
	}

	Path GetExecutableDirectory()
	{
		char buffer[PATH_MAX] = {};
		const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
		if (length <= 0)
			return {};

		return Path::FromResolved(FilePath(std::string(buffer, static_cast<std::size_t>(length))).parent_path());
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
