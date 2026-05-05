#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <locale>
#include <sstream>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "App/Managers/ConfigManager.hpp"
#include "App/Serialization/YamlCodecFacade.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/RuntimeTuning.hpp"
#include "IO/TextFileIO.hpp"

namespace DefectStudio
{


	namespace ConfigPaths
	{
		Path NormalizeConfigDirectory(const Path &configDirectory)
		{
			if (!configDirectory.Empty())
				return Path::FromResolved(configDirectory.Native());

			return Path::FromResolved(FileSystem::CurrentPath() / "install" / "app" / "config");
		}

		bool IsPortableAppConfigDirectory(const Path &configDirectory)
		{
			const FilePath path = configDirectory.Native().lexically_normal();
			return path.filename() == "config"
				&& path.parent_path().filename() == "app"
				&& path.parent_path().parent_path().filename() == "install";
		}

		ApplicationPaths Build(const Path &configDirectory)
		{
			ApplicationPaths paths;
			paths.appConfigDirectory = NormalizeConfigDirectory(configDirectory);

			if (IsPortableAppConfigDirectory(paths.appConfigDirectory))
			{
				paths.installRoot = Path::FromResolved(paths.appConfigDirectory.Native().parent_path().parent_path());
				const Path appRoot = Path::FromResolved(paths.installRoot.Native() / "app");
				const Path userRoot = Path::FromResolved(paths.installRoot.Native() / "users" / "default");
				paths.userConfigDirectory = Path::FromResolved(userRoot.Native() / "config");
				paths.profilesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "profiles");
				paths.themesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "themes");
				paths.layoutsDirectory = Path::FromResolved(userRoot.Native() / "layouts");
				paths.exportsDirectory = Path::FromResolved(userRoot.Native() / "exports");
				paths.assetsDirectory = Path::FromResolved(appRoot.Native() / "assets");
				paths.fontsDirectory = Path::FromResolved(paths.assetsDirectory.Native() / "fonts");
				return paths;
			}

			paths.installRoot = Path::FromResolved(paths.appConfigDirectory.Native().parent_path());
			paths.userConfigDirectory = paths.appConfigDirectory;
			paths.profilesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "profiles");
			paths.themesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "themes");
			paths.layoutsDirectory = paths.userConfigDirectory;
			paths.exportsDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "exports");
			paths.assetsDirectory = Path::FromResolved(paths.installRoot.Native() / "assets");
			paths.fontsDirectory = Path::FromResolved(paths.assetsDirectory.Native() / "fonts");
			return paths;
		}

		bool EnsureDirectory(const Path &directory, std::string &error)
		{
			std::error_code directoryError;
			FileSystem::CreateDirectories(directory.Native(), directoryError);
			if (!directoryError)
				return true;

			error = directoryError.message();
			return false;
		}

		bool EnsurePortableDirectories(const ApplicationPaths &paths, std::string &error)
		{
			return EnsureDirectory(paths.appConfigDirectory, error)
				&& EnsureDirectory(paths.userConfigDirectory, error)
				&& EnsureDirectory(paths.profilesDirectory, error)
				&& EnsureDirectory(paths.themesDirectory, error)
				&& EnsureDirectory(paths.layoutsDirectory, error)
				&& EnsureDirectory(paths.exportsDirectory, error)
				&& EnsureDirectory(paths.assetsDirectory, error)
				&& EnsureDirectory(paths.fontsDirectory, error);
		}

		std::vector<Path> ListFilesByExtension(const Path &directory, std::string_view extension)
		{
			std::vector<Path> result;
			std::error_code error;
			if (!std::filesystem::exists(directory.Native(), error) || error)
				return result;

			for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory.Native(), error))
			{
				if (error)
					break;
				if (!entry.is_regular_file(error) || error)
				{
					error.clear();
					continue;
				}

				std::string fileExtension = entry.path().extension().string();
				std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), [](unsigned char character) {
					return static_cast<char>(std::tolower(character));
				});
				if (fileExtension == extension)
					result.push_back(Path::FromResolved(entry.path()));
			}

			std::sort(result.begin(), result.end(), [](const Path &left, const Path &right) {
				return left.String() < right.String();
			});
			return result;
		}
	}

	ConfigManager::ConfigManager(Path configDirectory):
		m_ConfigDirectory(std::move(configDirectory)),
		m_Config(CreateDefaultConfig(m_ConfigDirectory))
	{
	}

	bool ConfigManager::Initialize(std::string &error)
	{
		m_ConfigDirectory = ConfigPaths::NormalizeConfigDirectory(m_ConfigDirectory);
		m_Config = CreateDefaultConfig(m_ConfigDirectory);
		if (!ConfigPaths::EnsurePortableDirectories(m_Config.paths, error))
			return false;

		if (!loadOrCreateDefaultConfig(error))
			return false;
		if (!loadOrCreateUserSettings(error))
			return false;

		m_Config.directory = m_ConfigDirectory;
		m_Config.paths = ResolvePortablePaths(m_ConfigDirectory);
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

	const ApplicationPaths &ConfigManager::GetPaths() const
	{
		return m_Config.paths;
	}

	void ConfigManager::SetConfig(const ApplicationConfig &config)
	{
		m_Config = config;
		m_Config.directory = m_ConfigDirectory;
		m_Config.paths = ResolvePortablePaths(m_ConfigDirectory);
		if (m_Config.layout.imGuiIniPath.empty())
			m_Config.layout.imGuiIniPath = GetLayoutPath(m_ConfigDirectory).String();
	}

	void ConfigManager::SetUiConfig(const UIConfig &config)
	{
		m_Config.ui = config;
	}

	void ConfigManager::SetAppearanceConfig(const AppearanceConfig &config)
	{
		m_Config.appearance = config;
	}

	std::vector<Path> ConfigManager::ListYamlFiles(const Path &directory) const
	{
		std::vector<Path> result = ConfigPaths::ListFilesByExtension(directory, ".yaml");
		std::vector<Path> ymlFiles = ConfigPaths::ListFilesByExtension(directory, ".yml");
		result.insert(result.end(), ymlFiles.begin(), ymlFiles.end());
		std::sort(result.begin(), result.end(), [](const Path &left, const Path &right) {
			return left.String() < right.String();
		});
		return result;
	}

	std::vector<Path> ConfigManager::ListIniFiles(const Path &directory) const
	{
		return ConfigPaths::ListFilesByExtension(directory, ".ini");
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
		config.directory = ConfigPaths::NormalizeConfigDirectory(configDirectory);
		config.paths = ResolvePortablePaths(config.directory);
		config.layout.imGuiIniPath = GetLayoutPath(configDirectory).String();
		config.log.level = LogLevel::Info;
		config.log.toFile = true;
		config.log.filePath = Path::FromResolved(RuntimeDefaults::LogFilePath);
		config.log.traceEvents = false;
		config.window.x = RuntimeDefaults::WindowPosX;
		config.window.y = RuntimeDefaults::WindowPosY;
		config.window.width = RuntimeDefaults::WindowWidth;
		config.window.height = RuntimeDefaults::WindowHeight;
		config.window.maximized = RuntimeDefaults::WindowMaximized;
		config.window.title = RuntimeDefaults::WindowTitle;
		config.ui.fontScale = RuntimeDefaults::UiFontScale;
		config.ui.fontScaleStep = RuntimeDefaults::UiFontScaleStep;
		config.ui.fontPath = RuntimeDefaults::UiFontPath;
		config.ui.settingsPreviewEnabled = RuntimeDefaults::UiSettingsPreviewEnabled;
		config.ui.settingsAutoSaveOnPreview = RuntimeDefaults::UiSettingsAutoSaveOnPreview;
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

	ApplicationPaths ConfigManager::ResolvePortablePaths(const Path &configDirectory)
	{
		return ConfigPaths::Build(configDirectory);
	}

	Path ConfigManager::GetDefaultConfigPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).appConfigDirectory / Path(DefaultConfigFileName);
	}

	Path ConfigManager::GetUserSettingsPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).userConfigDirectory / Path(UserSettingsFileName);
	}

	Path ConfigManager::GetKeybindingsPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).userConfigDirectory / Path(KeybindingsFileName);
	}

	Path ConfigManager::GetLayoutPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).layoutsDirectory / Path(LayoutFileName);
	}

	Path ConfigManager::GetProfilesDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).profilesDirectory;
	}

	Path ConfigManager::GetThemesDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).themesDirectory;
	}

	Path ConfigManager::GetLayoutsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).layoutsDirectory;
	}

	Path ConfigManager::GetExportsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).exportsDirectory;
	}

	Path ConfigManager::GetAssetsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).assetsDirectory;
	}

	Path ConfigManager::GetFontsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).fontsDirectory;
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
		{
			std::string text;
			if (!YamlCodecFacade::Default().SerializeDefaultConfig(m_Config, text, error))
				return false;
			return TextFileIO::Save(path, text, error);
		}

		std::string text;
		if (!TextFileIO::Load(path, text, error))
			return false;

		return YamlCodecFacade::Default().DeserializeDefaultConfig(text, m_Config, error);
	}

	bool ConfigManager::loadOrCreateUserSettings(std::string &error)
	{
		const Path path = GetUserSettingsPath(m_ConfigDirectory);
		if (!FileSystem::Exists(path.Native()))
		{
			const Path legacyAppSettingsPath = m_Config.paths.appConfigDirectory / Path(UserSettingsFileName);
			if (legacyAppSettingsPath != path && FileSystem::Exists(legacyAppSettingsPath.Native()))
			{
				std::string legacyText;
				if (!TextFileIO::Load(legacyAppSettingsPath, legacyText, error))
					return false;
				if (!YamlCodecFacade::Default().DeserializeUserSettings(legacyText, m_Config, error))
					return false;
			}

			std::string text;
			if (!YamlCodecFacade::Default().SerializeUserSettings(m_Config, text, error))
				return false;
			return TextFileIO::Save(path, text, error);
		}

		std::string text;
		if (!TextFileIO::Load(path, text, error))
			return false;

		return YamlCodecFacade::Default().DeserializeUserSettings(text, m_Config, error);
	}
} // namespace DefectStudio

