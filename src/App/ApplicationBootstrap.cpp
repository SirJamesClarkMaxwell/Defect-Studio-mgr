#include "Core/dspch.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#endif

#include "App/Application.hpp"
#include "App/ApplicationInternals.hpp"
#include "App/Controllers/ApplicationEventController.hpp"
#include "App/Events/ApplicationConfigEvents.hpp"
#include "App/Managers/ConfigManager.hpp"
#include "Core/Assets/AssetManager.hpp"
#include "Core/Capabilities/CapabilityService.hpp"
#include "Core/CoreLayer.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Input/KeyBindingEvents.hpp"
#include "Core/Logging/LogRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Notifications/Notifier.hpp"
#include "Core/Platform/PlatformSystem.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/RuntimeTuning.hpp"
#include "Domain/DomainLayer.hpp"
#include "IO/IOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/ImGuiLayer.hpp"
#include "Renderer/RendererAssetBundle.hpp"
#include "Renderer/RendererLayer.hpp"
#include "ScientificRuntime/ScientificRuntimeLayer.hpp"
#include "Storage/StorageLayer.hpp"

namespace DefectStudio
{
	namespace ApplicationDetail
	{
		namespace
		{
			std::atomic<bool> g_CrashHandlersInstalled = false;
			std::atomic<const char *> g_CrashStage = "startup";
			std::atomic<bool> g_TerminateHandlerInvoked = false;

			void appendLineToFile(const char *path, const char *message)
			{
				std::ofstream stream(path, std::ios::app);
				if (!stream)
					return;
				stream << message << '\n';
				stream.flush();
			}

			void terminateHandler()
			{
				g_TerminateHandlerInvoked.store(true, std::memory_order_relaxed);
				std::string reason = "unknown";
				if (const std::exception_ptr current = std::current_exception(); current != nullptr)
				{
					try
					{
						std::rethrow_exception(current);
					}
					catch (const std::exception &exception)
					{
						reason = exception.what();
					}
					catch (...)
					{
						reason = "non-standard exception";
					}
				}

				Logger::Flush();
				Platform::AppendNativeCrashStackTrace(AppendCrashMarker, 1);
				const std::string message = "[CRASH] std::terminate invoked reason=" + reason;
				AppendCrashMarker(message.c_str());
				std::_Exit(EXIT_FAILURE);
			}

			void signalHandler(int signalCode)
			{
				Platform::AppendNativeCrashStackTrace(AppendCrashMarker, 1);
				char buffer[128] = {};
				const int terminateInvoked = g_TerminateHandlerInvoked.load(std::memory_order_relaxed) ? 1 : 0;
				std::snprintf(buffer, sizeof(buffer), "[CRASH] signal=%d terminate=%d", signalCode, terminateInvoked);
				AppendCrashMarker(buffer);
				std::_Exit(EXIT_FAILURE);
			}

			bool HasPortableConfigFiles(const Path &root)
			{
				const Path configPath = root.Join("install").Join("app").Join("config");
				const Path defaultConfigFileName = configPath.Join(ConfigManager::DefaultConfigFileName);
				const Path userSettingsFileName = configPath.Join(ConfigManager::UserSettingsFileName);
				return FileSystem::Exists(defaultConfigFileName) || FileSystem::Exists(userSettingsFileName);
			}

			bool HasLegacyConfigFiles(const Path &root)
			{
				const Path configPath = root.Join("config");
				const Path defaultConfigFileName = configPath.Join(ConfigManager::DefaultConfigFileName);
				const Path userSettingsFileName = configPath.Join(ConfigManager::UserSettingsFileName);
				return FileSystem::Exists(defaultConfigFileName) || FileSystem::Exists(userSettingsFileName);
			}

			Path FindConfigDirectoryInAncestors(Path start)
			{
				Path current = Path::FromResolved(start.Native());
				for (int i = 0; i < 10; ++i)
				{
					if (HasPortableConfigFiles(current))
						return Path::FromResolved(current.Native() / "install" / "app" / "config");
					if (HasLegacyConfigFiles(current))
						return Path::FromResolved(current.Native() / "config");

					const Path parent = current.parent_path();
					if (parent.empty() || parent == current)
						break;

					current = parent;
				}

				return Path{};
			}

			Path ResolveConfigDirectory(char **argv)
			{
				const Path fromCwd = FindConfigDirectoryInAncestors(Path::FromResolved(FileSystem::CurrentPath()));
				if (!fromCwd.Empty())
					return fromCwd;

				if (argv != nullptr && argv[0] != nullptr)
				{
					std::error_code absoluteError;
					const Path executablePath = Path::FromResolved(FileSystem::Absolute(argv[0], absoluteError));
					if (!absoluteError && !executablePath.empty())
					{
						const Path fromExe = FindConfigDirectoryInAncestors(Path::FromResolved(executablePath.parent_path()));
						if (!fromExe.Empty())
							return fromExe;
					}
				}

				return Path::FromResolved(FileSystem::CurrentPath() / "install" / "app" / "config");
			}

			ApplicationSpecification defaultApplicationSpecification()
			{
				ApplicationSpecification specification;

#if defined(DS_DEBUG)
				specification.logLevel = DefectStudio::LogLevel::Trace;
#else
				specification.logLevel = DefectStudio::LogLevel::Info;
#endif

				specification.logToFile = true;
				specification.logFilePath = Path::FromResolved(RuntimeDefaults::LogFilePath);
				return specification;
			}

			std::optional<LogLevel> ParseLogLevelArgument(std::string_view value)
			{
				if (value == "trace")
					return LogLevel::Trace;
				if (value == "debug")
					return LogLevel::Debug;
				if (value == "info")
					return LogLevel::Info;
				if (value == "warn" || value == "warning")
					return LogLevel::Warn;
				if (value == "error")
					return LogLevel::Error;
				if (value == "critical")
					return LogLevel::Critical;
				return std::nullopt;
			}
		}

		void SetCrashStage(const char *stage)
		{
			g_CrashStage.store(stage, std::memory_order_relaxed);
		}

		void AppendCrashMarker(const char *message)
		{
			FileSystem::CreateDirectories("logs");
			char line[512] = {};
			std::snprintf(line,
			              sizeof(line),
			              "[%lld] %s stage=%s",
			              static_cast<long long>(std::time(nullptr)),
			              message,
			              g_CrashStage.load(std::memory_order_relaxed));

			appendLineToFile("logs/DefectStudio-crash.log", line);
			appendLineToFile(RuntimeDefaults::LogFilePath, line);
		}

		void InstallCrashFallbackHandlers()
		{
			if (g_CrashHandlersInstalled.exchange(true))
				return;

			std::set_terminate(terminateHandler);
			std::signal(SIGABRT, signalHandler);
			std::signal(SIGSEGV, signalHandler);
			std::signal(SIGILL, signalHandler);
			std::signal(SIGFPE, signalHandler);
			Platform::InstallNativeCrashHandler(AppendCrashMarker);
		}

		StartupStepTimer::StartupStepTimer(const char *name)
			: m_Name(name), m_Start(std::chrono::steady_clock::now())
		{
			DS_LOG_DEBUG("Startup step begin: {}", m_Name);
		}

		StartupStepTimer::~StartupStepTimer()
		{
			if (!m_Finished)
				Finish(false);
		}

		bool StartupStepTimer::Finish(bool success)
		{
			const auto elapsed = std::chrono::steady_clock::now() - m_Start;
			const auto elapsedMs = std::chrono::duration<double, std::milli>(elapsed).count();
			DS_LOG_INFO("Startup step end: {} success={} elapsed_ms={:.3f}", m_Name, success, elapsedMs);
			m_Finished = true;
			return success;
		}

		ApplicationSpecification ParseApplicationArguments(int argc, char **argv)
		{
			ApplicationSpecification specification = defaultApplicationSpecification();

			if (argv == nullptr)
				return specification;

			for (int index = 1; index < argc; ++index)
			{
				const std::string_view argument = argv[index];
				if (argument == "--help" || argument == "-h")
				{
					std::cout << "Usage: DefectStudio [--reset-layout] [--trace-events] [--log-level=trace|debug|info|warn|error] [--log-file[=path]]\n";
					std::exit(0);
				}

				if (argument == "--reset-layout")
					specification.resetLayout = true;

				if (argument == "--trace-events")
				{
					specification.traceEvents = true;
					specification.traceEventsOverride = true;
				}

				if (argument == "--log-file")
				{
					specification.logToFile = true;
					specification.logToFileOverride = true;
				}

				constexpr std::string_view logLevelPrefix = "--log-level=";
				if (argument.starts_with(logLevelPrefix))
				{
					const std::string_view levelText = argument.substr(logLevelPrefix.size());
					if (const std::optional<LogLevel> parsedLevel = ParseLogLevelArgument(levelText))
					{
						specification.logLevel = parsedLevel.value();
						specification.logLevelOverride = parsedLevel.value();
					}
				}

				constexpr std::string_view logFilePrefix = "--log-file=";
				if (argument.starts_with(logFilePrefix))
				{
					const std::string_view logFileText = argument.substr(logFilePrefix.size());
					specification.logToFile = true;
					specification.logToFileOverride = true;
					if (!logFileText.empty())
					{
						specification.logFilePath = Path::FromResolved(std::string(logFileText));
						specification.logFilePathOverride = specification.logFilePath;
					}
				}
			}

			return specification;
		}

		void ApplySpecificationOverrides(ApplicationSpecification &specification)
		{
			if (specification.logLevelOverride.has_value())
				specification.logLevel = specification.logLevelOverride.value();
			if (specification.logToFileOverride.has_value())
				specification.logToFile = specification.logToFileOverride.value();
			if (specification.logFilePathOverride.has_value())
				specification.logFilePath = specification.logFilePathOverride.value();
			if (specification.traceEventsOverride.has_value())
				specification.traceEvents = specification.traceEventsOverride.value();
		}
	}


	bool Application::createFromSpecification(const ApplicationSpecification &specification)
	{
		ZoneScopedN("Application.createFromSpecification");
		m_Runtime.specification = specification;
		ApplicationDetail::StartupStepTimer totalTimer("CreateFromSpecification.total");
		{
			ZoneScopedN("Application.CreateFromSpecification.InitialLogger");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.initializeLogger.initial");
			initializeLogger();
			timer.Finish(true);
		}

		{
			ZoneScopedN("Application.CreateFromSpecification.Begin");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.begin");
			if (!timer.Finish(beginCreateFromSpecification()))
				return false;
		}
		{
			ZoneScopedN("Application.CreateFromSpecification.BootstrapConfiguration");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.bootstrapConfiguration");
			if (!timer.Finish(bootstrapApplicationConfiguration()))
				return false;
		}
		{
			ZoneScopedN("Application.CreateFromSpecification.ReloadLogger");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.initializeLogger.afterConfig");
			initializeLogger();
			timer.Finish(true);
		}
		DS_LOG_INFO("Config directory: {}", m_Config.directory.String());
		logStartupSpecification();
		{
			ZoneScopedN("Application.CreateFromSpecification.EventInfrastructure");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.eventInfrastructure");
			if (!timer.Finish(initializeEventInfrastructure()))
				return false;
		}
		{
			ZoneScopedN("Application.CreateFromSpecification.AssetManager");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.assetManager");
			if (!timer.Finish(initializeAssetManager()))
				return false;
		}
		{
			ZoneScopedN("Application.CreateFromSpecification.WindowingAndGraphics");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.windowingAndGraphics");
			if (!timer.Finish(initializeWindowingAndGraphics()))
				return false;
		}
		{
			ZoneScopedN("Application.CreateFromSpecification.ApplicationLayers");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.applicationLayers");
			if (!timer.Finish(initializeApplicationLayers()))
				return false;
		}
		{
			ZoneScopedN("Application.CreateFromSpecification.CoreRuntimeServices");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.coreRuntimeServices");
			if (!timer.Finish(initializeCoreRuntimeServices()))
				return false;
		}
		{
			ZoneScopedN("Application.CreateFromSpecification.Finish");
			ApplicationDetail::StartupStepTimer finishTimer("CreateFromSpecification.finish");
			const bool finished = finishTimer.Finish(finishCreateFromSpecification());
			return totalTimer.Finish(finished);
		}
	}


	bool Application::beginCreateFromSpecification()
	{
		ZoneScopedN("Application.beginCreateFromSpecification");
		ApplicationDetail::SetCrashStage("create application");
		DS_LOG_INFO("CreateFromSpecification: begin");

		if (s_Instance.has_value())
			DS_LOG_WARN("Application::Create called more than once; replacing previous instance pointer");

		if (!m_Runtime.lifecycle.TryMarkCreated())
		{
			DS_LOG_WARN("Application::Create called more than once; call ignored");
			return false;
		}

		s_Instance = std::ref(*this);
		TracyMessageL("Launching GUI shell");
		return true;
	}


	bool Application::bootstrapApplicationConfiguration()
	{
		ZoneScopedN("Application.bootstrapApplicationConfiguration");
		ApplicationDetail::SetCrashStage("resolve configuration directory");
		{
			ZoneScopedN("Application.ResolveConfigDirectory");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.resolveConfigDirectory");
			m_Config.directory = ApplicationDetail::ResolveConfigDirectory(m_Runtime.argv);
			timer.Finish(true);
		}
		DS_LOG_INFO("CreateFromSpecification: resolved config directory={}", m_Config.directory.String());
		ApplicationDetail::SetCrashStage("bootstrap configuration");
		{
			ZoneScopedN("Application.bootstrapConfiguration");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.bootstrapConfiguration.load");
			if (!timer.Finish(bootstrapConfiguration()))
			{
				DS_LOG_ERROR("CreateFromSpecification: configuration bootstrap failed");
				shutdownInternal();
				return false;
			}
		}
		{
			ZoneScopedN("Application.ApplySpecificationFromDefaultConfig");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.applySpecificationFromConfig");
			applySpecificationFromDefaultConfig();
			timer.Finish(true);
		}
		return true;
	}


	bool Application::initializeEventInfrastructure()
	{
		ZoneScopedN("Application.initializeEventInfrastructure");
		ApplicationDetail::SetCrashStage("initialize event dispatch");
		DS_LOG_INFO("CreateFromSpecification: init EventDispatchingSystem");
		{
			ZoneScopedN("Application.initializeEventDispatchingSystem");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.initializeEventDispatchingSystem");
			if (!timer.Finish(initializeEventDispatchingSystem()))
			{
				DS_LOG_ERROR("CreateFromSpecification: EventDispatchingSystem init failed");
				shutdownInternal();
				return false;
			}
		}
		DS_LOG_INFO("CreateFromSpecification: EventDispatchingSystem ready");

		ApplicationDetail::SetCrashStage("create event bus");
		DS_LOG_INFO("CreateFromSpecification: create EventBus");
		{
			ZoneScopedN("Application.CreateEventBus");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.createEventBus");
			m_EventBus = CreateRef<EventBus>();
			if (!m_EventBus)
			{
				timer.Finish(false);
				DS_LOG_ERROR("Failed to initialize EventBus");
				shutdownInternal();
				return false;
			}
			timer.Finish(true);
		}
		DS_LOG_INFO("CreateFromSpecification: EventBus ready");
		{
			ZoneScopedN("Application.CreateEventController");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.createEventController");
			m_EventController = CreateUnique<ApplicationEventController>(
				m_EventBus,
				m_ConfigManager,
				m_Config,
				m_Runtime.specification,
				m_EventQueue,
				m_LogRegistry);
			timer.Finish(true);
		}
		return true;
	}


	bool Application::initializeAssetManager()
	{
		ZoneScopedN("Application.initializeAssetManager");
		ApplicationDetail::SetCrashStage("initialize asset manager");
		ApplicationDetail::StartupStepTimer assetTimer("CreateFromSpecification.initializeAssetManager.detail");
		const Path defaultAppRoot = m_Config.paths.assetsDirectory.parent_path();
		const Path userOverrideRoot = m_Config.paths.userConfigDirectory.parent_path();
		m_AssetManager = CreateRef<AssetManager>(defaultAppRoot, userOverrideRoot);
		m_AssetManager->RegisterAsset(AssetDescriptor{"assets/icon.ico", AssetType::Icon, AssetCriticality::Critical, "1", "app.icon"});
		m_AssetManager->RegisterAsset(AssetDescriptor{"assets/fonts/segoeui.ttf", AssetType::Font, AssetCriticality::Critical, "1", "ui.font.default"});
		m_AssetManager->RegisterAsset(AssetDescriptor{"assets/fonts/fa-solid-900.ttf", AssetType::Font, AssetCriticality::Optional, "1", "ui.font.icons"});
		m_AssetManager->RegisterAsset(AssetDescriptor{"assets/renderer/atom_styles.yaml", AssetType::Data, AssetCriticality::Critical, "1", "renderer.atom_styles"});
		m_AssetManager->RegisterAsset(AssetDescriptor{"data/elements/element_properties.yaml", AssetType::Data, AssetCriticality::Critical, "1", "renderer.element_properties"});
		m_AssetManager->RegisterAsset(AssetDescriptor{"data/renderer/startup_layout.yaml", AssetType::Data, AssetCriticality::Critical, "1", "renderer.startup_layout"});
		m_AssetManager->RegisterAsset(AssetDescriptor{"assets/renderer/meshes/sphere.obj", AssetType::Data, AssetCriticality::Critical, "1", "renderer.mesh.sphere"});
		m_AssetManager->RegisterAsset(AssetDescriptor{"assets/renderer/meshes/cylinder.obj", AssetType::Data, AssetCriticality::Critical, "1", "renderer.mesh.cylinder"});

		AssetValidationReport report;
		{
			ZoneScopedN("Application.AssetManager.ValidateRegisteredAssets");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.validateAssets");
			report = m_AssetManager->ValidateRegisteredAssets();
			timer.Finish(true);
		}
		for (const AssetValidationIssue &issue : report.issues)
		{
			if (issue.descriptor.criticality == AssetCriticality::Critical)
				DS_LOG_ERROR("Critical asset missing [{}]: {}", issue.descriptor.logicalPath, issue.error.technicalDetails);
			else
				DS_LOG_WARN("Optional asset missing [{}]: {}", issue.descriptor.logicalPath, issue.error.technicalDetails);
		}

		if (report.HasCriticalFailures())
		{
			assetTimer.Finish(false);
			DS_LOG_ERROR("Asset validation failed: critical assets are missing");
			shutdownInternal();
			return false;
		}

		DS_LOG_INFO(
			"AssetManager initialized: default_root={} user_override_root={} resolved={} issues={}",
			m_AssetManager->GetDefaultRoot().String(),
			m_AssetManager->GetUserOverrideRoot().String(),
			report.resolvedAssets.size(),
			report.issues.size());
		return assetTimer.Finish(true);
	}


	bool Application::initializeWindowingAndGraphics()
	{
		ZoneScopedN("Application.initializeWindowingAndGraphics");
		ApplicationDetail::SetCrashStage("initialize glfw");
		DS_LOG_INFO("CreateFromSpecification: init GLFW");
		{
			ZoneScopedN("Application.initializeGlfw");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.initializeGlfw");
			if (!timer.Finish(initializeGlfw()))
			{
				DS_LOG_ERROR("CreateFromSpecification: GLFW init failed");
				shutdownInternal();
				return false;
			}
		}
		DS_LOG_INFO("CreateFromSpecification: GLFW ready");

		ApplicationDetail::SetCrashStage("create main window");
		DS_LOG_INFO("CreateFromSpecification: create main window");
		{
			ZoneScopedN("Application.createMainWindow");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.createMainWindow");
			if (!timer.Finish(createMainWindow()))
			{
				DS_LOG_ERROR("CreateFromSpecification: main window creation failed");
				shutdownInternal();
				return false;
			}
		}
		DS_LOG_INFO("CreateFromSpecification: main window ready");

		ApplicationDetail::SetCrashStage("initialize graphics");
		DS_LOG_INFO("CreateFromSpecification: init graphics");
		{
			ZoneScopedN("Application.initializeGraphics");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.initializeGraphics");
			if (!timer.Finish(initializeGraphics()))
			{
				DS_LOG_ERROR("CreateFromSpecification: graphics init failed");
				shutdownInternal();
				return false;
			}
		}
		DS_LOG_INFO("CreateFromSpecification: graphics ready");
		return true;
	}


	bool Application::initializeApplicationLayers()
	{
		ZoneScopedN("Application.initializeApplicationLayers");
		ApplicationDetail::SetCrashStage("setup layers");
		DS_LOG_INFO("CreateFromSpecification: setup default layers");
		{
			ZoneScopedN("Application.setupDefaultLayers");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.setupDefaultLayers");
			setupDefaultLayers();
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.VerifyImGuiLayer");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.verifyImGuiLayer");
			if (auto imGuiLayer = m_LayerStack.FindLayerAs<ImGuiLayer>(LayerId::ImGui).lock();
			    imGuiLayer == nullptr || !imGuiLayer->IsInitialized())
			{
				timer.Finish(false);
				DS_LOG_ERROR("CreateFromSpecification: ImGuiLayer initialization failed");
				shutdownInternal();
				return false;
			}
			timer.Finish(true);
		}
		DS_LOG_INFO("CreateFromSpecification: default layers ready");
		return true;
	}


	bool Application::initializeCoreRuntimeServices()
	{
		ZoneScopedN("Application.initializeCoreRuntimeServices");
		ApplicationDetail::SetCrashStage("initialize core runtime services");
		DS_LOG_INFO("CreateFromSpecification: init core runtime services");
		{
			ZoneScopedN("Application.CreateCapabilityService");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.createCapabilityService");
			m_CapabilityService = CreateRef<CapabilityService>();
			timer.Finish(m_CapabilityService != nullptr);
		}
		{
			ZoneScopedN("Application.RegisterCapabilities");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.registerCapabilities");
			auto scientificRuntimeLayer = m_LayerStack.FindLayerAs<ScientificRuntimeLayer>(LayerId::ScientificRuntime).lock();
			if (scientificRuntimeLayer != nullptr && m_CapabilityService != nullptr)
			{
				scientificRuntimeLayer->RegisterCapability(
					*m_CapabilityService,
					scientificRuntimeLayer->BuildPythonBridgeCapability());
			}
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.LockCapabilitiesAfterStartup");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.lockCapabilitiesAfterStartup");
			if (m_CapabilityService)
				m_CapabilityService->LockAfterStartup();
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.CreateNotifier");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.createNotifier");
			m_Notifier = CreateRef<Notifier>(m_EventBus);
			timer.Finish(m_Notifier != nullptr);
		}
		{
			ZoneScopedN("Application.initializeCoreLayerSystems");
			ApplicationDetail::StartupStepTimer timer("CreateFromSpecification.initializeCoreLayerSystems");
			if (!timer.Finish(initializeCoreLayerSystems()))
			{
				DS_LOG_ERROR("CreateFromSpecification: core runtime services init failed");
				shutdownInternal();
				return false;
			}
		}
		DS_LOG_INFO("CreateFromSpecification: core runtime services ready");
		return true;
	}


	bool Application::finishCreateFromSpecification()
	{
		m_Runtime.lifecycle.SetRunning(true);
		ApplicationDetail::SetCrashStage("running");
		DS_LOG_INFO("CreateFromSpecification: success, runtime running");
		return true;
	}


	bool Application::initializeEventDispatchingSystem()
	{
		ZoneScopedN("Application.initializeEventDispatchingSystem.detail");
		DS_LOG_INFO("Init: EventDispatchingSystem (EventQueue)");
		m_EventQueue.Configure(m_Config.eventQueue.initialCapacity, m_Config.eventQueue.growthStep);
		return true;
	}


	void Application::shutdownInternal()
	{
		if (!m_Runtime.lifecycle.TryBeginShutdown())
			return;

		DS_LOG_INFO("Shutdown: persisting UI settings");
		persistUiSettings();

		DS_LOG_INFO("Shutdown: clearing layers");
		m_LayerStack.Clear();

		if (m_EventController)
		{
			DS_LOG_INFO("Shutdown: releasing ApplicationEventController");
			m_EventController.reset();
		}

		m_CapabilityService.reset();
		m_AssetManager.reset();
		m_Notifier.reset();

		if (m_EventBus)
		{
			DS_LOG_INFO("Shutdown: releasing EventBus");
			m_EventBus->ClearQueue();
			m_EventBus->ClearAllListeners();
			m_EventBus.reset();
		}

		DS_LOG_INFO("Shutdown: destroying window");
		shutdownWindow();

		DS_LOG_INFO("Shutdown: terminating GLFW");
		shutdownGlfw();

		DS_LOG_INFO("Shutdown: closing logger");
		shutdownLogger();
	}


	void Application::setupDefaultLayers()
	{
		ZoneScopedN("Application.setupDefaultLayers.detail");
		DS_LOG_INFO("LayerStack setup: begin");
		{
			ZoneScopedN("Application.setupDefaultLayers.PushCoreLayer");
			ApplicationDetail::StartupStepTimer timer("LayerStack.push.CoreLayer");
			m_LayerStack.PushLayer(CreateUnique<CoreLayer>());
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.setupDefaultLayers.PushIOLayer");
			ApplicationDetail::StartupStepTimer timer("LayerStack.push.IOLayer");
			auto ioLayer = CreateUnique<IOLayer>();
			ioLayer->BindRuntimeServices(m_EventBus);
			m_LayerStack.PushLayer(std::move(ioLayer));
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.setupDefaultLayers.PushStorageLayer");
			ApplicationDetail::StartupStepTimer timer("LayerStack.push.StorageLayer");
			m_LayerStack.PushLayer(CreateUnique<StorageLayer>());
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.setupDefaultLayers.PushScientificRuntimeLayer");
			ApplicationDetail::StartupStepTimer timer("LayerStack.push.ScientificRuntimeLayer");
			m_LayerStack.PushLayer(CreateUnique<ScientificRuntimeLayer>());
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.setupDefaultLayers.PushDomainLayer");
			ApplicationDetail::StartupStepTimer timer("LayerStack.push.DomainLayer");
			m_LayerStack.PushLayer(CreateUnique<DomainLayer>());
			timer.Finish(true);
		}
		RendererAssetBundle rendererAssets;
		const Result<RendererAssetBundle> rendererAssetsResult = [&]() {
			ZoneScopedN("Application.setupDefaultLayers.LoadRendererAssetBundle");
			ApplicationDetail::StartupStepTimer timer("LayerStack.load.RendererAssetBundle");
			auto result = LoadRendererAssetBundle(*m_AssetManager);
			timer.Finish(result.HasValue());
			return result;
		}();
		if (!rendererAssetsResult.HasValue())
		{
			const StructuredError &error = rendererAssetsResult.Error();
			DS_LOG_ERROR(
				"Renderer startup assets failed to load [{}]: {}",
				error.code.empty() ? "unknown" : error.code,
				error.technicalDetails);
		}
		else
		{
			rendererAssets = rendererAssetsResult.Value();
		}
		RendererStartupConfig rendererStartupConfig;
		{
			ZoneScopedN("Application.setupDefaultLayers.BuildRendererStartupConfig");
			ApplicationDetail::StartupStepTimer timer("LayerStack.build.RendererStartupConfig");
			rendererStartupConfig.configDirectory = m_Config.paths.userConfigDirectory;
			rendererStartupConfig.assetsDirectory = m_Config.paths.assetsDirectory;
			rendererStartupConfig.shaderDirectory = Path::FromResolved(
				FileSystem::CurrentPath() / "src" / "Renderer" / "OpenGl" / "Shaders");
			rendererStartupConfig.atomStyleTable = std::move(rendererAssets.atomStyleTable);
			rendererStartupConfig.elementPropertiesTable = std::move(rendererAssets.elementPropertiesTable);
			rendererStartupConfig.startupLayout = std::move(rendererAssets.startupLayout);
			rendererStartupConfig.primitiveMeshes = std::move(rendererAssets.primitiveMeshes);
			rendererStartupConfig.loadDefaultScene = rendererAssetsResult.HasValue();
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.setupDefaultLayers.CreateRendererLayer");
			ApplicationDetail::StartupStepTimer timer("LayerStack.create.RendererLayer");
			auto rendererLayerInstance = CreateUnique<RendererLayer>(std::move(rendererStartupConfig));
			rendererLayerInstance->BindEventBus(m_EventBus);
			m_LayerStack.PushLayer(std::move(rendererLayerInstance));
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.setupDefaultLayers.ApplyRendererConfig");
			ApplicationDetail::StartupStepTimer timer("LayerStack.apply.RendererConfig");
			auto rendererLayer = m_LayerStack.FindLayerAs<RendererLayer>(LayerId::Renderer).lock();
			if (rendererLayer != nullptr)
				rendererLayer->ApplyConfig(m_Config);
			timer.Finish(rendererLayer != nullptr);
		}
		ImGuiLayerRuntime imGuiRuntime;
		{
			ZoneScopedN("Application.setupDefaultLayers.BuildImGuiRuntime");
			ApplicationDetail::StartupStepTimer timer("LayerStack.build.ImGuiRuntime");
			imGuiRuntime.nativeWindow = m_Graphics.window != nullptr ? m_Graphics.window->GetNativeHandle() : nullptr;
			imGuiRuntime.eventBus = m_EventBus;
			auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
			if (coreLayer != nullptr)
				imGuiRuntime.jobSystem = coreLayer->GetJobSystemHandle();
			imGuiRuntime.ui = m_Config.ui;
			imGuiRuntime.appearance = m_Config.appearance;
			imGuiRuntime.layoutPath = m_Config.layout.imGuiIniPath.empty()
				? ConfigManager::GetLayoutPath(m_Config.directory)
				: Path::FromResolved(m_Config.layout.imGuiIniPath);
			imGuiRuntime.resetLayout = m_Runtime.specification.resetLayout;
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.setupDefaultLayers.PushImGuiLayer");
			ApplicationDetail::StartupStepTimer timer("LayerStack.push.ImGuiLayer");
			m_LayerStack.PushLayer(CreateUnique<ImGuiLayer>(std::move(imGuiRuntime)));
			timer.Finish(true);
		}
		{
			ZoneScopedN("Application.setupDefaultLayers.PushEditorLayer");
			ApplicationDetail::StartupStepTimer timer("LayerStack.push.EditorLayer");
			m_LayerStack.PushLayer(CreateUnique<EditorLayer>());
			timer.Finish(true);
		}
		// m_LayerStack.PushLayer(CreateUnique<DemoLayer>());
		// m_LayerStack.PushLayer(CreateUnique<DebugLayer>());
		DS_LOG_INFO("LayerStack setup: DemoLayer and DebugLayer registration disabled");
		DS_LOG_INFO("LayerStack setup: complete");
	}


	void Application::initializeLogger() const
	{
		LoggerOptions loggerOptions;
		loggerOptions.level = m_Runtime.specification.logLevel;
		loggerOptions.logToFile = m_Runtime.specification.logToFile;
		loggerOptions.logFilePath = m_Runtime.specification.logFilePath.Native();
		loggerOptions.logRegistry = m_LogRegistry;
		Logger::Initialize(loggerOptions);
	}


	void Application::shutdownLogger() const
	{
		// m_LogRegistry = nullptr;
		Logger::Shutdown();
	}


	void Application::logStartupSpecification() const
	{
		DS_LOG_INFO("Starting DefectStudio GUI shell");
		DS_LOG_INFO("Log level: {}", ToString(m_Runtime.specification.logLevel));

		if (m_Runtime.specification.logToFile)
		{
			if (m_Runtime.specification.logFilePath.Empty())
				DS_LOG_INFO("File logging enabled: {}", RuntimeDefaults::LogFilePath);
			else
				DS_LOG_INFO("File logging enabled: {}", m_Runtime.specification.logFilePath.String());
		}

		if (m_Runtime.specification.resetLayout)
			DS_LOG_WARN("Layout reset requested");

		if (m_Runtime.specification.traceEvents)
			DS_LOG_INFO("Event tracing enabled");
	}


	bool Application::initializeCoreLayerSystems()
	{
		ZoneScopedN("Application.initializeCoreLayerSystems.detail");
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		auto editorLayer = m_LayerStack.FindLayerAs<EditorLayer>(LayerId::Editor).lock();
		auto imGuiLayer = m_LayerStack.FindLayerAs<ImGuiLayer>(LayerId::ImGui).lock();
		DS_ASSERT(coreLayer != nullptr, "CoreLayer was not created");
		DS_ASSERT(m_EventBus != nullptr, "EventBus was not created");
		DS_LOG_INFO("Init: Core runtime services via CoreLayer");
		bool systemInitialized = false;
		{
			ZoneScopedN("Application.CoreLayer.InitializeSystems");
			ApplicationDetail::StartupStepTimer timer("CoreLayer.InitializeSystems");
			systemInitialized = coreLayer->InitializeSystems(m_EventBus, m_Config.jobs);
			timer.Finish(systemInitialized);
		}
		if (!systemInitialized)
		{
			DS_LOG_ERROR("Init: Core runtime services failed");
			return false;
		}

		// Sanity-check: force asserts if any core service failed to initialize.
		{
			ZoneScopedN("Application.CoreLayer.VerifyRuntimeServices");
			ApplicationDetail::StartupStepTimer timer("CoreLayer.VerifyRuntimeServices");
			GetEventBus();
			GetJobSystem();
			GetProgressTracker();
			timer.Finish(true);
		}

		if (editorLayer != nullptr)
		{
			{
				ZoneScopedN("Application.EditorLayer.BindRuntimeServices");
				ApplicationDetail::StartupStepTimer timer("EditorLayer.BindRuntimeServices");
				editorLayer->BindRuntimeServices(
					m_EventBus,
					coreLayer->GetJobSystemHandle(),
					coreLayer->GetProgressTrackerHandle(),
					m_LogRegistry,
					coreLayer->GetCommandServiceHandle(),
					coreLayer->GetKeymapResolverHandle(),
					coreLayer->GetContextManagerHandle(),
					coreLayer->GetCommandRegistryHandle());
				timer.Finish(true);
			}
			{
				ZoneScopedN("Application.EditorLayer.ApplyConfig");
				ApplicationDetail::StartupStepTimer timer("EditorLayer.ApplyConfig");
				editorLayer->ApplyConfig(m_Config);
				timer.Finish(true);
			}
			{
				ZoneScopedN("Application.ImGuiLayer.BindUiState");
				ApplicationDetail::StartupStepTimer timer("ImGuiLayer.BindUiState");
				if (imGuiLayer != nullptr)
					imGuiLayer->BindUiState(editorLayer->GetUiStateHandle());
				timer.Finish(imGuiLayer != nullptr);
			}
		}

		const Path keybindingsPath = ConfigManager::GetKeybindingsPath(m_Config.directory);
		{
			ZoneScopedN("Application.LoadStartupKeybindings");
			ApplicationDetail::StartupStepTimer timer("Application.LoadStartupKeybindings");
			m_EventBus->Queue(AppEvents::Keymap::BindingsLoadRequested{keybindingsPath});
			m_EventBus->ProcessQueue();
			timer.Finish(true);
		}
		DS_LOG_INFO("Init: Core runtime services ready");
		return true;
	}


	bool Application::bootstrapConfiguration()
	{
		ZoneScopedN("Application.bootstrapConfiguration.detail");
		ApplicationDetail::StartupStepTimer timer("ConfigManager.Initialize");
		m_ConfigManager = CreateRef<ConfigManager>(m_Config.directory);
		std::string configError;
		if (!m_ConfigManager->Initialize(configError))
		{
			timer.Finish(false);
			DS_LOG_ERROR("Config bootstrap failed [{}]: {}", m_Config.directory.String(), configError);
			m_ConfigManager.reset();
			return false;
		}

		m_Config = m_ConfigManager->GetConfig();
		DS_LOG_INFO("Config bootstrap complete: {}", m_ConfigManager->GetConfigDirectory().String());
		return timer.Finish(true);
	}


	void Application::applySpecificationFromDefaultConfig()
	{
		ZoneScopedN("Application.applySpecificationFromDefaultConfig.detail");
		ApplicationDetail::StartupStepTimer timer("ConfigManager.ApplySpecification");
		DS_ASSERT(m_ConfigManager != nullptr, "ConfigManager is not initialized");
		m_ConfigManager->ApplySpecification(m_Runtime.specification);
		ApplicationDetail::ApplySpecificationOverrides(m_Runtime.specification);

		DS_LOG_INFO(
			"Runtime tuning loaded: font_scale=[{}, {}] step=[{}, {}] step_slider_max={} workers_default={} reserve_urgent={} event_queue={{capacity:{}, growth:{}}}",
			m_Config.ui.fontScaleMin,
			m_Config.ui.fontScaleMax,
			m_Config.ui.fontScaleStepMin,
			m_Config.ui.fontScaleStepMax,
			m_Config.ui.fontScaleStepSliderMax,
			m_Config.jobs.defaultWorkerThreadCount,
			m_Config.jobs.reserveUrgentWorker,
			m_Config.eventQueue.initialCapacity,
			m_Config.eventQueue.growthStep);
		timer.Finish(true);
	}


	bool Application::persistUiSettings()
	{
		if (m_ConfigManager == nullptr)
			return false;

		if (m_Graphics.window != nullptr)
			m_Graphics.window->CaptureConfig(m_Config.window);

		if (auto editorLayer = m_LayerStack.FindLayerAs<EditorLayer>(LayerId::Editor).lock())
			editorLayer->ExportConfig(m_Config);

		m_ConfigManager->SetConfig(m_Config);
		if (m_EventBus == nullptr)
		{
			DS_LOG_WARN("UI settings save skipped: EventBus unavailable");
			return false;
		}

		m_EventBus->Queue(AppEvents::Config::SaveUserRequested{m_Config});
		m_EventBus->ProcessQueue();
		m_EventBus->ProcessQueue();

		return true;
	}


} // namespace DefectStudio
