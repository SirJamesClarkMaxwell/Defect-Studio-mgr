#pragma once

#include <string>
#include <vector>

#include "App/ApplicationState.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	class ConfigManager
	{
	public:
		static constexpr const char *SchemaVersion = "1";
		static constexpr const char *DefaultConfigFileName = "default.yaml";
		static constexpr const char *UserSettingsFileName = "ui_settings.yaml";
		static constexpr const char *KeybindingsFileName = "keybindings.yaml";
		static constexpr const char *LayoutFileName = "imgui.ini";

		explicit ConfigManager(Path configDirectory);

		[[nodiscard]] bool Initialize(std::string &error);

		[[nodiscard]] const Path &GetConfigDirectory() const;
		[[nodiscard]] const ApplicationConfig &GetConfig() const;
		[[nodiscard]] const ApplicationPaths &GetPaths() const;

		void SetConfig(const ApplicationConfig &config);
		void SetUiConfig(const UIConfig &config);
		void SetAppearanceConfig(const AppearanceConfig &config);
		[[nodiscard]] std::vector<Path> ListYamlFiles(const Path &directory) const;
		[[nodiscard]] std::vector<Path> ListIniFiles(const Path &directory) const;

		void ApplySpecification(ApplicationSpecification &specification) const;

		[[nodiscard]] static ApplicationConfig CreateDefaultConfig(const Path &configDirectory = Path{});
		[[nodiscard]] static ApplicationPaths ResolvePortablePaths(const Path &configDirectory);
		[[nodiscard]] static Path GetDefaultConfigPath(const Path &configDirectory);
		[[nodiscard]] static Path GetUserSettingsPath(const Path &configDirectory);
		[[nodiscard]] static Path GetKeybindingsPath(const Path &configDirectory);
		[[nodiscard]] static Path GetLayoutPath(const Path &configDirectory);
		[[nodiscard]] static Path GetProfilesDirectory(const Path &configDirectory);
		[[nodiscard]] static Path GetThemesDirectory(const Path &configDirectory);
		[[nodiscard]] static Path GetLayoutsDirectory(const Path &configDirectory);
		[[nodiscard]] static Path GetExportsDirectory(const Path &configDirectory);
		[[nodiscard]] static Path GetAssetsDirectory(const Path &configDirectory);
		[[nodiscard]] static Path GetFontsDirectory(const Path &configDirectory);

	private:
		[[nodiscard]] bool ensureInitialized(std::string &error) const;
		[[nodiscard]] bool loadOrCreateDefaultConfig(std::string &error);
		[[nodiscard]] bool loadOrCreateUserSettings(std::string &error);

	private:
		Path m_ConfigDirectory;
		ApplicationConfig m_Config;
		bool m_Initialized = false;
	};
} // namespace DefectStudio
