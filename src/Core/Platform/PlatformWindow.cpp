#include "Core/dspch.hpp"

#include "Core/Platform/PlatformWindow.hpp"

#if !defined(_WIN32) && !defined(__linux__)
namespace DefectStudio::Platform
{
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
