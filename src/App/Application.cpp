#include "Core/dspch.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

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
#include "Debug/DebugLayer.hpp"
#include "Demo/DemoLayer.hpp"
#include "Domain/DomainLayer.hpp"
#include "IO/IOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/ImGuiLayer.hpp"
#include "ScientificRuntime/ScientificRuntimeLayer.hpp"
#include "Storage/StorageLayer.hpp"

namespace DefectStudio
{
	Application *Application::s_Instance = nullptr;

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

	static std::string TrimCopy(std::string_view value)
	{
		const auto first = value.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos)
			return {};

		const auto last = value.find_last_not_of(" \t\r\n");
		return std::string(value.substr(first, last - first + 1));
	}

	static std::string ToLowerCopy(std::string_view value)
	{
		std::string result(value);
		for (char &character : result)
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		return result;
	}

	static LogLevel ParseLogLevel(std::string_view value, LogLevel fallback)
	{
		const std::string normalized = ToLowerCopy(TrimCopy(value));
		if (normalized == "trace")
			return LogLevel::Trace;
		if (normalized == "debug")
			return LogLevel::Debug;
		if (normalized == "info")
			return LogLevel::Info;
		if (normalized == "warn" || normalized == "warning")
			return LogLevel::Warn;
		if (normalized == "error")
			return LogLevel::Error;
		if (normalized == "critical")
			return LogLevel::Critical;
		return fallback;
	}

	struct MainLoopState
	{
		bool showDemoWindow = true;
		bool showSettingsWindow = false;
		ImVec4 clearColor = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
		int workerThreadCount = 5;
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

		mainLoop();
		shutdownInternal();
		return 0;
	}

	void Application::Shutdown()
	{
		shutdownInternal();
	}

	void Application::EmitEvent(EventVariant event)
	{
		DS_ASSERT(s_Instance != nullptr, "Application not created");
		s_Instance->queueEvent(std::move(event));
	}

	void Application::ProcessQueuedEvents()
	{
		DS_ASSERT(s_Instance != nullptr, "Application not created");
		s_Instance->processPendingEvents();
	}

	Application &Application::Get()
	{
		DS_ASSERT(s_Instance != nullptr, "Application not created");
		return *s_Instance;
	}

	EventBus &Application::GetEventBus()
	{
		DS_ASSERT(m_EventBus != nullptr, "EventBus not initialized");
		return *m_EventBus;
	}

	JobSystem &Application::GetJobSystem()
	{
		DS_ASSERT(m_CoreLayer != nullptr, "CoreLayer not initialized");
		return m_CoreLayer->GetJobSystem();
	}

	ProgressTracker &Application::GetProgressTracker()
	{
		DS_ASSERT(m_CoreLayer != nullptr, "CoreLayer not initialized");
		return m_CoreLayer->GetProgressTracker();
	}

	float Application::GetFontScale() const
	{
		return m_Config.fontScale;
	}

	float Application::GetFontScaleStep() const
	{
		return m_Config.fontScaleStep;
	}

	void Application::SetFontScale(float fontScale)
	{
		m_Config.fontScale = std::clamp(fontScale, 0.70f, 2.00f);
		if (ImGui::GetCurrentContext() != nullptr)
			ImGui::GetIO().FontGlobalScale = m_Config.fontScale;
	}

	void Application::SetFontScaleStep(float fontScaleStep)
	{
		m_Config.fontScaleStep = std::clamp(fontScaleStep, 0.01f, 1.00f);
	}

	void Application::AdjustFontScale(float delta)
	{
		SetFontScale(m_Config.fontScale + delta);
	}

	// ===== Instance lifecycle =====

	Application::Application(int argc, char **argv)
	{
		if (s_Instance != nullptr)
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
		if (s_Instance == this)
			s_Instance = nullptr;
	}

	bool Application::createFromSpecification(const ApplicationSpecification &specification)
	{
		ZoneScoped;
		DS_LOG_INFO("CreateFromSpecification: begin");

		if (!m_Runtime.lifecycle.TryMarkCreated())
		{
			DS_LOG_WARN("Application::Create called more than once; call ignored");
			return false;
		}

		s_Instance = this;
		TracyMessageL("Launching GUI shell");

		m_Runtime.specification = specification;
		m_Config.directory = ResolveConfigDirectory(m_Runtime.argv);
		DS_LOG_INFO("CreateFromSpecification: resolved config directory={}", m_Config.directory.String());
		if (!bootstrapConfiguration())
		{
			DS_LOG_ERROR("CreateFromSpecification: configuration bootstrap failed");
			shutdownInternal();
			return false;
		}
		applySpecificationFromDefaultConfig();

		DS_LOG_INFO("CreateFromSpecification: init EventDispatchingSystem");
		if (!initializeEventDispatchingSystem())
		{
			DS_LOG_ERROR("CreateFromSpecification: EventDispatchingSystem init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: EventDispatchingSystem ready");

		DS_LOG_INFO("CreateFromSpecification: create EventBus");
		m_EventBus = CreateRef<EventBus>();
		if (!m_EventBus)
		{
			DS_LOG_ERROR("Failed to initialize EventBus");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: EventBus ready");

		DS_LOG_INFO("CreateFromSpecification: init logger");
		initializeLogger();
		DS_LOG_INFO("CreateFromSpecification: logger ready");

		DS_LOG_INFO("Config directory: {}", m_Config.directory.String());
		logStartupSpecification();

		DS_LOG_INFO("CreateFromSpecification: init GLFW");
		if (!initializeGlfw())
		{
			DS_LOG_ERROR("CreateFromSpecification: GLFW init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: GLFW ready");

		DS_LOG_INFO("CreateFromSpecification: create main window");
		if (!createMainWindow())
		{
			DS_LOG_ERROR("CreateFromSpecification: main window creation failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: main window ready");

		DS_LOG_INFO("CreateFromSpecification: init graphics");
		if (!initializeGraphics())
		{
			DS_LOG_ERROR("CreateFromSpecification: graphics init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: graphics ready");

		DS_LOG_INFO("CreateFromSpecification: init ImGui");
		if (!initializeImGui())
		{
			DS_LOG_ERROR("CreateFromSpecification: ImGui init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: ImGui ready");

		DS_LOG_INFO("CreateFromSpecification: setup default layers");
		setupDefaultLayers();
		DS_LOG_INFO("CreateFromSpecification: default layers ready");

		DS_LOG_INFO("CreateFromSpecification: init core runtime services");
		if (!initializeCoreLayerSystems())
		{
			DS_LOG_ERROR("CreateFromSpecification: core runtime services init failed");
			shutdownInternal();
			return false;
		}
		DS_LOG_INFO("CreateFromSpecification: core runtime services ready");

		m_Runtime.lifecycle.SetRunning(true);
		DS_LOG_INFO("CreateFromSpecification: success, runtime running");
		return true;
	}

	bool Application::initializeEventDispatchingSystem()
	{
		DS_LOG_INFO("Init: EventDispatchingSystem (EventQueue)");
		m_EventQueue.Configure(256);
		return true;
	}

	void Application::shutdownInternal()
	{
		if (!m_Runtime.lifecycle.TryBeginShutdown())
			return;

		DS_LOG_INFO("Shutdown: clearing layers");
		m_LayerStack.Clear();
		m_CoreLayer = nullptr;

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
		ImGui::GetIO().FontGlobalScale = m_Config.fontScale;
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID, ImGui::GetMainViewport(),
		                             ImGuiDockNodeFlags_PassthruCentralNode);
	}

	void Application::drawMainPanel(bool &showDemoWindow, ImVec4 &clearColor, float frameRate)
	{
		bool saveUiSettings = false;

		ImGui::Begin("DefectStudio");

		ImGui::Text("GUI shell is running.");
		ImGui::Text("FPS: %.1f", frameRate);
		ImGui::Checkbox("Show ImGui Demo", &showDemoWindow);
		ImGui::ColorEdit3("Clear color", reinterpret_cast<float *>(&clearColor));

		if (ImGui::SliderFloat("Font scale", &m_Config.fontScale, 0.70f, 2.00f, "%.2f"))
		{
			SetFontScale(m_Config.fontScale);
			saveUiSettings = true;
		}
		if (ImGui::SliderFloat("Font scale step", &m_Config.fontScaleStep, 0.01f, 0.50f, "%.2f"))
		{
			SetFontScaleStep(m_Config.fontScaleStep);
			saveUiSettings = true;
		}
		ImGui::End();

		if (saveUiSettings)
			persistUiSettings();

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
		auto coreLayer = CreateUnique<CoreLayer>();
		m_CoreLayer = coreLayer.get();
		m_LayerStack.PushLayer(std::move(coreLayer));
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
				DS_LOG_INFO("File logging enabled: logs/DefectStudio.log");
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
		DS_ASSERT(m_CoreLayer != nullptr, "CoreLayer was not created");
		DS_ASSERT(m_EventBus != nullptr, "EventBus was not created");
		DS_LOG_INFO("Init: Core runtime services via CoreLayer");
		bool systemInitialized = m_CoreLayer->InitializeSystems(CreateWeakRef(m_EventBus));
		if (!systemInitialized)
		{
			DS_LOG_ERROR("Init: Core runtime services failed");
			return false;
		}

		// Sanity-check: force asserts if any core service failed to initialize.
		GetEventBus();
		GetJobSystem();
		GetProgressTracker();
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

		DS_LOG_INFO("Config bootstrap complete: {}", m_ConfigManager->GetConfigDirectory().string());
		return true;
	}

	void Application::applySpecificationFromDefaultConfig()
	{
		DS_ASSERT(m_ConfigManager != nullptr, "ConfigManager is not initialized");
		m_Runtime.specification.logLevel = ParseLogLevel(
			m_ConfigManager->GetDefaultString("log.level", ToString(m_Runtime.specification.logLevel)),
			m_Runtime.specification.logLevel);
		m_Runtime.specification.traceEvents = m_ConfigManager->GetDefaultBool(
			"trace_events",
			m_Runtime.specification.traceEvents);
	}

	void Application::applyUiSettingsFromConfig()
	{
		DS_ASSERT(m_ConfigManager != nullptr, "ConfigManager is not initialized");
		m_Config.fontScale = static_cast<float>(m_ConfigManager->GetUiDouble("font_scale", 1.0));
		m_Config.fontScaleStep = static_cast<float>(m_ConfigManager->GetUiDouble("font_scale_step", 0.10));
		m_Config.fontScale = std::clamp(m_Config.fontScale, 0.70f, 2.00f);
		m_Config.fontScaleStep = std::clamp(m_Config.fontScaleStep, 0.01f, 1.00f);

		if (ImGui::GetCurrentContext() != nullptr)
			ImGui::GetIO().FontGlobalScale = m_Config.fontScale;
	}

	bool Application::persistUiSettings()
	{
		if (m_ConfigManager == nullptr)
			return false;

		m_ConfigManager->SetUiValue("font_scale", std::to_string(m_Config.fontScale));
		m_ConfigManager->SetUiValue("font_scale_step", std::to_string(m_Config.fontScaleStep));

		std::string saveError;
		if (!m_ConfigManager->SaveUiSettings(saveError))
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
		const int width = m_ConfigManager->GetDefaultInt("window.width", 1280);
		const int height = m_ConfigManager->GetDefaultInt("window.height", 720);
		const std::string title = m_ConfigManager->GetDefaultString("window.title", "DefectStudio");

		m_Graphics.window = CreateUnique<Window>();
		if (!m_Graphics.window->Create(width, height, title))
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

		const std::array<const char *, 3> preferredFonts = {
			// "C:/Windows/Fonts/CascadiaCode.ttf",
			// "C:/Windows/Fonts/CascadiaMono.ttf",
			"C:/Windows/Fonts/segoeui.ttf"
		};

		for (const char *fontPath : preferredFonts)
		{
			if (!FileSystem::Exists(fontPath))
				continue;

			if (ImFont *font = io.Fonts->AddFontFromFileTTF(fontPath, 16.0f); font != nullptr)
			{
				io.FontDefault = font;
				DS_LOG_INFO("Loaded UI font: {}", fontPath);
				break;
			}
		}

		const Path iniPath = Path::FromResolved(m_Config.directory.Join("imgui.ini").Native());
		FileSystem::CreateDirectories(iniPath.parent_path().Native());
		if (m_Runtime.specification.resetLayout && FileSystem::Exists(iniPath.Native()))
			FileSystem::Remove(iniPath.Native());

		m_Config.imGuiIniPath = iniPath.string();
		io.IniFilename = m_Config.imGuiIniPath.c_str();
		if (FileSystem::Exists(iniPath.Native()))
			ImGui::LoadIniSettingsFromDisk(m_Config.imGuiIniPath.c_str());

		applyUiSettingsFromConfig();
		io.FontGlobalScale = m_Config.fontScale;

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
