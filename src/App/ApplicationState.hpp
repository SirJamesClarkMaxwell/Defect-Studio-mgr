#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

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

	struct RendererLightingConfig
	{
		std::array<float, 3> keyDirection = {0.6f, 0.8f, 0.5f};
		std::array<float, 3> fillDirection = {-0.7f, 0.3f, 0.2f};
		std::array<float, 3> backDirection = {0.0f, -0.4f, -0.8f};
		float ambientIntensity = 0.18f;
		float keyIntensity = 0.55f;
		float fillIntensity = 0.25f;
		float backIntensity = 0.12f;
		bool twoSided = true;
	};

	struct RendererViewportConfig
	{
		float axisButtonSize = 20.0f;
		float iconButtonSize = 18.0f;
	};

	struct RendererToolbarWheelConfig
	{
		float rotationStepDelta = 1.0f;
		float zoomStepDelta = 1.0f;
		std::vector<float> ctrlPresetValues = {0.0f, 1.0f, 3.0f, 5.0f, 10.0f, 15.0f, 30.0f, 45.0f, 60.0f, 90.0f, 180.0f};
	};

	struct RendererGridConfig
	{
		bool autoFitToStructureBounds = true;
		float paddingPercent = 20.0f;
		float spacing = 0.7f;
		float planeZ = 0.0f;
	};

	struct RendererShortcutConfig
	{
		std::string alignAxisA = "A";
		std::string alignAxisB = "B";
		std::string alignAxisC = "C";
		std::string orbitLeft = "Left";
		std::string orbitRight = "Right";
		std::string orbitUp = "Up";
		std::string orbitDown = "Down";
		std::string rollLeft = "Q";
		std::string rollRight = "E";
		std::string zoomIn = "R";
		std::string zoomOut = "F";
		std::string focusSelectedAtom = "Period";
	};

	struct RendererConfig
	{
		std::array<float, 4> backgroundColor = {0.06f, 0.07f, 0.08f, 1.0f};
		float orbitSensitivity = 1.0f;
		float panSensitivity = 1.0f;
		float zoomSensitivity = 1.0f;
		float rotationSpeed = 1.0f;
		float focusSelectedAtomDistance = 3.0f;
		float focusSelectedAtomTransitionSeconds = 0.18f;
		bool focusSelectedAtomRespectAtomRadius = true;
		float focusSelectedAtomRadiusMultiplier = 2.0f;
		bool invertZoom = false;
		bool touchpadNavigation = true;
		std::string defaultProjection = "perspective";
		RendererLightingConfig lighting;
		RendererViewportConfig viewport;
		RendererToolbarWheelConfig toolbarWheel;
		RendererGridConfig grid;
		RendererShortcutConfig shortcuts;
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
		RendererConfig renderer;
	};

} // namespace DefectStudio
