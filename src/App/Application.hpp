#pragma once

#include <filesystem>

#include "App/ApplicationLifecycle.hpp"
#include "App/ApplicationState.hpp"
#include "App/ConfigManager.hpp"
#include "App/Window.hpp"
#include "Core/Event.hpp"
#include "Core/EventBus.hpp"
#include "Core/EventQueue.hpp"
#include "Core/JobSystem.hpp"
#include "Core/LayerStack.hpp"
#include "Core/Logger.hpp"
#include "Core/ProgressTracker.hpp"

struct ImVec4;
struct ImGuiIO;

namespace DefectStudio
{
	class CoreLayer;

	class Application
	{
	public:
		static Application Create(int argc, char **argv);
		int Run();
		void Shutdown();
		~Application();

		static void EmitEvent(EventVariant event);

		// Global accessor for layers and demos
		static Application &Get();

		// Runtime service accessors
		EventBus &GetEventBus();
		JobSystem &GetJobSystem();
		ProgressTracker &GetProgressTracker();

		// Event processing entry point for concrete event types.
		template <typename TEvent>
		void OnEvent(TEvent &event);

	private:
		Application(int argc, char **argv);
		Application(const Application &) = delete;
		Application &operator=(const Application &) = delete;
		Application(Application &&) = delete;
		Application &operator=(Application &&) = delete;
		
		// High-level lifecycle orchestration
		bool createFromSpecification(const ApplicationSpecification &specification);
		void shutdownInternal();
		
		// High-level runtime flow
		void onUpdate(float deltaTime);
		void onRender(const ImVec4 &clearColor, float frameRate);
		template <typename TEvent>
		void dispatchEventToLayers(TEvent &event);
		void mainLoop();
		void runMainLoopFrame(bool &showDemoWindow, ImVec4 &clearColor, ImGuiIO &io);
		void initializeLogger() const;
		void shutdownLogger() const;
		void logStartupSpecification() const;

		// Runtime services lifecycle
		bool initializeCoreLayerSystems();

		// Configuration API
		ConfigLoadResult loadConfigFromPath(const Path &path) const;
		bool saveConfigToPath(const Path &path, const ConfigDocument &document) const;
		bool loadUiSettingsDocument(ConfigDocument &document) const;
		bool saveUiSettingsDocument(const ConfigDocument &document) const;

		// Low-level platform/graphics setup
		void setupDefaultLayers();
		bool initializeGlfw();
		bool createMainWindow();
		bool initializeGraphics();
		bool initializeImGui();
		
		void shutdownImGui();
		void shutdownWindow();
		void shutdownGlfw();
		void configureInputBackend();

		// Low-level frame and UI helpers
		void beginImGuiFrame();
		void drawMainPanel(bool &showDemoWindow, ImVec4 &clearColor, float frameRate);
		void renderFrame(const ImVec4 &clearColor, float frameRate);

		// Event queue internals
		static void ProcessQueuedEvents();
		void queueEvent(EventVariant event);
		void processPendingEvents();

	private:
		ApplicationRuntimeState m_Runtime;
		ApplicationGraphicsState m_Graphics;
		ApplicationConfigState m_Config;
		EventQueue m_EventQueue;

		LayerStack m_LayerStack;
		CoreLayer *m_CoreLayer = nullptr;

		// Singleton ownership
		static Application *s_Instance;
	};
} // namespace DefectStudio
