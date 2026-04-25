#include "Core/dspch.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <GLFW/glfw3.h>

#include "App/Window.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Platform/PlatformWindow.hpp"

namespace DefectStudio
{
	static Window *GetWindowInstance(GLFWwindow *window)
	{
		return static_cast<Window *>(glfwGetWindowUserPointer(window));
	}

	static void OnGlfwWindowClose(GLFWwindow *window)
	{
		Window *instance = GetWindowInstance(window);
		if (instance == nullptr)
			return;

		WindowCloseEvent event;
		instance->DispatchEvent(event);
	}

	static void OnGlfwWindowSize(GLFWwindow *window, int newWidth, int newHeight)
	{
		Window *instance = GetWindowInstance(window);
		if (instance == nullptr)
			return;

		WindowResizeEvent event(newWidth, newHeight);
		instance->DispatchEvent(event);
	}

	static void OnGlfwKey(GLFWwindow *window, int key, int scancode, int action, int mods)
	{
		(void)scancode;
		(void)mods;

		Window *instance = GetWindowInstance(window);
		if (instance == nullptr)
			return;

		if (action == GLFW_PRESS)
		{
			KeyPressedEvent event(key);
			instance->DispatchEvent(event);
		}
		else if (action == GLFW_REPEAT)
		{
			KeyRepeatedEvent event(key);
			instance->DispatchEvent(event);
		}
		else if (action == GLFW_RELEASE)
		{
			KeyReleasedEvent event(key);
			instance->DispatchEvent(event);
		}
	}

	static void OnGlfwMouseButton(GLFWwindow *window, int button, int action, int mods)
	{
		(void)mods;

		Window *instance = GetWindowInstance(window);
		if (instance == nullptr)
			return;

		if (action == GLFW_PRESS)
		{
			MouseButtonPressedEvent event(button);
			instance->DispatchEvent(event);
		}
		else if (action == GLFW_RELEASE)
		{
			MouseButtonReleasedEvent event(button);
			instance->DispatchEvent(event);
		}
	}

	static void OnGlfwCursorPos(GLFWwindow *window, double x, double y)
	{
		Window *instance = GetWindowInstance(window);
		if (instance == nullptr)
			return;

		MouseMovedEvent event(static_cast<float>(x), static_cast<float>(y));
		instance->DispatchEvent(event);
	}

	static void OnGlfwScroll(GLFWwindow *window, double offsetX, double offsetY)
	{
		Window *instance = GetWindowInstance(window);
		if (instance == nullptr)
			return;

		MouseScrolledEvent event(static_cast<float>(offsetX), static_cast<float>(offsetY));
		instance->DispatchEvent(event);
	}

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
		glfwSetWindowUserPointer(m_Handle, this);

		glfwSetWindowCloseCallback(m_Handle, OnGlfwWindowClose);
		glfwSetWindowSizeCallback(m_Handle, OnGlfwWindowSize);
		glfwSetKeyCallback(m_Handle, OnGlfwKey);
		glfwSetMouseButtonCallback(m_Handle, OnGlfwMouseButton);
		glfwSetCursorPosCallback(m_Handle, OnGlfwCursorPos);
		glfwSetScrollCallback(m_Handle, OnGlfwScroll);

		Platform::InitializeWindowPlatform(m_Handle, Path("install") / "app" / "assets" / "icon.ico");
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

	void Window::SetEventCallback(EventCallback callback)
	{
		m_EventCallback = std::move(callback);
	}

	void Window::DispatchEvent(EventVariant event)
	{
		if (!m_EventCallback)
			return;

		m_EventCallback(std::move(event));
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
