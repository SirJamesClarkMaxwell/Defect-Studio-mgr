#pragma once

#include <filesystem>

#include "App/Window.hpp"
#include "Core/Event.hpp"
#include "Core/LayerStack.hpp"
#include "Core/Logger.hpp"
#include "Core/Memory.hpp"

struct ImVec4;

namespace DefectStudio
{
	struct ApplicationSpecification
	{
		LogLevel logLevel = LogLevel::Info;
		bool logToFile = true;
		std::filesystem::path logFilePath;
		bool resetLayout = false;
		bool traceEvents = false;
	};

	class Application
	{
	public:
		Application(int argc, char **argv);
		~Application();
		Application(const Application &) = delete;
		Application &operator=(const Application &) = delete;
		Application(Application &&) = delete;
		Application &operator=(Application &&) = delete;

		bool Create(const ApplicationSpecification &specification);
		int Run();
		void Shutdown();

	private:
		void onEvent(Event &event);
		void onUpdate(float deltaTime);
		void onRender(const ImVec4 &clearColor, float frameRate);
		void initializeLogger() const;
		void shutdownLogger() const;
		void logStartupSpecification() const;
		bool initializeGlfw();
		bool createMainWindow();
		bool initializeGraphics();
		bool initializeImGui();
		void beginImGuiFrame();
		void drawMainPanel(bool &showDemoWindow, ImVec4 &clearColor, float frameRate);
		void renderFrame(const ImVec4 &clearColor, float frameRate);
		void configureInputBackend();
		void mainLoop();
		void shutdownImGui();
		void shutdownWindow();
		void shutdownGlfw();

	private:
		int m_Argc = 0;
		char **m_Argv = nullptr;
		ApplicationSpecification m_Specification;
		Unique<Window> m_Window;
		LayerStack m_LayerStack;
		bool m_GlfwInitialized = false;
		bool m_ImGuiInitialized = false;
		bool m_GladInitialized = false;
		bool m_Running = false;
		double m_LastFrameTime = 0.0;
	};
} // namespace DefectStudio
