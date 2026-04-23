#include "Core/dspch.hpp"

// #include <algorithm>
// #include <array>
// #include <cctype>
// #include <fstream>
// #include <locale>
// #include <sstream>
// #include <string_view>

#include <yaml-cpp/yaml.h>

#include "App/ConfigManager.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/RuntimeTuning.hpp"

namespace DefectStudio
{
	namespace ConfigSchema
	{
		namespace Root
		{
			inline constexpr const char *SchemaVersion = "schema_version";
			inline constexpr const char *TraceEvents = "trace_events";
		}

		namespace Sections
		{
			inline constexpr const char *Appearance = "appearance";
			inline constexpr const char *EventQueue = "event_queue";
			inline constexpr const char *Jobs = "jobs";
			inline constexpr const char *Log = "log";
			inline constexpr const char *UI = "ui";
			inline constexpr const char *Window = "window";
		}

		namespace Appearance
		{
			inline constexpr const char *ClearColor = "clear_color";
		}

		namespace EventQueue
		{
			inline constexpr const char *GrowthStep = "growth_step";
			inline constexpr const char *InitialCapacity = "initial_capacity";
		}

		namespace Jobs
		{
			inline constexpr const char *DefaultWorkerThreads = "default_worker_threads";
			inline constexpr const char *ReserveUrgentWorker = "reserve_urgent_worker";
		}

		namespace Log
		{
			inline constexpr const char *FilePath = "file_path";
			inline constexpr const char *Level = "level";
			inline constexpr const char *ToFile = "to_file";
		}

		namespace UI
		{
			inline constexpr const char *Font = "font";
			inline constexpr const char *FontPath = "path";
			inline constexpr const char *FontScale = "font_scale";
			inline constexpr const char *FontScaleMax = "max";
			inline constexpr const char *FontScaleMin = "min";
			inline constexpr const char *FontScaleStep = "font_scale_step";
			inline constexpr const char *FontScaleStepMax = "max";
			inline constexpr const char *FontScaleStepMin = "min";
			inline constexpr const char *FontScaleStepSliderMax = "slider_max";
		}

		namespace Window
		{
			inline constexpr const char *Height = "height";
			inline constexpr const char *Title = "title";
			inline constexpr const char *Width = "width";
		}

		namespace Legacy
		{
			inline constexpr const char *AppearanceClearColor = "clear_color";
			inline constexpr const char *EventQueueGrowthStep = "event_queue.growth_step";
			inline constexpr const char *EventQueueInitialCapacity = "event_queue.initial_capacity";
			inline constexpr const char *JobsDefaultWorkerThreads = "jobs.default_worker_threads";
			inline constexpr const char *JobsReserveUrgentWorker = "jobs.reserve_urgent_worker";
			inline constexpr const char *LogFilePath = "log.file_path";
			inline constexpr const char *LogLevel = "log.level";
			inline constexpr const char *LogToFile = "log.to_file";
			inline constexpr const char *UiFontPath = "font_path";
			inline constexpr const char *UiFontScale = "font_scale";
			inline constexpr const char *UiFontScaleMax = "ui.font_scale.max";
			inline constexpr const char *UiFontScaleMin = "ui.font_scale.min";
			inline constexpr const char *UiFontScaleStep = "font_scale_step";
			inline constexpr const char *UiFontScaleStepMax = "ui.font_scale_step.max";
			inline constexpr const char *UiFontScaleStepMin = "ui.font_scale_step.min";
			inline constexpr const char *UiFontScaleStepSliderMax = "ui.font_scale_step.slider_max";
			inline constexpr const char *WindowHeight = "window.height";
			inline constexpr const char *WindowTitle = "window.title";
			inline constexpr const char *WindowWidth = "window.width";
		}
	}

	namespace ConfigYaml
	{
		YAML::Node MissingNode()
		{
			return YAML::Node(YAML::NodeType::Undefined);
		}

		YAML::Node FindNodeRecursive(const YAML::Node &current, const std::string_view *path, std::size_t remaining)
		{
			if (remaining == 0)
				return current;

			if (!current || !current.IsMap())
				return MissingNode();

			YAML::Node child = current[std::string(path[0])];
			if (!child.IsDefined())
				return MissingNode();

			return FindNodeRecursive(child, path + 1, remaining - 1);
		}

		YAML::Node FindNode(const YAML::Node &root, std::initializer_list<std::string_view> path)
		{
			if (!root || !root.IsMap())
				return MissingNode();

			return FindNodeRecursive(root, path.begin(), path.size());
		}

		YAML::Node FindNode(const YAML::Node &root,
		                    std::initializer_list<std::string_view> path,
		                    std::string_view legacyFlatKey)
		{
			YAML::Node node = FindNode(root, path);
			if (node.IsDefined() || legacyFlatKey.empty() || !root || !root.IsMap())
				return node;

			try
			{
				YAML::Node legacyNode = root[std::string(legacyFlatKey)];
				if (legacyNode.IsDefined())
					return legacyNode;
			}
			catch (const YAML::InvalidNode &)
			{
			}

			return MissingNode();
		}

		template <typename TValue>
		TValue ReadValue(const YAML::Node &root,
		                 std::initializer_list<std::string_view> path,
		                 std::string_view legacyFlatKey,
		                 TValue fallback)
		{
			const YAML::Node node = FindNode(root, path, legacyFlatKey);
			if (!node.IsDefined() || node.IsNull())
				return fallback;

			try
			{
				return node.as<TValue>();
			}
			catch (const YAML::Exception &)
			{
				return fallback;
			}
		}

		std::string ReadString(const YAML::Node &root,
		                       std::initializer_list<std::string_view> path,
		                       std::string_view legacyFlatKey,
		                       std::string_view fallback)
		{
			return ReadValue<std::string>(root, path, legacyFlatKey, std::string(fallback));
		}

		std::string ToLowerCopy(std::string_view value)
		{
			std::string result(value);
			for (char &character : result)
				character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			return result;
		}

		LogLevel ParseLogLevel(std::string_view value, LogLevel fallback)
		{
			const std::string normalized = ToLowerCopy(value);
			if (normalized == "trace")
				return LogLevel::Trace;
			if (normalized == "debug")
				return LogLevel::Debug;
			if (normalized == "info")
				return LogLevel::Info;
			if (normalized == "warn" || normalized == "warning")
				return LogLevel::Warn;
			if (normalized == "error")
				return LogLevel::Error;
			if (normalized == "critical")
				return LogLevel::Critical;
			return fallback;
		}

		std::string LogLevelToString(LogLevel level)
		{
			switch (level)
			{
			case LogLevel::Trace:
				return "trace";
			case LogLevel::Debug:
				return "debug";
			case LogLevel::Info:
				return "info";
			case LogLevel::Warn:
				return "warn";
			case LogLevel::Error:
				return "error";
			case LogLevel::Critical:
				return "critical";
			}
			return "info";
		}

		std::array<float, 4> ParseColorList(std::string_view text, std::array<float, 4> fallback)
		{
			std::array<float, 4> result = fallback;
			std::string normalized(text);
			std::replace(normalized.begin(), normalized.end(), ',', ' ');
			std::istringstream stream(normalized);
			stream.imbue(std::locale::classic());

			for (float &component : result)
			{
				if (!(stream >> component))
					return fallback;
			}
			return result;
		}

		std::array<float, 4> ReadColor(const YAML::Node &root, std::array<float, 4> fallback)
		{
			YAML::Node node = FindNode(root,
			                          {ConfigSchema::Sections::Appearance, ConfigSchema::Appearance::ClearColor},
			                          ConfigSchema::Legacy::AppearanceClearColor);
			if (!node.IsDefined() || node.IsNull())
				node = FindNode(root, {ConfigSchema::Sections::UI, ConfigSchema::Appearance::ClearColor});
			if (!node.IsDefined() || node.IsNull())
				return fallback;

			try
			{
				if (node.IsSequence())
				{
					std::array<float, 4> result = fallback;
					const std::size_t count = std::min<std::size_t>(node.size(), result.size());
					for (std::size_t index = 0; index < count; ++index)
						result[index] = node[index].as<float>();
					return result;
				}

				if (node.IsScalar())
					return ParseColorList(node.as<std::string>(), fallback);
			}
			catch (const YAML::Exception &)
			{
			}

			return fallback;
		}

		void ApplyDefaultYaml(const YAML::Node &root, ApplicationConfig &config)
		{
			config.log.level = ParseLogLevel(
				ReadString(root, {ConfigSchema::Sections::Log, ConfigSchema::Log::Level}, ConfigSchema::Legacy::LogLevel, LogLevelToString(config.log.level)),
				config.log.level);
			config.log.toFile = ReadValue(root, {ConfigSchema::Sections::Log, ConfigSchema::Log::ToFile}, ConfigSchema::Legacy::LogToFile, config.log.toFile);
			config.log.filePath = Path::FromResolved(ReadString(
				root,
				{ConfigSchema::Sections::Log, ConfigSchema::Log::FilePath},
				ConfigSchema::Legacy::LogFilePath,
				config.log.filePath.Empty() ? RuntimeDefaults::LogFilePath : config.log.filePath.String()));
			config.log.traceEvents = ReadValue(root, {ConfigSchema::Root::TraceEvents}, ConfigSchema::Root::TraceEvents, config.log.traceEvents);

			config.window.width = ReadValue(root, {ConfigSchema::Sections::Window, ConfigSchema::Window::Width}, ConfigSchema::Legacy::WindowWidth, config.window.width);
			config.window.height = ReadValue(root, {ConfigSchema::Sections::Window, ConfigSchema::Window::Height}, ConfigSchema::Legacy::WindowHeight, config.window.height);
			config.window.title = ReadString(root, {ConfigSchema::Sections::Window, ConfigSchema::Window::Title}, ConfigSchema::Legacy::WindowTitle, config.window.title);

			config.ui.fontScaleMin = ReadValue(root,
			                                {ConfigSchema::Sections::UI, ConfigSchema::UI::FontScale, ConfigSchema::UI::FontScaleMin},
			                                ConfigSchema::Legacy::UiFontScaleMin,
			                                config.ui.fontScaleMin);
			config.ui.fontScaleMax = ReadValue(root,
			                                {ConfigSchema::Sections::UI, ConfigSchema::UI::FontScale, ConfigSchema::UI::FontScaleMax},
			                                ConfigSchema::Legacy::UiFontScaleMax,
			                                config.ui.fontScaleMax);
			if (config.ui.fontScaleMax < config.ui.fontScaleMin)
				std::swap(config.ui.fontScaleMin, config.ui.fontScaleMax);

			config.ui.fontScaleStepMin = ReadValue(root,
			                                    {ConfigSchema::Sections::UI, ConfigSchema::UI::FontScaleStep, ConfigSchema::UI::FontScaleStepMin},
			                                    ConfigSchema::Legacy::UiFontScaleStepMin,
			                                    config.ui.fontScaleStepMin);
			config.ui.fontScaleStepMax = ReadValue(root,
			                                    {ConfigSchema::Sections::UI, ConfigSchema::UI::FontScaleStep, ConfigSchema::UI::FontScaleStepMax},
			                                    ConfigSchema::Legacy::UiFontScaleStepMax,
			                                    config.ui.fontScaleStepMax);
			if (config.ui.fontScaleStepMax < config.ui.fontScaleStepMin)
				std::swap(config.ui.fontScaleStepMin, config.ui.fontScaleStepMax);
			config.ui.fontScaleStepSliderMax = ReadValue(root,
			                                          {ConfigSchema::Sections::UI, ConfigSchema::UI::FontScaleStep, ConfigSchema::UI::FontScaleStepSliderMax},
			                                          ConfigSchema::Legacy::UiFontScaleStepSliderMax,
			                                          config.ui.fontScaleStepSliderMax);
			config.ui.fontScaleStepSliderMax = std::clamp(config.ui.fontScaleStepSliderMax,
			                                             config.ui.fontScaleStepMin,
			                                             config.ui.fontScaleStepMax);
			config.ui.clearColor = ReadColor(root, config.ui.clearColor);

			config.jobs.defaultWorkerThreadCount = std::max(1,
				ReadValue(root,
				          {ConfigSchema::Sections::Jobs, ConfigSchema::Jobs::DefaultWorkerThreads},
				          ConfigSchema::Legacy::JobsDefaultWorkerThreads,
				          config.jobs.defaultWorkerThreadCount));
			config.jobs.reserveUrgentWorker = ReadValue(root,
			                                      {ConfigSchema::Sections::Jobs, ConfigSchema::Jobs::ReserveUrgentWorker},
			                                      ConfigSchema::Legacy::JobsReserveUrgentWorker,
			                                      config.jobs.reserveUrgentWorker);

			const int initialCapacity = ReadValue(root,
			                                  {ConfigSchema::Sections::EventQueue, ConfigSchema::EventQueue::InitialCapacity},
			                                  ConfigSchema::Legacy::EventQueueInitialCapacity,
			                                  static_cast<int>(config.eventQueue.initialCapacity));
			const int growthStep = ReadValue(root,
			                             {ConfigSchema::Sections::EventQueue, ConfigSchema::EventQueue::GrowthStep},
			                             ConfigSchema::Legacy::EventQueueGrowthStep,
			                             static_cast<int>(config.eventQueue.growthStep));
			config.eventQueue.initialCapacity = static_cast<std::size_t>(
				std::max(initialCapacity, static_cast<int>(RuntimeDefaults::EventQueueMinCapacity)));
			config.eventQueue.growthStep = static_cast<std::size_t>(
				std::max(growthStep, static_cast<int>(RuntimeDefaults::EventQueueMinGrowthStep)));
		}

		void ApplyUserYaml(const YAML::Node &root, ApplicationConfig &config)
		{
			config.ui.fontScale = ReadValue(root,
			                              {ConfigSchema::Sections::UI, ConfigSchema::UI::FontScale},
			                              ConfigSchema::Legacy::UiFontScale,
			                              config.ui.fontScale);
			config.ui.fontScaleStep = ReadValue(root,
			                                  {ConfigSchema::Sections::UI, ConfigSchema::UI::FontScaleStep},
			                                  ConfigSchema::Legacy::UiFontScaleStep,
			                                  config.ui.fontScaleStep);
			config.ui.fontPath = ReadString(root,
			                               {ConfigSchema::Sections::UI, ConfigSchema::UI::Font, ConfigSchema::UI::FontPath},
			                               ConfigSchema::Legacy::UiFontPath,
			                               config.ui.fontPath);
			config.ui.clearColor = ReadColor(root, config.ui.clearColor);
			config.ui.fontScale = std::clamp(config.ui.fontScale, config.ui.fontScaleMin, config.ui.fontScaleMax);
			config.ui.fontScaleStep = std::clamp(config.ui.fontScaleStep, config.ui.fontScaleStepMin, config.ui.fontScaleStepMax);
		}

		bool LoadYamlFile(const Path &path, YAML::Node &root, std::string &error)
		{
			try
			{
				root = YAML::LoadFile(path.String());
				if (!root || root.IsNull())
					root = YAML::Node(YAML::NodeType::Map);
				if (!root.IsMap())
				{
					error = "YAML config root must be a mapping: " + path.String();
					return false;
				}
				return true;
			}
			catch (const YAML::Exception &exception)
			{
				error = exception.what();
				return false;
			}
		}

		bool EnsureParentDirectory(const Path &path, std::string &error)
		{
			if (path.parent_path().empty())
				return true;

			std::error_code directoryError;
			FileSystem::CreateDirectories(path.parent_path().Native(), directoryError);
			if (!directoryError)
				return true;

			error = directoryError.message();
			return false;
		}

		bool WriteEmitterToFile(const Path &path, const YAML::Emitter &emitter, std::string &error)
		{
			if (!emitter.good())
			{
				error = "Unable to emit YAML config";
				return false;
			}

			if (!EnsureParentDirectory(path, error))
				return false;

			std::ofstream stream(path.Native(), std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				error = "Unable to open YAML config for writing: " + path.String();
				return false;
			}

			stream << emitter.c_str() << '\n';
			return true;
		}

		void EmitColor(YAML::Emitter &emitter, const std::array<float, 4> &color)
		{
			emitter << YAML::Flow << YAML::BeginSeq;
			for (float component : color)
				emitter << component;
			emitter << YAML::EndSeq << YAML::Block;
		}

		bool WriteDefaultConfigFile(const Path &path, const ApplicationConfig &config, std::string &error)
		{
			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::Root::SchemaVersion << YAML::Value << ConfigManager::SchemaVersion;
			out << YAML::Key << ConfigSchema::Sections::Log << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::Log::Level << YAML::Value << LogLevelToString(config.log.level);
			out << YAML::Key << ConfigSchema::Log::ToFile << YAML::Value << config.log.toFile;
			out << YAML::Key << ConfigSchema::Log::FilePath << YAML::Value << config.log.filePath.String();
			out << YAML::EndMap;
			out << YAML::Key << ConfigSchema::Root::TraceEvents << YAML::Value << config.log.traceEvents;

			out << YAML::Key << ConfigSchema::Sections::Window << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::Window::Width << YAML::Value << config.window.width;
			out << YAML::Key << ConfigSchema::Window::Height << YAML::Value << config.window.height;
			out << YAML::Key << ConfigSchema::Window::Title << YAML::Value << config.window.title;
			out << YAML::EndMap;

			out << YAML::Key << ConfigSchema::Sections::UI << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::UI::FontScale << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::UI::FontScaleMin << YAML::Value << config.ui.fontScaleMin;
			out << YAML::Key << ConfigSchema::UI::FontScaleMax << YAML::Value << config.ui.fontScaleMax;
			out << YAML::EndMap;
			out << YAML::Key << ConfigSchema::UI::FontScaleStep << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::UI::FontScaleStepMin << YAML::Value << config.ui.fontScaleStepMin;
			out << YAML::Key << ConfigSchema::UI::FontScaleStepMax << YAML::Value << config.ui.fontScaleStepMax;
			out << YAML::Key << ConfigSchema::UI::FontScaleStepSliderMax << YAML::Value << config.ui.fontScaleStepSliderMax;
			out << YAML::EndMap;
			out << YAML::EndMap;

			out << YAML::Key << ConfigSchema::Sections::Appearance << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::Appearance::ClearColor << YAML::Value;
			EmitColor(out, config.ui.clearColor);
			out << YAML::EndMap;

			out << YAML::Key << ConfigSchema::Sections::Jobs << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::Jobs::DefaultWorkerThreads << YAML::Value << config.jobs.defaultWorkerThreadCount;
			out << YAML::Key << ConfigSchema::Jobs::ReserveUrgentWorker << YAML::Value << config.jobs.reserveUrgentWorker;
			out << YAML::EndMap;

			out << YAML::Key << ConfigSchema::Sections::EventQueue << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::EventQueue::InitialCapacity << YAML::Value << static_cast<unsigned long long>(config.eventQueue.initialCapacity);
			out << YAML::Key << ConfigSchema::EventQueue::GrowthStep << YAML::Value << static_cast<unsigned long long>(config.eventQueue.growthStep);
			out << YAML::EndMap;
			out << YAML::EndMap;

			return WriteEmitterToFile(path, out, error);
		}

		bool WriteUserSettingsFile(const Path &path, const ApplicationConfig &config, std::string &error)
		{
			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::Root::SchemaVersion << YAML::Value << ConfigManager::SchemaVersion;
			out << YAML::Key << ConfigSchema::Sections::UI << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::UI::Font << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::UI::FontPath << YAML::Value << config.ui.fontPath;
			out << YAML::EndMap;
			out << YAML::Key << ConfigSchema::UI::FontScale << YAML::Value << config.ui.fontScale;
			out << YAML::Key << ConfigSchema::UI::FontScaleStep << YAML::Value << config.ui.fontScaleStep;
			out << YAML::EndMap;
			out << YAML::Key << ConfigSchema::Sections::Appearance << YAML::Value << YAML::BeginMap;
			out << YAML::Key << ConfigSchema::Appearance::ClearColor << YAML::Value;
			EmitColor(out, config.ui.clearColor);
			out << YAML::EndMap;
			out << YAML::EndMap;

			return WriteEmitterToFile(path, out, error);
		}
	}

	ConfigManager::ConfigManager(Path configDirectory):
		m_ConfigDirectory(std::move(configDirectory)),
		m_Config(CreateDefaultConfig(m_ConfigDirectory))
	{
	}

	bool ConfigManager::Initialize(std::string &error)
	{
		std::error_code directoryError;
		FileSystem::CreateDirectories(m_ConfigDirectory.Native(), directoryError);
		if (directoryError)
		{
			error = directoryError.message();
			return false;
		}

		m_Config = CreateDefaultConfig(m_ConfigDirectory);
		if (!loadOrCreateDefaultConfig(error))
			return false;
		if (!loadOrCreateUserSettings(error))
			return false;

		m_Config.directory = m_ConfigDirectory;
		m_Config.layout.imGuiIniPath = GetLayoutPath(m_ConfigDirectory).String();
		m_Initialized = true;
		return true;
	}

	const Path &ConfigManager::GetConfigDirectory() const
	{
		return m_ConfigDirectory;
	}

	const ApplicationConfig &ConfigManager::GetConfig() const
	{
		return m_Config;
	}

	void ConfigManager::SetConfig(const ApplicationConfig &config)
	{
		m_Config = config;
		m_Config.directory = m_ConfigDirectory;
		m_Config.layout.imGuiIniPath = GetLayoutPath(m_ConfigDirectory).String();
	}

	void ConfigManager::SetUiConfig(const UIConfig &config)
	{
		m_Config.ui = config;
	}

	bool ConfigManager::SaveUserSettings(std::string &error) const
	{
		if (!ensureInitialized(error))
			return false;

		return ConfigYaml::WriteUserSettingsFile(GetUserSettingsPath(m_ConfigDirectory), m_Config, error);
	}

	void ConfigManager::ApplySpecification(ApplicationSpecification &specification) const
	{
		specification.logLevel = m_Config.log.level;
		specification.logToFile = m_Config.log.toFile;
		specification.logFilePath = m_Config.log.filePath;
		specification.traceEvents = m_Config.log.traceEvents;
	}

	ApplicationConfig ConfigManager::CreateDefaultConfig(const Path &configDirectory)
	{
		ApplicationConfig config;
		config.directory = configDirectory;
		config.layout.imGuiIniPath = GetLayoutPath(configDirectory).String();
		config.log.level = LogLevel::Info;
		config.log.toFile = true;
		config.log.filePath = Path::FromResolved(RuntimeDefaults::LogFilePath);
		config.log.traceEvents = false;
		config.window.width = RuntimeDefaults::WindowWidth;
		config.window.height = RuntimeDefaults::WindowHeight;
		config.window.title = RuntimeDefaults::WindowTitle;
		config.ui.fontScale = RuntimeDefaults::UiFontScale;
		config.ui.fontScaleStep = RuntimeDefaults::UiFontScaleStep;
		config.ui.fontPath = RuntimeDefaults::UiFontPath;
		config.ui.clearColor = {0.10f, 0.10f, 0.12f, 1.00f};
		config.ui.fontScaleMin = RuntimeDefaults::UiFontScaleMin;
		config.ui.fontScaleMax = RuntimeDefaults::UiFontScaleMax;
		config.ui.fontScaleStepMin = RuntimeDefaults::UiFontScaleStepMin;
		config.ui.fontScaleStepMax = RuntimeDefaults::UiFontScaleStepMax;
		config.ui.fontScaleStepSliderMax = RuntimeDefaults::UiFontScaleStepSliderMax;
		config.jobs.defaultWorkerThreadCount = RuntimeDefaults::JobsDefaultWorkerThreads;
		config.jobs.reserveUrgentWorker = RuntimeDefaults::JobsReserveUrgentWorker;
		config.eventQueue.initialCapacity = RuntimeDefaults::EventQueueInitialCapacity;
		config.eventQueue.growthStep = RuntimeDefaults::EventQueueGrowthStep;
		return config;
	}

	Path ConfigManager::GetDefaultConfigPath(const Path &configDirectory)
	{
		return configDirectory / Path(DefaultConfigFileName);
	}

	Path ConfigManager::GetUserSettingsPath(const Path &configDirectory)
	{
		return configDirectory / Path(UserSettingsFileName);
	}

	Path ConfigManager::GetLayoutPath(const Path &configDirectory)
	{
		return configDirectory / Path(LayoutFileName);
	}

	bool ConfigManager::ensureInitialized(std::string &error) const
	{
		if (m_Initialized)
			return true;

		error = "ConfigManager is not initialized";
		return false;
	}

	bool ConfigManager::loadOrCreateDefaultConfig(std::string &error)
	{
		const Path path = GetDefaultConfigPath(m_ConfigDirectory);
		if (!FileSystem::Exists(path.Native()))
			return ConfigYaml::WriteDefaultConfigFile(path, m_Config, error);

		YAML::Node root;
		if (!ConfigYaml::LoadYamlFile(path, root, error))
			return false;

		ConfigYaml::ApplyDefaultYaml(root, m_Config);
		return true;
	}

	bool ConfigManager::loadOrCreateUserSettings(std::string &error)
	{
		const Path path = GetUserSettingsPath(m_ConfigDirectory);
		if (!FileSystem::Exists(path.Native()))
			return ConfigYaml::WriteUserSettingsFile(path, m_Config, error);

		YAML::Node root;
		if (!ConfigYaml::LoadYamlFile(path, root, error))
			return false;

		ConfigYaml::ApplyUserYaml(root, m_Config);
		return true;
	}
} // namespace DefectStudio
