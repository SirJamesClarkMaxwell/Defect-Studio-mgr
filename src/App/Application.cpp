#include "Core/dspch.hpp"

#include <functional>
#include <optional>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#endif

#include "App/Application.hpp"
#include "App/Controllers/ApplicationEventController.hpp"
#include "App/ApplicationInternals.hpp"
#include "Core/CoreLayer.hpp"
#include "Core/Logging/LogRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Threading/ThreadAffinity.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/Utils/RuntimeTuning.hpp"
#include "Presentation/ImGuiLayer.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <imgui.h>

namespace DefectStudio
{
	std::optional<std::reference_wrapper<Application>> Application::s_Instance = std::nullopt;

	// ===== Singleton lifecycle API =====

	Application Application::Create(int argc, char **argv)
	{
		return Application(argc, argv);
	}


	int Application::Run()
	{
		if (!m_Runtime.lifecycle.IsRunning())
			return 1;

		ApplicationDetail::SetCrashStage("main loop");
		try
		{
			mainLoop();
		}
		catch (const std::exception &exception)
		{
			DS_LOG_CRITICAL("Unhandled exception in main loop: {}", exception.what());
			Logger::Flush();
			ApplicationDetail::AppendCrashMarker("[CRASH] unhandled C++ exception in main loop");
			shutdownInternal();
			return 1;
		}
		catch (...)
		{
			DS_LOG_CRITICAL("Unhandled non-standard exception in main loop");
			Logger::Flush();
			ApplicationDetail::AppendCrashMarker("[CRASH] unhandled unknown exception in main loop");
			shutdownInternal();
			return 1;
		}

		ApplicationDetail::SetCrashStage("shutdown");
		shutdownInternal();
		return 0;
	}


	void Application::Shutdown()
	{
		shutdownInternal();
	}


	void Application::EmitEvent(EventVariant event)
	{
		DS_ASSERT(s_Instance.has_value(), "Application not created");
		s_Instance->get().queueEvent(std::move(event));
	}


	Application &Application::Get()
	{
		DS_ASSERT(s_Instance.has_value(), "Application not created");
		return s_Instance->get();
	}


	EventBus &Application::GetEventBus()
	{
		DS_ASSERT(m_EventBus != nullptr, "EventBus not initialized");
		return *m_EventBus;
	}


	Ref<EventBus> Application::GetEventBusRef() const
	{
		DS_ASSERT(m_EventBus != nullptr, "EventBus not initialized");
		return m_EventBus;
	}


	Notifier &Application::GetNotifier()
	{
		DS_ASSERT(m_Notifier != nullptr, "Notifier not initialized");
		return *m_Notifier;
	}


	Ref<Notifier> Application::GetNotifierRef() const
	{
		DS_ASSERT(m_Notifier != nullptr, "Notifier not initialized");
		return m_Notifier;
	}


	CapabilityService &Application::GetCapabilityService()
	{
		DS_ASSERT(m_CapabilityService != nullptr, "CapabilityService not initialized");
		return *m_CapabilityService;
	}


	AssetManager &Application::GetAssetManager()
	{
		DS_ASSERT(m_AssetManager != nullptr, "AssetManager not initialized");
		return *m_AssetManager;
	}


	Ref<LogRegistry> Application::GetLogRegistry() const
	{
		return m_LogRegistry;
	}


	void Application::ShowBlockingError(const StructuredError &error)
	{
		DS_LOG_CRITICAL("Blocking error: {} [{}]", error.userMessage, error.code);
		if (auto *imguiLayer = m_LayerStack.FindLayer<ImGuiLayer>())
			imguiLayer->SetBlockingError(error);
	}

	JobSystem &Application::GetJobSystem()
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		DS_ASSERT(coreLayer != nullptr, "CoreLayer not initialized");
		return coreLayer->GetJobSystem();
	}


	ProgressTracker &Application::GetProgressTracker()
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		DS_ASSERT(coreLayer != nullptr, "CoreLayer not initialized");
		return coreLayer->GetProgressTracker();
	}


	WeakRef<ConfigManager> Application::GetConfigManager() const
	{
		if (m_ConfigManager == nullptr)
			return {};
		return CreateWeakRef(m_ConfigManager);
	}


	const ApplicationConfig &Application::GetConfig() const
	{
		return m_Config;
	}


	// Extension point - see declaration in Application.hpp.
	// void Application::ProcessQueuedEvents()
	// {
	// 	DS_ASSERT(s_Instance.has_value(), "Application not created");
	// 	s_Instance->get().processPendingEvents();
	// }

	// ===== Instance lifecycle =====

	Application::Application(int argc, char **argv)
	{
		ApplicationDetail::InstallCrashFallbackHandlers();
		Threading::SetMainThread();
		ApplicationDetail::SetCrashStage("construct application");
		m_LogRegistry = CreateRef<LogRegistry>();
		m_EventQueue.Configure(RuntimeDefaults::EventQueueInitialCapacity, RuntimeDefaults::EventQueueGrowthStep);

		m_Runtime.argc = argc;
		m_Runtime.argv = argv;

		ApplicationSpecification spec = ApplicationDetail::ParseApplicationArguments(m_Runtime.argc,m_Runtime.argv);
		bool created = createFromSpecification(spec);
		if (!created)
			DS_LOG_ERROR("Application creation failed");
	}


	Application::~Application()
	{
		shutdownInternal();
		if (s_Instance.has_value() && &s_Instance->get() == this)
			s_Instance.reset();
	}


	// ===== High-level runtime flow =====

	void Application::mainLoop()
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");

		ImGuiIO &io = ImGui::GetIO();
		const ImVec4 clearColor = ImVec4(
			m_Config.appearance.clearColor[0],
			m_Config.appearance.clearColor[1],
			m_Config.appearance.clearColor[2],
			m_Config.appearance.clearColor[3]);
		m_Runtime.lastFrameTime = glfwGetTime();
		DS_LOG_INFO("Entering render loop");
		TracyMessageL("Entering render loop");

		while (m_Runtime.Running() && !m_Graphics.window->ShouldClose())
			runMainLoopFrame(clearColor, io);
	}


	template <typename TEvent>
	void Application::OnEvent(TEvent &event)
	{
		if (m_Runtime.specification.traceEvents)
		{
			if constexpr (std::is_same_v<TEvent, MouseMovedEvent> || std::is_same_v<TEvent, MouseScrolledEvent>)
			{
				// High-frequency pointer events are intentionally skipped in trace logging.
			}
			else
			{
				DS_LOG_DEBUG("Event: {}", event.GetName());
			}
		}

		HandleLifecycleEvent(event, m_Runtime.lifecycle);

		if (event.handled)
			return;

		// ImGui text fields (window rename, search boxes, numeric inputs, ...) should own the
		// keyboard while focused - without this, app-wide shortcuts (e.g. "B" -> align to B axis)
		// fire on every keystroke typed into any text field, anywhere in the app.
		if constexpr (std::is_same_v<TEvent, KeyPressedEvent> || std::is_same_v<TEvent, KeyRepeatedEvent>)
		{
			if (ImGui::GetIO().WantCaptureKeyboard)
			{
				event.handled = true;
				return;
			}
		}

		dispatchEventToLayers(event);
	}


	void Application::runMainLoopFrame(const ImVec4 &clearColor, ImGuiIO &io)
	{
		const double now = glfwGetTime();
		const float deltaTime = static_cast<float>(now - m_Runtime.lastFrameTime);
		m_Runtime.lastFrameTime = now;


		onUpdate(deltaTime);

		beginFrame();
		for (const auto &layer : m_LayerStack)
			layer->OnImGuiRender();
		onRender(clearColor, io.Framerate);
	}


	void Application::onUpdate(float deltaTime)
	{
		for (const auto &layer : m_LayerStack)
			layer->OnUpdate(deltaTime);

		m_Graphics.window->PollEvents();
		processPendingEvents();
	}


	void Application::queueEvent(EventVariant event)
	{
		m_EventQueue.Add(std::move(event));
	}


	void Application::processPendingEvents()
	{
		std::vector<EventVariant> events = m_EventQueue.Drain();
		if (events.empty())
			return;

		for (auto &variant : events)
		{
			std::visit([this](auto &event) { OnEvent(event); }, variant);
		}
	}

	void Application::onRender(const ImVec4 &clearColor, float frameRate)
	{
		renderFrame(clearColor, frameRate);
	}


	template <typename TEvent>
	void Application::dispatchEventToLayers(TEvent &event)
	{
		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			auto &layer = *it;
			layer->OnEvent(event);
			if (event.handled)
				break;
		}
	}


	void Application::beginFrame()
	{
		auto imGuiLayer = m_LayerStack.FindLayerAs<ImGuiLayer>(LayerId::ImGui).lock();
		DS_ASSERT(imGuiLayer != nullptr, "ImGuiLayer not initialized");
		imGuiLayer->BeginFrame();
	}


	void Application::renderFrame(const ImVec4 &clearColor, float frameRate)
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");
		(void)frameRate;

		int displayWidth = 0;
		int displayHeight = 0;
		m_Graphics.window->GetFramebufferSize(displayWidth, displayHeight);
		glViewport(0, 0, displayWidth, displayHeight);
		glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
		glClear(GL_COLOR_BUFFER_BIT);
		if (auto imGuiLayer = m_LayerStack.FindLayerAs<ImGuiLayer>(LayerId::ImGui).lock())
		{
			imGuiLayer->EndFrame();
			imGuiLayer->Render();
		}
		TracyPlot("FPS", frameRate);

		m_Graphics.window->SwapBuffers();
		FrameMark;
	}


	void Application::presentLoadingFrame()
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");

		const ImVec4 clearColor = ImVec4(
			m_Config.appearance.clearColor[0],
			m_Config.appearance.clearColor[1],
			m_Config.appearance.clearColor[2],
			m_Config.appearance.clearColor[3]);

		int displayWidth = 0;
		int displayHeight = 0;
		m_Graphics.window->GetFramebufferSize(displayWidth, displayHeight);
		glViewport(0, 0, displayWidth, displayHeight);
		glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
		glClear(GL_COLOR_BUFFER_BIT);
		m_Graphics.window->SwapBuffers();
		m_Graphics.window->PollEvents();
	}


	template void Application::OnEvent<WindowCloseEvent>(WindowCloseEvent &);
	template void Application::OnEvent<WindowResizeEvent>(WindowResizeEvent &);
	template void Application::OnEvent<KeyPressedEvent>(KeyPressedEvent &);
	template void Application::OnEvent<KeyReleasedEvent>(KeyReleasedEvent &);
	template void Application::OnEvent<KeyRepeatedEvent>(KeyRepeatedEvent &);
	template void Application::OnEvent<MouseButtonPressedEvent>(MouseButtonPressedEvent &);
	template void Application::OnEvent<MouseButtonReleasedEvent>(MouseButtonReleasedEvent &);
	template void Application::OnEvent<MouseMovedEvent>(MouseMovedEvent &);
	template void Application::OnEvent<MouseScrolledEvent>(MouseScrolledEvent &);
	template void Application::OnEvent<TouchpadGestureEvent>(TouchpadGestureEvent &);
} // namespace DefectStudio
