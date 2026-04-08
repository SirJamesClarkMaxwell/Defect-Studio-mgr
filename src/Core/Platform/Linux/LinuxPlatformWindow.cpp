#include "Core/dspch.hpp"

#if defined(__linux__)

#include <GLFW/glfw3.h>

#include "Core/Platform/PlatformWindow.hpp"

namespace DefectStudio::Platform
{
	void InitializeWindowPlatform(GLFWwindow *window, const std::filesystem::path &iconPath)
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
