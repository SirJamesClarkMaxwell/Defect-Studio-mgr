#include "Core/dspch.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <exception>

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#endif

#include "App/Application.hpp"
#include "App/ConfigManager.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/CoreLayer.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/RuntimeTuning.hpp"
#include "Debug/DebugLayer.hpp"
#include "Demo/DemoLayer.hpp"
#include "Domain/DomainLayer.hpp"
#include "IO/IOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/EditorUiState.hpp"
#include "Presentation/ImGuiLayer.hpp"
#include "ScientificRuntime/ScientificRuntimeLayer.hpp"
#include "Storage/StorageLayer.hpp"

namespace DefectStudio
{
	std::optional<std::reference_wrapper<Application>> Application::s_Instance = std::nullopt;

	namespace
	{
		std::atomic<bool> g_CrashHandlersInstalled = false;
		std::atomic<const char *> g_CrashStage = "startup";

		void setCrashStage(const char *stage)
		{
			g_CrashStage.store(stage, std::memory_order_relaxed);
		}

		void appendLineToFile(const char *path, const char *message)
		{
			FILE *stream = nullptr;
#if defined(_WIN32)
			if (fopen_s(&stream, path, "a") != 0)
				return;
#else
			stream = std::fopen(path, "a");
#endif
			if (stream == nullptr)
				return;

			std::fprintf(stream, "%s\n", message);
			std::fflush(stream);
			std::fclose(stream);
		}

		void appendCrashMarker(const char *message)
		{
			FileSystem::CreateDirectories("logs");
			char line[512] = {};
			std::snprintf(line,
			              sizeof(line),
			              "[%lld] %s stage=%s",
			              static_cast<long long>(std::time(nullptr)),
			              message,
			              g_CrashStage.load(std::memory_order_relaxed));

			appendLineToFile("logs/DefectStudio-crash.log", line);
			appendLineToFile(RuntimeDefaults::LogFilePath, line);
		}

		void terminateHandler()
		{
			Logger::Flush();
			appendCrashMarker("[CRASH] std::terminate invoked");
			std::abort();
		}

		void signalHandler(int signalCode)
		{
			char buffer[128] = {};
			std::snprintf(buffer, sizeof(buffer), "[CRASH] signal=%d", signalCode);
			appendCrashMarker(buffer);
			std::_Exit(EXIT_FAILURE);
		}

#if defined(_WIN32)
		LONG WINAPI windowsUnhandledExceptionFilter(EXCEPTION_POINTERS *exceptionPointers)
		{
			const unsigned long code = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
				? exceptionPointers->ExceptionRecord->ExceptionCode
				: 0UL;
			const unsigned long flags = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
				? exceptionPointers->ExceptionRecord->ExceptionFlags
				: 0UL;
			void *address = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr
				? exceptionPointers->ExceptionRecord->ExceptionAddress
				: nullptr;

			Logger::Flush();
			char buffer[256] = {};
			std::snprintf(buffer,
			              sizeof(buffer),
			              "[CRASH] unhandled SEH exception code=0x%08lX flags=0x%08lX address=%p",
			              code,
			              flags,
			              address);
			appendCrashMarker(buffer);
			return EXCEPTION_EXECUTE_HANDLER;
		}
#endif

		void installCrashFallbackHandlers()
		{
			if (g_CrashHandlersInstalled.exchange(true))
				return;

			std::set_terminate(terminateHandler);
			std::signal(SIGABRT, signalHandler);
			std::signal(SIGSEGV, signalHandler);
#if defined(_WIN32)
			SetUnhandledExceptionFilter(windowsUnhandledExceptionFilter);
#endif
		}
	}

	// ===== File-local path and event helpers =====

	static bool HasConfigFiles(const Path &root)
	{
		const Path configPath = root.Join("config");
		return FileSystem::Exists(configPath.Join("ui_settings.yaml").Native())
			|| FileSystem::Exists(configPath.Join("default.yaml").Native());
	}

	static Path FindConfigDirectoryInAncestors(Path start)
	{
		Path current = Path::FromResolved(start.Native());
		for (int i = 0; i < 10; ++i)
		{
			if (HasConfigFiles(Path::FromResolved(current)))
				return Path::FromResolved(current / "config");

			const Path parent = current.parent_path();
			if (parent.empty() || parent == current)
				break;

			current = parent;
		}

		return Path{};
	}

		static Path ResolveConfigDirectory(char **argv)
	{
			const Path fromCwd = FindConfigDirectoryInAncestors(Path::FromResolved(FileSystem::CurrentPath()));
			if (!fromCwd.Empty())
			return fromCwd;

		if (argv != nullptr && argv[0] != nullptr)
		{
			std::error_code absoluteError;
			const Path executablePath = Path::FromResolved(FileSystem::Absolute(argv[0], absoluteError));
			if (!absoluteError && !executablePath.empty())
			{
					const Path fromExe = FindConfigDirectoryInAncestors(Path::FromResolved(executablePath.parent_path()));
					if (!fromExe.Empty())
					return fromExe;
			}
		}

			return Path::FromResolved(FileSystem::CurrentPath() / "config");
	}

	struct MainLoopState
	{
		bool showDemoWindow = true;
		bool showSettingsWindow = false;
		ImVec4 clearColor = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
	};

	ApplicationSpecification defaultApplicationSpecification();
	ApplicationSpecification parseApplicationArguments(int argc, char **argv);

	// ===== Singleton lifecycle API =====

	Application Application::Create(int argc, char **argv)
	{
		return Application(argc, argv);
	}

	int Application::Run()
	{
		if (!m_Runtime.lifecycle.IsRunning())
			return 1;

		setCrashStage("main loop");
		try
		{
			mainLoop();
		}
		catch (const std::exception &exception)
		{
			DS_LOG_CRITICAL("Unhandled exception in main loop: {}", exception.what());
			Logger::Flush();
			appendCrashMarker("[CRASH] unhandled C++ exception in main loop");
			shutdownInternal();
			return 1;
		}
		catch (...)
		{
			DS_LOG_CRITICAL("Unhandled non-standard exception in main loop");
			Logger::Flush();
			appendCrashMarker("[CRASH] unhandled unknown exception in main loop");
			shutdownInternal();
			return 1;
		}

		setCrashStage("shutdown");
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

	void Application::ProcessQueuedEvents()
	{
		DS_ASSERT(s_Instance.has_value(), "Application not created");
		s_Instance->get().processPendingEvents();
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

	// ===== Instance lifecycle =====

	Application::Application(int argc, char **argv)
	{
		installCrashFallbackHandlers();
		setCrashStage("construct application");

		if (s_Instance.has_value())
		{
			DS_LOG_WARN("Application::Create called more than once; replacing previous instance pointer");
		}

		DS_LOG_INFO("Application ctor: argc={}", argc);
		m_Runtime.argc = argc;
		m_Runtime.argv = argv;
		
		ApplicationSpecification spec = parseApplicationArguments(m_Runtime.argc,m_Runtime.argv);
		DS_LOG_INFO("Application ctor: arguments parsed");
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

	bool Application::createFromSpecification(const ApplicationSpecification &specification)
	{
		ZoneScoped;
		setCrashStage("create application");
		DS_LOG_INFO("CreateFromSpecification: begin");

		if (!m_Runtime.lifecycle.TryMarkCreated())
		{
			DS_LOG_WARN("Application::Create called more than once; call ignored");
			return false;
		}

		s_Instance = std::ref(*this);
		TracyMessageL("Launching GUI shell");

		m_Runtime.specification = specification;
		setCrashStage("resolve configuration directory");
		m_Config.directory = ResolveConfigDirectory(m_Runtime.argv);
		DS_LOG_INFO("CreateFromSpecification: resolved config directory={}", m_Config.directory.String());
		setCrashStage("bootstrap configuration");
		if (!bootstrapConfiguration())
		{
			DS_LOG_ERROR("CreateFromSpecification: configuration bootstrap failed");
			shutdownInternal();
			return false;
		}
		applySpecificationFromDefaultConfig();

		setCrashStage("initialize event dispatch");
		DS_LOG_INFO("CreateFromSpecification: init EventDispatchingSystem");
		if (!initializeEventDispatchingSystem())
		{
			DS_LOG_ERROR("CreateFromSpecification: EventDispatchingSystem init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: EventDispatchingSystem ready");

		setCrashStage("create event bus");
		DS_LOG_INFO("CreateFromSpecification: create EventBus");
		m_EventBus = CreateRef<EventBus>();
		if (!m_EventBus)
		{
			DS_LOG_ERROR("Failed to initialize EventBus");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: EventBus ready");

		setCrashStage("initialize logger");
		DS_LOG_INFO("CreateFromSpecification: init logger");
		initializeLogger();
		DS_LOG_INFO("CreateFromSpecification: logger ready");

		DS_LOG_INFO("Config directory: {}", m_Config.directory.String());
		logStartupSpecification();

		setCrashStage("initialize glfw");
		DS_LOG_INFO("CreateFromSpecification: init GLFW");
		if (!initializeGlfw())
		{
			DS_LOG_ERROR("CreateFromSpecification: GLFW init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: GLFW ready");

		setCrashStage("create main window");
		DS_LOG_INFO("CreateFromSpecification: create main window");
		if (!createMainWindow())
		{
			DS_LOG_ERROR("CreateFromSpecification: main window creation failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: main window ready");

		setCrashStage("initialize graphics");
		DS_LOG_INFO("CreateFromSpecification: init graphics");
		if (!initializeGraphics())
		{
			DS_LOG_ERROR("CreateFromSpecification: graphics init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: graphics ready");

		setCrashStage("initialize imgui");
		DS_LOG_INFO("CreateFromSpecification: init ImGui");
		if (!initializeImGui())
		{
			DS_LOG_ERROR("CreateFromSpecification: ImGui init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: ImGui ready");

		setCrashStage("setup layers");
		DS_LOG_INFO("CreateFromSpecification: setup default layers");
		setupDefaultLayers();
		DS_LOG_INFO("CreateFromSpecification: default layers ready");

		setCrashStage("initialize core runtime services");
		DS_LOG_INFO("CreateFromSpecification: init core runtime services");
		if (!initializeCoreLayerSystems())
		{
			DS_LOG_ERROR("CreateFromSpecification: core runtime services init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: core runtime services ready");

		m_Runtime.lifecycle.SetRunning(true);
		setCrashStage("running");
		DS_LOG_INFO("CreateFromSpecification: success, runtime running");
		return true;
	}

	bool Application::initializeEventDispatchingSystem()
	{
		DS_LOG_INFO("Init: EventDispatchingSystem (EventQueue)");
		m_EventQueue.Configure(m_Config.eventQueue.initialCapacity, m_Config.eventQueue.growthStep);
		return true;
	}

	void Application::shutdownInternal()
	{
		if (!m_Runtime.lifecycle.TryBeginShutdown())
			return;

		DS_LOG_INFO("Shutdown: persisting UI settings");
		persistUiSettings();

		DS_LOG_INFO("Shutdown: clearing layers");
		m_LayerStack.Clear();

		if (m_EventBus)
		{
			DS_LOG_INFO("Shutdown: releasing EventBus");
			m_EventBus->ClearQueue();
			m_EventBus->ClearAllListeners();
			m_EventBus.reset();
		}
		
		DS_LOG_INFO("Shutdown: releasing ImGui");
		shutdownImGui();
		
		DS_LOG_INFO("Shutdown: destroying window");
		shutdownWindow();
		
		DS_LOG_INFO("Shutdown: terminating GLFW");
		shutdownGlfw();

		DS_LOG_INFO("Shutdown: closing logger");
		shutdownLogger();
	}

	// ===== High-level runtime flow =====

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

	ApplicationSpecification defaultApplicationSpecification()
	{
		ApplicationSpecification specification;

#if defined(DS_DEBUG)
		specification.logLevel = DefectStudio::LogLevel::Trace;
#else
		specification.logLevel = DefectStudio::LogLevel::Info;
#endif

		specification.logToFile = true;
		specification.logFilePath = Path::FromResolved(RuntimeDefaults::LogFilePath);
		return specification;
	}

	ApplicationSpecification parseApplicationArguments(int argc, char **argv)
	{
		ApplicationSpecification specification = defaultApplicationSpecification();

		if (argv == nullptr)
			return specification;

		for (int index = 1; index < argc; ++index)
		{
			const std::string_view argument = argv[index];
			if (argument == "--help" || argument == "-h")
			{
				std::cout << "Usage: DefectStudio [--reset-layout] [--trace-events]\n";
				std::exit(0);
			}

			if (argument == "--reset-layout")
				specification.resetLayout = true;

			if (argument == "--trace-events")
				specification.traceEvents = true;
		}

		return specification;
	}

	// ===== Low-level frame and UI helpers =====

	void Application::beginImGuiFrame()
	{
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID, ImGui::GetMainViewport(),
		                             ImGuiDockNodeFlags_PassthruCentralNode);
	}

	void Application::drawMainPanel(bool &showDemoWindow, ImVec4 &clearColor, float frameRate)
	{
		ImGui::Begin("DefectStudio");

		ImGui::Text("GUI shell is running.");
		ImGui::Text("FPS: %.1f", frameRate);
		ImGui::Checkbox("Show ImGui Demo", &showDemoWindow);
		ImGui::ColorEdit3("Clear color", reinterpret_cast<float *>(&clearColor));
		ImGui::End();

		if (showDemoWindow)
			ImGui::ShowDemoWindow(&showDemoWindow);
	}

	void Application::renderFrame(const ImVec4 &clearColor, float frameRate)
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");
		(void)frameRate;

		ImGui::Render();

		int displayWidth = 0;
		int displayHeight = 0;
		m_Graphics.window->GetFramebufferSize(displayWidth, displayHeight);
		glViewport(0, 0, displayWidth, displayHeight);
		glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		TracyPlot("FPS", frameRate);

		m_Graphics.window->SwapBuffers();
		FrameMark;
	}

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

	// ===== Runtime services and configuration =====

	void Application::setupDefaultLayers()
	{
		DS_LOG_INFO("LayerStack setup: begin");
		m_LayerStack.PushLayer(CreateUnique<CoreLayer>());
		m_LayerStack.PushLayer(CreateUnique<IOLayer>());
		m_LayerStack.PushLayer(CreateUnique<StorageLayer>());
		m_LayerStack.PushLayer(CreateUnique<ScientificRuntimeLayer>());
		m_LayerStack.PushLayer(CreateUnique<DomainLayer>());
		m_LayerStack.PushLayer(CreateUnique<ImGuiLayer>());
		m_LayerStack.PushLayer(CreateUnique<EditorLayer>());
		m_LayerStack.PushLayer(CreateUnique<Demo::DemoLayer>());
		m_LayerStack.PushOverlay(CreateUnique<DebugLayer>());
		DS_LOG_INFO("LayerStack setup: complete");
	}

	void Application::initializeLogger() const
	{
		LoggerOptions loggerOptions;
		loggerOptions.level = m_Runtime.specification.logLevel;
		loggerOptions.logToFile = m_Runtime.specification.logToFile;
		loggerOptions.logFilePath = m_Runtime.specification.logFilePath.Native();
		Logger::Initialize(loggerOptions);
	}

	void Application::shutdownLogger() const
	{
		Logger::Shutdown();
	}

	void Application::logStartupSpecification() const
	{
		DS_LOG_INFO("Starting DefectStudio GUI shell");
		DS_LOG_INFO("Log level: {}", ToString(m_Runtime.specification.logLevel));

		if (m_Runtime.specification.logToFile)
		{
			if (m_Runtime.specification.logFilePath.Empty())
				DS_LOG_INFO("File logging enabled: {}", RuntimeDefaults::LogFilePath);
			else
				DS_LOG_INFO("File logging enabled: {}", m_Runtime.specification.logFilePath.String());
		}

		if (m_Runtime.specification.resetLayout)
			DS_LOG_WARN("Layout reset requested");

		if (m_Runtime.specification.traceEvents)
			DS_LOG_INFO("Event tracing enabled");
	}

	bool Application::initializeCoreLayerSystems()
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		auto editorLayer = m_LayerStack.FindLayerAs<EditorLayer>(LayerId::Editor).lock();
		auto imGuiLayer = m_LayerStack.FindLayerAs<ImGuiLayer>(LayerId::ImGui).lock();
		DS_ASSERT(coreLayer != nullptr, "CoreLayer was not created");
		DS_ASSERT(m_EventBus != nullptr, "EventBus was not created");
		DS_LOG_INFO("Init: Core runtime services via CoreLayer");
		bool systemInitialized = coreLayer->InitializeSystems(CreateWeakRef(m_EventBus));
		if (!systemInitialized)
		{
			DS_LOG_ERROR("Init: Core runtime services failed");
			return false;
		}

		// Sanity-check: force asserts if any core service failed to initialize.
		GetEventBus();
		GetJobSystem();
		GetProgressTracker();

		const std::size_t urgentWorkerCount = static_cast<std::size_t>(m_Config.jobs.reserveUrgentWorker ? 1 : 0);
		const std::size_t targetThreadCount = static_cast<std::size_t>(m_Config.jobs.defaultWorkerThreadCount) + urgentWorkerCount;
		if (!GetJobSystem().SetThreadCount(targetThreadCount))
		{
			DS_LOG_WARN("Init: failed to apply startup worker count {}", targetThreadCount);
		}
		else
		{
			DS_LOG_INFO(
				"Init: worker count target={} (base={} reserve_urgent={})",
				targetThreadCount,
				m_Config.jobs.defaultWorkerThreadCount,
				m_Config.jobs.reserveUrgentWorker);
		}

		if (editorLayer != nullptr)
		{
			editorLayer->BindRuntimeServices(
				coreLayer->GetJobSystemHandle(),
				coreLayer->GetProgressTrackerHandle());

			m_EditorUiState = editorLayer->GetUiStateHandle();
			if (auto uiState = m_EditorUiState.lock())
			{
				uiState->fontScale = m_Config.ui.fontScale;
				uiState->fontScaleStep = m_Config.ui.fontScaleStep;
				uiState->fontScaleMin = m_Config.ui.fontScaleMin;
				uiState->fontScaleMax = m_Config.ui.fontScaleMax;
				uiState->fontScaleStepMin = m_Config.ui.fontScaleStepMin;
				uiState->fontScaleStepMax = m_Config.ui.fontScaleStepMax;
				uiState->fontScaleStepSliderMax = m_Config.ui.fontScaleStepSliderMax;
				uiState->defaultWorkerThreadCount = m_Config.jobs.defaultWorkerThreadCount;
				uiState->reserveUrgentWorkerByDefault = m_Config.jobs.reserveUrgentWorker;
				uiState->selectedFontPath = m_Config.ui.fontPath;
			}

			if (imGuiLayer != nullptr)
				imGuiLayer->BindUiState(m_EditorUiState);
		}
		DS_LOG_INFO("Init: Core runtime services ready");
		return true;
	}

	bool Application::bootstrapConfiguration()
	{
		m_ConfigManager = CreateUnique<ConfigManager>(m_Config.directory);
		std::string configError;
		if (!m_ConfigManager->Initialize(configError))
		{
			DS_LOG_ERROR("Config bootstrap failed [{}]: {}", m_Config.directory.String(), configError);
			m_ConfigManager.reset();
			return false;
		}

		m_Config = m_ConfigManager->GetConfig();
		DS_LOG_INFO("Config bootstrap complete: {}", m_ConfigManager->GetConfigDirectory().String());
		return true;
	}

	void Application::applySpecificationFromDefaultConfig()
	{
		DS_ASSERT(m_ConfigManager != nullptr, "ConfigManager is not initialized");
		m_ConfigManager->ApplySpecification(m_Runtime.specification);

		DS_LOG_INFO(
			"Runtime tuning loaded: font_scale=[{}, {}] step=[{}, {}] step_slider_max={} workers_default={} reserve_urgent={} event_queue={{capacity:{}, growth:{}}}",
			m_Config.ui.fontScaleMin,
			m_Config.ui.fontScaleMax,
			m_Config.ui.fontScaleStepMin,
			m_Config.ui.fontScaleStepMax,
			m_Config.ui.fontScaleStepSliderMax,
			m_Config.jobs.defaultWorkerThreadCount,
			m_Config.jobs.reserveUrgentWorker,
			m_Config.eventQueue.initialCapacity,
			m_Config.eventQueue.growthStep);
	}

	void Application::applyUiSettingsFromConfig()
	{
		DS_ASSERT(m_ConfigManager != nullptr, "ConfigManager is not initialized");
		m_Config.ui.fontScale = std::clamp(m_Config.ui.fontScale, m_Config.ui.fontScaleMin, m_Config.ui.fontScaleMax);
		m_Config.ui.fontScaleStep = std::clamp(m_Config.ui.fontScaleStep, m_Config.ui.fontScaleStepMin, m_Config.ui.fontScaleStepMax);

		if (ImGui::GetCurrentContext() != nullptr)
			ImGui::GetIO().FontGlobalScale = m_Config.ui.fontScale;

		DS_LOG_INFO("UI settings loaded: font_scale={}, font_scale_step={}, font_path={}",
		            m_Config.ui.fontScale,
		            m_Config.ui.fontScaleStep,
		            m_Config.ui.fontPath.empty() ? "<default>" : m_Config.ui.fontPath);
	}

	bool Application::persistUiSettings()
	{
		if (m_ConfigManager == nullptr)
			return false;

		if (auto uiState = m_EditorUiState.lock())
		{
			m_Config.ui.fontScale = std::clamp(uiState->fontScale, m_Config.ui.fontScaleMin, m_Config.ui.fontScaleMax);
			m_Config.ui.fontScaleStep = std::clamp(uiState->fontScaleStep, m_Config.ui.fontScaleStepMin, m_Config.ui.fontScaleStepMax);
			m_Config.ui.fontPath = uiState->selectedFontPath;
		}

		m_ConfigManager->SetConfig(m_Config);

		std::string saveError;
		if (!m_ConfigManager->SaveUserSettings(saveError))
		{
			DS_LOG_WARN("UI settings save failed: {}", saveError);
			return false;
		}

		return true;
	}

	// ===== Low-level platform/graphics setup =====

	bool Application::initializeGlfw()
	{
		if (!glfwInit())
		{
			DS_LOG_ERROR("Failed to initialize GLFW");
			return false;
		}

		m_Graphics.glfwInitialized = true;
		DS_LOG_INFO("GLFW initialized");
		return true;
	}

	bool Application::createMainWindow()
	{
		DS_ASSERT(m_ConfigManager != nullptr, "ConfigManager is not initialized");

		m_Graphics.window = CreateUnique<Window>();
		if (!m_Graphics.window->Create(m_Config.window.width, m_Config.window.height, m_Config.window.title))
		{
			m_Graphics.window.reset();
			return false;
		}

		m_Graphics.window->SetEventCallback([this](EventVariant event) { queueEvent(std::move(event)); });

		configureInputBackend();

		return true;
	}

	bool Application::initializeGraphics()
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");

		const int glVersion = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
		if (glVersion == 0)
		{
			DS_LOG_ERROR("Failed to initialize GLAD");
			return false;
		}

		m_Graphics.gladInitialized = true;
		DS_LOG_INFO("OpenGL loaded: {}.{}", GLAD_VERSION_MAJOR(glVersion), GLAD_VERSION_MINOR(glVersion));
		return true;
	}

	bool Application::initializeImGui()
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigWindowsResizeFromEdges = true;

		const Path iniPath = m_Config.layout.imGuiIniPath.empty()
			? ConfigManager::GetLayoutPath(m_Config.directory)
			: Path::FromResolved(m_Config.layout.imGuiIniPath);
		FileSystem::CreateDirectories(iniPath.parent_path().Native());
		if (m_Runtime.specification.resetLayout && FileSystem::Exists(iniPath.Native()))
			FileSystem::Remove(iniPath.Native());

		m_Config.layout.imGuiIniPath = iniPath.String();
		io.IniFilename = m_Config.layout.imGuiIniPath.c_str();
		if (FileSystem::Exists(iniPath.Native()))
			ImGui::LoadIniSettingsFromDisk(m_Config.layout.imGuiIniPath.c_str());

		applyUiSettingsFromConfig();
		io.FontGlobalScale = m_Config.ui.fontScale;

		ImGui::StyleColorsDark();
		DS_LOG_INFO("ImGui context created and docking enabled");

		ImGui_ImplGlfw_InitForOpenGL(m_Graphics.window->GetNativeHandle(), true);
		ImGui_ImplOpenGL3_Init("#version 330");
		m_Graphics.imGuiInitialized = true;
		DS_LOG_INFO("ImGui backends initialized");
		return true;
	}

	// ===== Main loop and event queue internals =====

	void Application::mainLoop()
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");

		ImGuiIO &io = ImGui::GetIO();
		MainLoopState state;
		state.clearColor = ImVec4(
			m_Config.ui.clearColor[0],
			m_Config.ui.clearColor[1],
			m_Config.ui.clearColor[2],
			m_Config.ui.clearColor[3]);
		m_Runtime.lastFrameTime = glfwGetTime();
		DS_LOG_INFO("Entering render loop");
		TracyMessageL("Entering render loop");

		while (m_Runtime.Running() && !m_Graphics.window->ShouldClose())
			runMainLoopFrame(state.showDemoWindow, state.clearColor, io);
	}

	template <typename TEvent>
	void Application::OnEvent(TEvent &event)
	{
		if (m_Runtime.specification.traceEvents)
			DS_LOG_DEBUG("Event: {}", event.GetName());

		HandleLifecycleEvent(event, m_Runtime.lifecycle);

		if (event.handled)
			return;

		dispatchEventToLayers(event);
	}

	void Application::runMainLoopFrame(bool &showDemoWindow, ImVec4 &clearColor, ImGuiIO &io)
	{
		const double now = glfwGetTime();
		const float deltaTime = static_cast<float>(now - m_Runtime.lastFrameTime);
		m_Runtime.lastFrameTime = now;


		onUpdate(deltaTime);

		beginImGuiFrame();
		for (const auto &layer : m_LayerStack)
			layer->OnImGuiRender();
		drawMainPanel(showDemoWindow, clearColor, io.Framerate);
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

	struct VariantEventVisitor
	{
		Application *self;

		template <typename TEvent>
		void operator()(TEvent &event) const
		{
			self->OnEvent(event);
		}
	};

	void Application::processPendingEvents()
	{
		std::vector<EventVariant> events = m_EventQueue.Drain();
		if (events.empty())
			return;

		for (auto &variant : events)
		{
			std::visit(VariantEventVisitor{this}, variant);
		}
	}

	void Application::onRender(const ImVec4 &clearColor, float frameRate)
	{
		renderFrame(clearColor, frameRate);
	}

	void Application::shutdownImGui()
	{
		if (!m_Graphics.imGuiInitialized)
			return;

		ImGuiIO &io = ImGui::GetIO();
		if (io.IniFilename != nullptr)
			ImGui::SaveIniSettingsToDisk(io.IniFilename);

		persistUiSettings();

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		m_Graphics.imGuiInitialized = false;
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
