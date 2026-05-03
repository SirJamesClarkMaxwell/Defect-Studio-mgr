#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "App/ApplicationLifecycle.hpp"
#include "App/Window.hpp"
#include "Core/JobSystem/JobSystemConfig.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/UiConfig.hpp"

namespace DefectStudio
{
	struct ApplicationSpecification
	{
		LogLevel logLevel = LogLevel::Info;
		bool logToFile = true;
		Path logFilePath;
		bool resetLayout = false;
		bool traceEvents = false;
	};

	struct ApplicationRuntimeState
	{
		int argc = 0;
		char **argv = nullptr;
		ApplicationSpecification specification;
		ApplicationLifecycleState lifecycle;
		double lastFrameTime = 0.0;
		
		inline bool Running(){return lifecycle.IsRunning();}
		inline bool Created(){return lifecycle.IsCreated();}
	};

	struct ApplicationGraphicsState
	{
		Ref<Window> window;
		bool glfwInitialized = false;
		bool gladInitialized = false;
	};

	struct WindowConfig
	{
		int x = -1;
		int y = -1;
		int width = 1280;
		int height = 720;
		bool maximized = false;
		std::string title = "DefectStudio";
	};

	struct LogConfig
	{
		LogLevel level = LogLevel::Info;
		bool toFile = true;
		Path filePath;
		bool traceEvents = false;
	};

	struct EventQueueConfig
	{
		std::size_t initialCapacity = 32;
		std::size_t growthStep = 32;
	};

	struct LayoutConfig
	{
		std::string imGuiIniPath;
	};

	struct ApplicationConfig
	{
		Path directory;
		ApplicationPaths paths;
		WindowConfig window;
		LogConfig log;
		UIConfig ui;
		AppearanceConfig appearance;
		JobsConfig jobs;
		EventQueueConfig eventQueue;
		LayoutConfig layout;
	};

} // namespace DefectStudio
