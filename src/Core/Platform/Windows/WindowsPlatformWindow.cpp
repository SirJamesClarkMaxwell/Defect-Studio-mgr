#include "Core/dspch.hpp"

#if defined(_WIN32)


#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <windows.h>

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Platform/PlatformPaths.hpp"
#include "Core/Platform/PlatformSystem.hpp"
#include "Core/Platform/PlatformWindow.hpp"

struct WindowIconState
{
	HICON icon = nullptr;
	HICON previousClassIcon = nullptr;
	HICON previousClassSmallIcon = nullptr;
};

static std::unordered_map<GLFWwindow *, WindowIconState> s_WindowIcons;

namespace
{
	constexpr wchar_t kWindowIconResourceName[] = L"IDI_ICON1";
	DefectStudio::Platform::NativeCrashCallback s_NativeCrashCallback = nullptr;

	DefectStudio::Path GetExecutableDirectory()
	{
		wchar_t modulePath[MAX_PATH] = {};
		const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
		if (modulePathLength == 0 || modulePathLength >= MAX_PATH)
			return {};

		return DefectStudio::Path::FromResolved(FilePath(modulePath).parent_path());
	}

	DefectStudio::Path ResolveIconPath(const DefectStudio::Path &iconPath)
	{
		if (iconPath.Empty())
			return {};

		if (iconPath.Native().is_absolute() && FileSystem::Exists(iconPath.Native()))
			return DefectStudio::Path::FromResolved(iconPath.Native());

		const DefectStudio::Path fromCurrentDirectory = DefectStudio::Path::FromResolved(FileSystem::CurrentPath()) / iconPath;
		if (FileSystem::Exists(fromCurrentDirectory.Native()))
			return fromCurrentDirectory;

		const DefectStudio::Path executableDirectory = GetExecutableDirectory();
		if (!executableDirectory.Empty())
		{
			const DefectStudio::Path fromExecutableDirectory = executableDirectory / iconPath;
			if (FileSystem::Exists(fromExecutableDirectory.Native()))
				return fromExecutableDirectory;
		}

		return {};
	}

	HICON LoadWindowIconFromResource()
	{
		const HINSTANCE moduleHandle = GetModuleHandleW(nullptr);
		if (moduleHandle == nullptr)
			return nullptr;

		return static_cast<HICON>(LoadImageW(moduleHandle, kWindowIconResourceName, IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
	}

	HICON LoadWindowIconFromFile(const DefectStudio::Path &iconPath)
	{
		const DefectStudio::Path resolvedIconPath = ResolveIconPath(iconPath);
		if (resolvedIconPath.Empty())
			return nullptr;

		return static_cast<HICON>(LoadImageW(nullptr,
		                                     resolvedIconPath.wstring().c_str(),
		                                     IMAGE_ICON,
		                                     0,
		                                     0,
		                                     LR_DEFAULTSIZE | LR_LOADFROMFILE));
	}

	WindowIconState ApplyWindowIcon(HWND hwnd, HICON icon)
	{
		WindowIconState state;
		if (hwnd == nullptr || icon == nullptr)
			return state;

		state.icon = icon;
		state.previousClassIcon = reinterpret_cast<HICON>(SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(icon)));
		state.previousClassSmallIcon = reinterpret_cast<HICON>(SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(icon)));
		SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
		SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
		return state;
	}

	std::optional<FilePath> EnvironmentDirectory(const char *variableName, const FilePath &suffix = {})
	{
		char *buffer = nullptr;
		std::size_t bufferSize = 0;
		if (_dupenv_s(&buffer, &bufferSize, variableName) != 0 || buffer == nullptr)
			return std::nullopt;

		std::string value(buffer);
		std::free(buffer);
		if (value.empty())
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

	LONG WINAPI WindowsUnhandledExceptionFilter(EXCEPTION_POINTERS *exceptionPointers)
	{
		const unsigned long code = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
			? exceptionPointers->ExceptionRecord->ExceptionCode
			: 0UL;
		const unsigned long flags = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
			? exceptionPointers->ExceptionRecord->ExceptionFlags
			: 0UL;
		void *address = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
			? exceptionPointers->ExceptionRecord->ExceptionAddress
			: nullptr;

		DefectStudio::Logger::Flush();
		char buffer[256] = {};
		std::snprintf(buffer,
		              sizeof(buffer),
		              "[CRASH] unhandled SEH exception code=0x%08lX flags=0x%08lX address=%p",
		              code,
		              flags,
		              address);
		if (s_NativeCrashCallback != nullptr)
			s_NativeCrashCallback(buffer);
		return EXCEPTION_EXECUTE_HANDLER;
	}
}

namespace DefectStudio::Platform
{
	void DebugBreak()
	{
		::DebugBreak();
	}

	void InstallNativeCrashHandler(NativeCrashCallback callback)
	{
		s_NativeCrashCallback = callback;
		SetUnhandledExceptionFilter(WindowsUnhandledExceptionFilter);
	}

	bool LocalTime(std::time_t time, std::tm &outLocalTime)
	{
		return localtime_s(&outLocalTime, &time) == 0;
	}

	std::vector<FilePath> GetSystemFontDirectories()
	{
		std::vector<FilePath> directories;
		AppendEnvironmentDirectory(directories, "WINDIR", "Fonts");
		AppendEnvironmentDirectory(directories, "SystemRoot", "Fonts");
		directories.emplace_back("C:/Windows/Fonts");
		return directories;
	}

	void InitializeWindowPlatform(GLFWwindow *window, const Path &iconPath)
	{
		if (window == nullptr)
			return;

		const HWND hwnd = glfwGetWin32Window(window);
		if (hwnd == nullptr)
			return;

		BOOL useDark = TRUE;
		if (DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark)) != S_OK)
			DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));

		HICON icon = LoadWindowIconFromResource();
		if (icon == nullptr)
			icon = LoadWindowIconFromFile(iconPath);
		if (icon == nullptr)
		{
			DS_LOG_WARN("Failed to assign a window icon");
			return;
		}

		s_WindowIcons[window] = ApplyWindowIcon(hwnd, icon);
		DS_LOG_INFO("Windows icon assigned");
	}

	void ShutdownWindowPlatform(GLFWwindow *window)
	{
		if (window == nullptr)
			return;

		auto iconIt = s_WindowIcons.find(window);
		if (iconIt == s_WindowIcons.end())
			return;

		const HWND hwnd = glfwGetWin32Window(window);
		if (hwnd != nullptr)
		{
			SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(iconIt->second.previousClassIcon));
			SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(iconIt->second.previousClassSmallIcon));
			SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconIt->second.previousClassIcon));
			SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconIt->second.previousClassSmallIcon));
		}

		DestroyIcon(iconIt->second.icon);
		s_WindowIcons.erase(iconIt);
	}
} // namespace DefectStudio::Platform

#endif
