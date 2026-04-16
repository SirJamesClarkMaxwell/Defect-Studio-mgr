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
		return std::filesystem::exists(configPath.Join("ui_settings.yaml").Native())
			|| std::filesystem::exists(configPath.Join("default.yaml").Native());
	}

	static Path FindConfigDirectoryInAncestors(Path start)
	{
		std::filesystem::path current = start.Native();
		for (int i = 0; i < 10; ++i)
		{
			if (HasConfigFiles(Path::FromResolved(current)))
				return Path::FromResolved(current / "config");

			const std::filesystem::path parent = current.parent_path();
			if (parent.empty() || parent == current)
				break;

			current = parent;
		}

		return Path{};
	}

		static Path ResolveConfigDirectory(char **argv)
	{
			const Path fromCwd = FindConfigDirectoryInAncestors(Path::FromResolved(std::filesystem::current_path()));
			if (!fromCwd.Empty())
			return fromCwd;

		if (argv != nullptr && argv[0] != nullptr)
		{
			std::error_code absoluteError;
			const std::filesystem::path executablePath = std::filesystem::absolute(argv[0], absoluteError);
			if (!absoluteError && !executablePath.empty())
			{
					const Path fromExe = FindConfigDirectoryInAncestors(Path::FromResolved(executablePath.parent_path()));
					if (!fromExe.Empty())
					return fromExe;
			}
		}

			return Path::FromResolved(std::filesystem::current_path() / "config");
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
		DS_ASSERT(m_CoreLayer != nullptr, "CoreLayer not initialized");
		return m_CoreLayer->GetEventBus();
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

	// ===== Instance lifecycle =====

	Application::Application(int argc, char **argv)
	{
		if (s_Instance != nullptr)
		{
			DS_LOG_WARN("Application::Create called more than once; replacing previous instance pointer");
		}

		s_Instance = this;
		m_Runtime.argc = argc;
		m_Runtime.argv = argv;
		
		ApplicationSpecification spec = parseApplicationArguments(m_Runtime.argc,m_Runtime.argv);
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

		if (!m_Runtime.lifecycle.TryMarkCreated())
		{
			DS_LOG_WARN("Application::Create called more than once; call ignored");
			return false;
		}

		s_Instance = this;
		TracyMessageL("Launching GUI shell");

		m_Runtime.specification = specification;
		m_Config.directory = ResolveConfigDirectory(m_Runtime.argv);

		if (!initializeEventDispatchingSystem())
		{
			shutdownInternal();
			return false;
		}

		initializeLogger();

		DS_LOG_INFO("Config directory: {}", m_Config.directory.String());
		logStartupSpecification();

		if (!initializeGlfw())
		{
			shutdownInternal();
			return false;
		}

		if (!createMainWindow())
		{
			shutdownInternal();
			return false;
		}

		if (!initializeGraphics())
		{
			shutdownInternal();
			return false;
		}

		if (!initializeImGui())
		{
			shutdownInternal();
			return false;
		}

		setupDefaultLayers();

		if (!initializeCoreLayerSystems())
		{
			shutdownInternal();
			return false;
		}

		m_Runtime.lifecycle.SetRunning(true);
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
			m_Config.fontScale = std::clamp(m_Config.fontScale, 0.70f, 2.00f);
			ImGui::GetIO().FontGlobalScale = m_Config.fontScale;
			saveUiSettings = true;
		}
		ImGui::End();

		if (saveUiSettings)
		{
			ConfigDocument uiDocument = ConfigManager::CreateUiSettingsDocument();
			loadUiSettingsDocument(uiDocument);
			uiDocument.Set("font_scale", std::to_string(m_Config.fontScale));
			saveUiSettingsDocument(uiDocument);
		}

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
		bool systemInitialized = m_CoreLayer->InitializeSystems();
		if (!systemInitialized)
			return false;

		// Sanity-check: force asserts if any core service failed to initialize.
		GetEventBus();
		GetJobSystem();
		GetProgressTracker();
		return true;
	}

	ConfigLoadResult Application::loadConfigFromPath(const Path &path) const
	{
		DS_LOG_INFO("Config load: {}", path.String());
		auto result = ConfigManager::LoadFile(path.Native());
		if (!result.success)
			DS_LOG_WARN("Config load failed [{}]: {}", path.String(), result.error);
		return result;
	}

	bool Application::saveConfigToPath(const Path &path, const ConfigDocument &document) const
	{
		DS_LOG_INFO("Config save: {}", path.String());
		std::string saveError;
		if (!ConfigManager::SaveFile(path.Native(), document, saveError))
		{
			DS_LOG_WARN("Config save failed [{}]: {}", path.String(), saveError);
			return false;
		}

		return true;
	}

	bool Application::loadUiSettingsDocument(ConfigDocument &document) const
	{
		const Path uiSettingsPath = Path::FromResolved(ConfigManager::GetUiSettingsPath(m_Config.directory.Native()));
		if (!std::filesystem::exists(uiSettingsPath.Native()))
		{
			DS_LOG_INFO("UI config missing, using defaults: {}", uiSettingsPath.String());
			return false;
		}

		auto loadResult = loadConfigFromPath(uiSettingsPath);
		if (!loadResult.success)
			return false;

		document = std::move(loadResult.document);
		return true;
	}

	bool Application::saveUiSettingsDocument(const ConfigDocument &document) const
	{
		const Path uiSettingsPath = Path::FromResolved(ConfigManager::GetUiSettingsPath(m_Config.directory.Native()));
		return saveConfigToPath(uiSettingsPath, document);
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
		m_Graphics.window = CreateUnique<Window>();
		if (!m_Graphics.window->Create(1280, 720, "DefectStudio"))
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
			if (!std::filesystem::exists(fontPath))
				continue;

			if (ImFont *font = io.Fonts->AddFontFromFileTTF(fontPath, 16.0f); font != nullptr)
			{
				io.FontDefault = font;
				DS_LOG_INFO("Loaded UI font: {}", fontPath);
				break;
			}
		}

		const std::filesystem::path iniPath = m_Config.directory.Join("imgui.ini").Native();
		std::filesystem::create_directories(iniPath.parent_path());
		if (m_Runtime.specification.resetLayout && std::filesystem::exists(iniPath))
			std::filesystem::remove(iniPath);

		m_Config.imGuiIniPath = iniPath.string();
		io.IniFilename = m_Config.imGuiIniPath.c_str();
		if (std::filesystem::exists(iniPath))
			ImGui::LoadIniSettingsFromDisk(m_Config.imGuiIniPath.c_str());

		ConfigDocument uiDocument = ConfigManager::CreateUiSettingsDocument();
		loadUiSettingsDocument(uiDocument);
		m_Config.fontScale = static_cast<float>(ConfigManager::GetDouble(uiDocument, "font_scale", 1.0));
		m_Config.fontScale = std::clamp(m_Config.fontScale, 0.70f, 2.00f);
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

		ConfigDocument uiDocument = ConfigManager::CreateUiSettingsDocument();
		loadUiSettingsDocument(uiDocument);
		uiDocument.Set("font_scale", std::to_string(m_Config.fontScale));
		saveUiSettingsDocument(uiDocument);

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
