#include "Core/dspch.hpp"

#if defined(_WIN32)


#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <windows.h>

#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
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

	DefectStudio::Path GetExecutableDirectory()
	{
		wchar_t modulePath[MAX_PATH] = {};
		const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
		if (modulePathLength == 0 || modulePathLength >= MAX_PATH)
			return {};

		return DefectStudio::Path::FromResolved(std::filesystem::path(modulePath).parent_path());
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
}

namespace DefectStudio::Platform
{
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
