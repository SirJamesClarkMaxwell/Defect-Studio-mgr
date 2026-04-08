#include "Core/dspch.hpp"

#include <GLFW/glfw3.h>

#include "App/Window.hpp"
#include "Core/Assert.hpp"
#include "Core/Logger.hpp"
#include "Core/Platform/PlatformWindow.hpp"

namespace DefectStudio
{
	Window::~Window()
	{
		Destroy();
	}

	bool Window::Create(int width, int height, const std::string &title)
	{
		DS_ASSERT(m_Handle == nullptr, "Window already created");

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		m_Handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
		if (m_Handle == nullptr)
		{
			DS_LOG_ERROR("Failed to create GLFW window");
			return false;
		}

		glfwMakeContextCurrent(m_Handle);
		glfwSwapInterval(1);

		Platform::InitializeWindowPlatform(m_Handle, std::filesystem::path("assets") / "icon.ico");
		DS_LOG_INFO("GLFW window created");
		return true;
	}

	void Window::Destroy()
	{
		if (m_Handle == nullptr)
			return;

		Platform::ShutdownWindowPlatform(m_Handle);
		glfwDestroyWindow(m_Handle);
		m_Handle = nullptr;
	}

	bool Window::ShouldClose() const
	{
		DS_ASSERT(m_Handle != nullptr, "Window handle is null");
		return glfwWindowShouldClose(m_Handle) != 0;
	}

	void Window::PollEvents() const
	{
		glfwPollEvents();
	}

	void Window::SwapBuffers() const
	{
		DS_ASSERT(m_Handle != nullptr, "Window handle is null");
		glfwSwapBuffers(m_Handle);
	}

	void Window::GetFramebufferSize(int &width, int &height) const
	{
		DS_ASSERT(m_Handle != nullptr, "Window handle is null");
		glfwGetFramebufferSize(m_Handle, &width, &height);
	}

	GLFWwindow *Window::GetNativeHandle() const
	{
		return m_Handle;
	}
} // namespace DefectStudio
