#include "Core/dspch.hpp"

#if defined(_WIN32)


#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <windows.h>

#include "Core/Utils/Logger.hpp"
#include "Core/Platform/PlatformWindow.hpp"

static std::unordered_map<GLFWwindow *, HICON> s_WindowIcons;

namespace DefectStudio::Platform
{
	void InitializeWindowPlatform(GLFWwindow *window, const std::filesystem::path &iconPath)
	{
		if (window == nullptr)
			return;

		const HWND hwnd = glfwGetWin32Window(window);
		if (hwnd == nullptr)
			return;

		BOOL useDark = TRUE;
		if (DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark)) != S_OK)
			DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));

		const std::filesystem::path absoluteIconPath = std::filesystem::current_path() / iconPath;
		HICON icon = static_cast<HICON>(LoadImageW(nullptr, absoluteIconPath.wstring().c_str(), IMAGE_ICON, 0, 0,
		                                           LR_DEFAULTSIZE | LR_LOADFROMFILE));
		if (icon == nullptr)
			return;

		SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
		s_WindowIcons[window] = icon;
		DS_LOG_INFO("Windows icon assigned");
	}

	void ShutdownWindowPlatform(GLFWwindow *window)
	{
		if (window == nullptr)
			return;

		auto iconIt = s_WindowIcons.find(window);
		if (iconIt == s_WindowIcons.end())
			return;

		DestroyIcon(iconIt->second);
		s_WindowIcons.erase(iconIt);
	}
} // namespace DefectStudio::Platform

#endif
