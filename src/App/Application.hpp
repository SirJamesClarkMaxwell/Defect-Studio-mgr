#pragma once

#include <functional>
#include <optional>
#include <filesystem>

#include "App/ApplicationLifecycle.hpp"
#include "App/ApplicationState.hpp"
#include "App/Managers/ConfigManager.hpp"
#include "App/Window.hpp"
#include "Core/Capabilities/CapabilityRegistry.hpp"
#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/Assets/AssetManager.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEvent.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/EventQueue.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/LayerStack.hpp"
#include "Core/Notifications/Notifier.hpp"
#include "Core/Utils/Logger.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"

struct ImVec4;
struct ImGuiIO;

namespace DefectStudio
{
	class ApplicationEventController;
	class CoreLayer;

	class Application
	{
	public:
		// Create a fully initialized application instance (may fail internally; check Run()).
		static Application Create(int argc, char **argv);
		// Run the main loop until shutdown is requested; returns exit code.
		int Run();
		// Request shutdown; safe to call multiple times.
		void Shutdown();
		~Application();

		// Queue a platform event for main-thread processing.
		static void EmitEvent(EventVariant event);
		// Present a blocking error dialog on the main thread.
		void ShowBlockingError(const StructuredError &error);

		// Global accessor for layers and demos
		// Precondition: Application was created.
		static Application &Get();

		// Runtime service accessors
		// Precondition: CoreLayer initialized.
		EventBus &GetEventBus();
		[[nodiscard]] Notifier &GetNotifier();
		[[nodiscard]] CapabilityRegistry &GetCapabilityRegistry();
		[[nodiscard]] CapabilityService &GetCapabilityService();
		[[nodiscard]] AssetManager &GetAssetManager();
		// Precondition: CoreLayer initialized.
		JobSystem &GetJobSystem();
		// Precondition: CoreLayer initialized.
		ProgressTracker &GetProgressTracker();
		[[nodiscard]] WeakRef<ConfigManager> GetConfigManager() const;
		[[nodiscard]] const ApplicationConfig &GetConfig() const;

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
		bool beginCreateFromSpecification();
		bool bootstrapApplicationConfiguration();
		bool initializeEventInfrastructure();
		bool initializeWindowingAndGraphics();
		bool initializeApplicationLayers();
		bool initializeCoreRuntimeServices();
		bool finishCreateFromSpecification();
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
		bool initializeEventDispatchingSystem();
		bool initializeCoreLayerSystems();
		bool initializeAssetManager();

		// Configuration API
		bool bootstrapConfiguration();
		void applySpecificationFromDefaultConfig();
		bool persistUiSettings();

		// Low-level platform/graphics setup
		void setupDefaultLayers();
		bool initializeGlfw();
		bool createMainWindow();
		bool initializeGraphics();
		
		void shutdownWindow();
		void shutdownGlfw();
		void configureInputBackend();

		// Low-level frame and UI helpers
		void beginFrame();
		void drawMainPanel(bool &showDemoWindow, ImVec4 &clearColor, float frameRate);
		void renderFrame(const ImVec4 &clearColor, float frameRate);

		// Event queue internals
		static void ProcessQueuedEvents();
		void queueEvent(EventVariant event);
		void processPendingEvents();

	private:
		EventQueue m_EventQueue;
		Ref<EventBus> m_EventBus;
		Ref<Notifier> m_Notifier;
		Ref<CapabilityRegistry> m_CapabilityRegistry;
		Ref<CapabilityService> m_CapabilityService;
		Ref<AssetManager> m_AssetManager;
		Unique<ApplicationEventController> m_EventController;
		LayerStack m_LayerStack;
		Ref<ConfigManager> m_ConfigManager;
		std::optional<StructuredError> m_BlockingError;

		ApplicationConfig m_Config;
		ApplicationRuntimeState m_Runtime;
		ApplicationGraphicsState m_Graphics;


		// Singleton ownership
		static std::optional<std::reference_wrapper<Application>> s_Instance;
	};
} // namespace DefectStudio
