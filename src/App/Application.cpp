#include "Core/dspch.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>

#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#include <windows.h>
#endif

#include "App/Application.hpp"
#include "Core/Logger.hpp"

namespace DefectStudio
{
int Application::Run(const ApplicationOptions &options)
{
	ZoneScoped;
	TracyMessageL("Launching GUI shell");

	DS_LOG_INFO("Starting DefectStudio GUI shell");
	DS_LOG_INFO("Log level: {}", static_cast<int>(options.logLevel));
	if (options.logToFile)
	{
		DS_LOG_INFO("File logging enabled: {}", options.logFilePath.string());
	}
	if (!options.projectPath.empty())
	{
		DS_LOG_INFO("Project path: {}", options.projectPath.string());
	}
	if (options.safeMode)
	{
		DS_LOG_WARN("Safe mode enabled");
	}
	if (options.resetLayout)
	{
		DS_LOG_WARN("Layout reset requested");
	}

	if (!glfwInit())
	{
		DS_LOG_ERROR("Failed to initialize GLFW");
		return 1;
	}
	DS_LOG_INFO("GLFW initialized");

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow *window = glfwCreateWindow(1280, 720, "DefectStudio", nullptr, nullptr);
	if (window == nullptr)
	{
		DS_LOG_ERROR("Failed to create GLFW window");
		glfwTerminate();
		return 1;
	}
	DS_LOG_INFO("GLFW window created");

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

#if defined(_WIN32)
	HICON windowIcon = nullptr;
	const HWND hwnd = glfwGetWin32Window(window);
	BOOL useDark = TRUE;
	if (DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark)) != S_OK)
	{
		DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
	}

	const std::filesystem::path iconPath = std::filesystem::current_path() / "assets" / "icon.ico";
	windowIcon = static_cast<HICON>(
	    LoadImageW(nullptr, iconPath.wstring().c_str(), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_LOADFROMFILE));
	if (windowIcon != nullptr)
	{
		SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(windowIcon));
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(windowIcon));
		DS_LOG_INFO("Windows icon assigned");
	}
#endif

	const int glVersion = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
	if (glVersion == 0)
	{
		DS_LOG_ERROR("Failed to initialize GLAD");
		glfwDestroyWindow(window);
		glfwTerminate();
		return 1;
	}

	DS_LOG_INFO("OpenGL loaded: {}.{}", GLAD_VERSION_MAJOR(glVersion), GLAD_VERSION_MINOR(glVersion));

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImGui::StyleColorsDark();
	DS_LOG_INFO("ImGui context created and docking enabled");

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");
	DS_LOG_INFO("ImGui backends initialized");

	bool showDemoWindow = true;
	ImVec4 clearColor = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
	DS_LOG_INFO("Entering render loop");
	TracyMessageL("Entering render loop");

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID, ImGui::GetMainViewport(),
		                             ImGuiDockNodeFlags_PassthruCentralNode);

		ImGui::Begin("DefectStudio");
		ImGui::Text("GUI shell is running.");
		ImGui::Text("FPS: %.1f", io.Framerate);
		ImGui::Checkbox("Show ImGui Demo", &showDemoWindow);
		ImGui::ColorEdit3("Clear color", reinterpret_cast<float *>(&clearColor));
		ImGui::End();

		if (showDemoWindow)
		{
			ImGui::ShowDemoWindow(&showDemoWindow);
		}

		ImGui::Render();

		int displayWidth = 0;
		int displayHeight = 0;
		glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
		glViewport(0, 0, displayWidth, displayHeight);
		glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		TracyPlot("FPS", io.Framerate);

		glfwSwapBuffers(window);
		FrameMark;
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

#if defined(_WIN32)
	if (windowIcon != nullptr)
	{
		DestroyIcon(windowIcon);
	}
#endif

	DS_LOG_INFO("DefectStudio GUI shell stopped");

	return 0;
}
} // namespace DefectStudio
