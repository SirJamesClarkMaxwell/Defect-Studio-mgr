#include "Core/dspch.hpp"

#include <utility>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#endif

#include "App/Application.hpp"
#include "App/ApplicationInternals.hpp"
#include "App/Managers/ConfigManager.hpp"
#include "App/Window.hpp"
#include "Core/Assets/AssetManager.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/Path.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

namespace DefectStudio
{
	// ===== Low-level platform/graphics setup =====

	void Application::configureInputBackend()
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");

		GLFWwindow *nativeHandle = m_Graphics.window->GetNativeHandle();
		DS_ASSERT(nativeHandle != nullptr, "Native window handle is null");

		InputBackend backend;
		backend.isKeyDown = [nativeHandle](KeyCode code) {
			const int state = glfwGetKey(nativeHandle, ToNativeKeyCode(code));
			return state == GLFW_PRESS || state == GLFW_REPEAT;
		};
		backend.isMouseButtonDown = [nativeHandle](MouseCode code) {
			const int state = glfwGetMouseButton(nativeHandle, ToNativeMouseCode(code));
			return state == GLFW_PRESS;
		};
		backend.mousePosition = [nativeHandle]() {
			double x = 0.0;
			double y = 0.0;
			glfwGetCursorPos(nativeHandle, &x, &y);
			return std::make_pair(static_cast<float>(x), static_cast<float>(y));
		};

		Input::SetBackend(std::move(backend));
	}


	bool Application::initializeGlfw()
	{
		ZoneScopedN("Application.initializeGlfw.detail");
		ApplicationDetail::StartupStepTimer timer("GLFW.initialize");
		if (!glfwInit())
		{
			timer.Finish(false);
			DS_LOG_ERROR("Failed to initialize GLFW");
			return false;
		}

		m_Graphics.glfwInitialized = true;
		DS_LOG_INFO("GLFW initialized");
		return timer.Finish(true);
	}


	bool Application::createMainWindow()
	{
		ZoneScopedN("Application.createMainWindow.detail");
		ApplicationDetail::StartupStepTimer timer("Window.createMainWindow.detail");
		DS_ASSERT(m_ConfigManager != nullptr, "ConfigManager is not initialized");

		m_Graphics.window = CreateRef<Window>();
		Path iconPath = Path("install") / "app" / "assets" / "icon.ico";
		if (m_AssetManager != nullptr)
		{
			auto resolvedIcon = m_AssetManager->ResolvePath("assets/icon.ico");
			if (resolvedIcon)
				iconPath = resolvedIcon->resolvedPath;
		}

		if (!m_Graphics.window->Create(m_Config.window.width, m_Config.window.height, m_Config.window.title, iconPath))
		{
			m_Graphics.window.reset();
			timer.Finish(false);
			return false;
		}

		{
			ZoneScopedN("Application.Window.BindEventBus");
			ApplicationDetail::StartupStepTimer bindTimer("Window.BindEventBus");
			m_Graphics.window->BindEventBus(m_EventBus);
			bindTimer.Finish(true);
		}
		{
			ZoneScopedN("Application.Window.ApplyConfig");
			ApplicationDetail::StartupStepTimer configTimer("Window.ApplyConfig.initial");
			m_Graphics.window->ApplyConfig(m_Config.window, true); // Apply full config during initialization
			configTimer.Finish(true);
		}

		m_Graphics.window->SetEventCallback([this](EventVariant event) { queueEvent(std::move(event)); });

		{
			ZoneScopedN("Application.configureInputBackend");
			ApplicationDetail::StartupStepTimer inputTimer("Window.configureInputBackend");
			configureInputBackend();
			inputTimer.Finish(true);
		}

		return timer.Finish(true);
	}


	bool Application::initializeGraphics()
	{
		ZoneScopedN("Application.initializeGraphics.detail");
		ApplicationDetail::StartupStepTimer timer("OpenGL.initializeGraphics");
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");

		const int glVersion = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
		if (glVersion == 0)
		{
			timer.Finish(false);
			DS_LOG_ERROR("Failed to initialize GLAD");
			return false;
		}

		m_Graphics.gladInitialized = true;
		DS_LOG_INFO("OpenGL loaded: {}.{}", GLAD_VERSION_MAJOR(glVersion), GLAD_VERSION_MINOR(glVersion));
		return timer.Finish(true);
	}


	void Application::shutdownWindow()
	{
		if (m_Graphics.window == nullptr)
			return;

		Input::ResetBackend();
		m_Graphics.window->Destroy();
		m_Graphics.window.reset();
	}


	void Application::shutdownGlfw()
	{
		if (!m_Graphics.glfwInitialized)
			return;

		glfwTerminate();
		m_Graphics.glfwInitialized = false;
		m_Graphics.gladInitialized = false;

		DS_LOG_INFO("DefectStudio GUI shell stopped");
	}


} // namespace DefectStudio
