#include "Core/dspch.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

#include "App/Application.hpp"
#include "Core/Assert.hpp"
#include "Core/Input.hpp"
#include "Core/CoreLayer.hpp"
#include "Core/Logger.hpp"
#include "Debug/DebugLayer.hpp"
#include "Domain/DomainLayer.hpp"
#include "IO/IOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/ImGuiLayer.hpp"
#include "ScientificRuntime/ScientificRuntimeLayer.hpp"
#include "Storage/StorageLayer.hpp"

namespace DefectStudio
{
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
		DS_ASSERT(m_Window != nullptr, "Main window was not created");
		(void)frameRate;

		ImGui::Render();

		int displayWidth = 0;
		int displayHeight = 0;
		m_Window->GetFramebufferSize(displayWidth, displayHeight);
		glViewport(0, 0, displayWidth, displayHeight);
		glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		TracyPlot("FPS", frameRate);

		m_Window->SwapBuffers();
		FrameMark;
	}

	void Application::configureInputBackend()
	{
		DS_ASSERT(m_Window != nullptr, "Main window was not created");

		GLFWwindow *nativeHandle = m_Window->GetNativeHandle();
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

	Application::Application(int argc, char **argv) : m_Argc(argc), m_Argv(argv)
	{
		Create(parseApplicationArguments(m_Argc, m_Argv));
	}

	Application::~Application() = default;

	bool Application::Create(const ApplicationSpecification &specification)
	{
		ZoneScoped;
		TracyMessageL("Launching GUI shell");

		m_Specification = specification;
		initializeLogger();
		logStartupSpecification();

		if (!initializeGlfw())
		{
			Shutdown();
			return false;
		}

		if (!createMainWindow())
		{
			Shutdown();
			return false;
		}

		if (!initializeGraphics())
		{
			Shutdown();
			return false;
		}

		if (!initializeImGui())
		{
			Shutdown();
			return false;
		}

		setupDefaultLayers();

		m_Running = true;
		return true;
	}

	int Application::Run()
	{
		if (!m_Running)
			return 1;

		mainLoop();
		Shutdown();
		return 0;
	}

	void Application::Shutdown()
	{
		m_LayerStack.Clear();
		shutdownImGui();
		shutdownWindow();
		shutdownGlfw();
		shutdownLogger();
		m_Running = false;
	}

	void Application::setupDefaultLayers()
	{
		m_LayerStack.PushLayer(CreateUnique<CoreLayer>());
		m_LayerStack.PushLayer(CreateUnique<DomainLayer>());
		m_LayerStack.PushLayer(CreateUnique<IOLayer>());
		m_LayerStack.PushLayer(CreateUnique<ScientificRuntimeLayer>());
		m_LayerStack.PushLayer(CreateUnique<StorageLayer>());
		m_LayerStack.PushLayer(CreateUnique<EditorLayer>());
		m_LayerStack.PushOverlay(CreateUnique<DebugLayer>());
		m_LayerStack.PushOverlay(CreateUnique<ImGuiLayer>());
	}

	void Application::initializeLogger() const
	{
		LoggerOptions loggerOptions;
		loggerOptions.level = m_Specification.logLevel;
		loggerOptions.logToFile = m_Specification.logToFile;
		loggerOptions.logFilePath = m_Specification.logFilePath;
		Logger::Initialize(loggerOptions);
	}

	void Application::shutdownLogger() const
	{
		Logger::Shutdown();
	}

	void Application::logStartupSpecification() const
	{
		DS_LOG_INFO("Starting DefectStudio GUI shell");
		DS_LOG_INFO("Log level: {}", ToString(m_Specification.logLevel));

		if (m_Specification.logToFile)
		{
			if (m_Specification.logFilePath.empty())
				DS_LOG_INFO("File logging enabled: logs/DefectStudio.log");
			else
				DS_LOG_INFO("File logging enabled: {}", m_Specification.logFilePath.string());
		}

		if (m_Specification.resetLayout)
			DS_LOG_WARN("Layout reset requested");

		if (m_Specification.traceEvents)
			DS_LOG_INFO("Event tracing enabled");
	}

	bool Application::initializeGlfw()
	{
		if (!glfwInit())
		{
			DS_LOG_ERROR("Failed to initialize GLFW");
			return false;
		}

		m_GlfwInitialized = true;
		DS_LOG_INFO("GLFW initialized");
		return true;
	}

	bool Application::createMainWindow()
	{
		m_Window = CreateUnique<Window>();
		if (!m_Window->Create(1280, 720, "DefectStudio"))
		{
			m_Window.reset();
			return false;
		}

		m_Window->SetEventCallback([this](Event &event) { onEvent(event); });

		configureInputBackend();

		return true;
	}

	bool Application::initializeGraphics()
	{
		DS_ASSERT(m_Window != nullptr, "Main window was not created");

		const int glVersion = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
		if (glVersion == 0)
		{
			DS_LOG_ERROR("Failed to initialize GLAD");
			return false;
		}

		m_GladInitialized = true;
		DS_LOG_INFO("OpenGL loaded: {}.{}", GLAD_VERSION_MAJOR(glVersion), GLAD_VERSION_MINOR(glVersion));
		return true;
	}

	bool Application::initializeImGui()
	{
		DS_ASSERT(m_Window != nullptr, "Main window was not created");

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO &io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		ImGui::StyleColorsDark();
		DS_LOG_INFO("ImGui context created and docking enabled");

		ImGui_ImplGlfw_InitForOpenGL(m_Window->GetNativeHandle(), true);
		ImGui_ImplOpenGL3_Init("#version 330");
		m_ImGuiInitialized = true;
		DS_LOG_INFO("ImGui backends initialized");
		return true;
	}

	void Application::mainLoop()
	{
		DS_ASSERT(m_Window != nullptr, "Main window was not created");

		ImGuiIO &io = ImGui::GetIO();
		bool showDemoWindow = true;
		ImVec4 clearColor = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
		m_LastFrameTime = glfwGetTime();
		DS_LOG_INFO("Entering render loop");
		TracyMessageL("Entering render loop");

		while (m_Running && !m_Window->ShouldClose())
		{
			const double now = glfwGetTime();
			const float deltaTime = static_cast<float>(now - m_LastFrameTime);
			m_LastFrameTime = now;

			m_Window->PollEvents();
			onUpdate(deltaTime);

			beginImGuiFrame();
			for (const auto &layer : m_LayerStack)
				layer->OnImGuiRender();
			drawMainPanel(showDemoWindow, clearColor, io.Framerate);
			onRender(clearColor, io.Framerate);
		}
	}

	void Application::onEvent(Event &event)
	{
		if (m_Specification.traceEvents)
			DS_LOG_DEBUG("Event: {}", event.GetName());

		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent &)
		                                      {
			                                      m_Running = false;
			                                      return true;
		                                      });

		if (event.handled)
			return;

		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			(*it)->OnEvent(event);
			if (event.handled)
				break;
		}
	}

	void Application::onUpdate(float deltaTime)
	{
		for (const auto &layer : m_LayerStack)
			layer->OnUpdate(deltaTime);
	}

	void Application::onRender(const ImVec4 &clearColor, float frameRate)
	{
		renderFrame(clearColor, frameRate);
	}

	void Application::shutdownImGui()
	{
		if (!m_ImGuiInitialized)
			return;

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		m_ImGuiInitialized = false;
	}

	void Application::shutdownWindow()
	{
		if (m_Window == nullptr)
			return;

		Input::ResetBackend();
		m_Window->Destroy();
		m_Window.reset();
	}

	void Application::shutdownGlfw()
	{
		if (!m_GlfwInitialized)
			return;

		glfwTerminate();
		m_GlfwInitialized = false;
		m_GladInitialized = false;

		DS_LOG_INFO("DefectStudio GUI shell stopped");
	}
} // namespace DefectStudio
