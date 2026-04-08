#pragma once

#include <filesystem>

#include "App/Window.hpp"
#include "Core/Logger.hpp"
#include "Core/Memory.hpp"

namespace DefectStudio
{
	struct ApplicationSpecification
	{
		LogLevel logLevel = LogLevel::Info;
		bool logToFile = true;
		std::filesystem::path logFilePath;
		bool resetLayout = false;
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
		void initializeLogger() const;
		void shutdownLogger() const;
		void logStartupSpecification() const;
		bool initializeGlfw();
		bool createMainWindow();
		bool initializeGraphics();
		bool initializeImGui();
		void mainLoop();
		void shutdownImGui();
		void shutdownWindow();
		void shutdownGlfw();

	private:
		int m_Argc = 0;
		char **m_Argv = nullptr;
		ApplicationSpecification m_Specification;
		Unique<Window> m_Window;
		bool m_GlfwInitialized = false;
		bool m_ImGuiInitialized = false;
		bool m_GladInitialized = false;
		bool m_Running = false;
	};
} // namespace DefectStudio
