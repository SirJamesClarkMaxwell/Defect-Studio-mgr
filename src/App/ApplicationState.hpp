#pragma once

#include <filesystem>
#include <string>

#include "App/ApplicationLifecycle.hpp"
#include "App/Window.hpp"
#include "Core/Logger.hpp"
#include "Core/Memory.hpp"
#include "Core/Path.hpp"

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
		Unique<Window> window;
		bool glfwInitialized = false;
		bool imGuiInitialized = false;
		bool gladInitialized = false;
	};

	struct ApplicationConfigState
	{
		Path directory;
		std::string imGuiIniPath;
		float fontScale = 1.0f;
	};

} // namespace DefectStudio
