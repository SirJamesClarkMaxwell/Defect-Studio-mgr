#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

#include "App/ApplicationLifecycle.hpp"
#include "App/Window.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"

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

	struct WindowConfig
	{
		int width = 1280;
		int height = 720;
		std::string title = "DefectStudio";
	};

	struct LogConfig
	{
		LogLevel level = LogLevel::Info;
		bool toFile = true;
		Path filePath;
		bool traceEvents = false;
	};

	struct UIConfig
	{
		float fontScale = 1.0f;
		float fontScaleStep = 0.1f;
		std::string fontPath;
		std::array<float, 4> clearColor = {0.10f, 0.10f, 0.12f, 1.00f};
		float fontScaleMin = 0.7f;
		float fontScaleMax = 10.0f;
		float fontScaleStepMin = 0.01f;
		float fontScaleStepMax = 1.0f;
		float fontScaleStepSliderMax = 0.5f;
	};

	struct JobsConfig
	{
		int defaultWorkerThreadCount = 1;
		bool reserveUrgentWorker = true;
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
		WindowConfig window;
		LogConfig log;
		UIConfig ui;
		JobsConfig jobs;
		EventQueueConfig eventQueue;
		LayoutConfig layout;
	};

} // namespace DefectStudio
