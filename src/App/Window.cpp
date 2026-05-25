#include "Core/dspch.hpp"

#include "App/Window.hpp"
#include "App/ApplicationState.hpp"
#include "App/Events/ApplicationConfigEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Platform/PlatformWindow.hpp"

#include <algorithm>
#include <functional>

#include <GLFW/glfw3.h>

namespace DefectStudio
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeWindow(
			EventBus &bus,
			Window &window,
			void (Window::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &window));
		}
	}

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

	bool Window::Create(int width, int height, const std::string &title, const Path &iconPath)
	{
		DS_ASSERT(m_Handle == nullptr, "Window already created");

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
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

		Platform::InitializeWindowPlatform(m_Handle, iconPath);
		DS_LOG_INFO("GLFW window created");
		return true;
	}

	void Window::Destroy()
	{
		if (m_Handle == nullptr)
			return;

		DS_LOG_INFO("Destroying GLFW window");
		ClearSubscriptions();
		Platform::ShutdownWindowPlatform(m_Handle);
		glfwDestroyWindow(m_Handle);
		m_Handle = nullptr;
		m_EventBus.reset();
	}

	void Window::BindEventBus(Ref<EventBus> eventBus)
	{
		ClearSubscriptions();
		m_EventBus = std::move(eventBus);
		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("Window event bus binding skipped: EventBus unavailable");
			return;
		}

		using namespace AppEvents::Config;
		AddSubscription(subscribeWindow<Applied>(*m_EventBus, *this, &Window::onConfigApplied));
		DS_LOG_INFO("Window config event handlers bound");
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

	void Window::ApplyConfig(const WindowConfig &config, bool applyPositionAndSize)
	{
		if (m_Handle == nullptr)
		{
			DS_LOG_WARN("Window config apply skipped: GLFW handle unavailable");
			return;
		}

		setTitle(config.title);

		const bool currentlyMaximized = glfwGetWindowAttrib(m_Handle, GLFW_MAXIMIZED) != 0;
		if (currentlyMaximized && !config.maximized)
			glfwRestoreWindow(m_Handle);
		else if (!currentlyMaximized && config.maximized)
			glfwMaximizeWindow(m_Handle);

		// Apply position and size only during initialization or if explicitly requested
		if (applyPositionAndSize)
		{
			const bool hasExplicitPosition = config.x >= 0 && config.y >= 0;
			if (hasExplicitPosition)
				glfwSetWindowPos(m_Handle, config.x, config.y);

			glfwSetWindowSize(m_Handle, std::max(320, config.width), std::max(240, config.height));

			DS_LOG_INFO(
				"Window config applied (full): title=\"{}\" size={}x{} position=({}, {}) explicit_position={} maximized={}",
				config.title,
				std::max(320, config.width),
				std::max(240, config.height),
				config.x,
				config.y,
				hasExplicitPosition,
				config.maximized);
		}
		else
		{
			DS_LOG_INFO(
				"Window config applied (title+maximized only): title=\"{}\" maximized={} (position and size preserved from current state)",
				config.title,
				config.maximized);
		}
	}

	void Window::CaptureConfig(WindowConfig &config) const
	{
		if (m_Handle == nullptr)
		{
			DS_LOG_WARN("Window config capture skipped: GLFW handle unavailable");
			return;
		}

		int x = config.x;
		int y = config.y;
		int width = config.width;
		int height = config.height;

		glfwGetWindowPos(m_Handle, &x, &y);
		glfwGetWindowSize(m_Handle, &width, &height);

		config.x = x;
		config.y = y;
		config.width = std::max(320, width);
		config.height = std::max(240, height);
		config.maximized = glfwGetWindowAttrib(m_Handle, GLFW_MAXIMIZED) != 0;
		DS_LOG_INFO(
			"Window config captured: size={}x{} position=({}, {}) maximized={}",
			config.width,
			config.height,
			config.x,
			config.y,
			config.maximized);
	}

	GLFWwindow *Window::GetNativeHandle() const
	{
		return m_Handle;
	}

	void Window::setTitle(const std::string &title)
	{
		if (m_Handle == nullptr)
			return;

		glfwSetWindowTitle(m_Handle, title.c_str());
	}

	void Window::onConfigApplied(const AppEvents::Config::Applied &event)
	{
		DS_LOG_INFO("Window received config applied event: persisted={}", event.persisted);
		ApplyConfig(event.config.window);
	}
} // namespace DefectStudio
