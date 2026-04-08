#pragma once

#include <filesystem>

struct GLFWwindow;

namespace DefectStudio::Platform
{
	void InitializeWindowPlatform(GLFWwindow *window, const std::filesystem::path &iconPath);
	void ShutdownWindowPlatform(GLFWwindow *window);
} // namespace DefectStudio::Platform
