#pragma once

#include <string>

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
		static constexpr const char *LayoutFileName = "imgui.ini";

		explicit ConfigManager(Path configDirectory);

		[[nodiscard]] bool Initialize(std::string &error);

		[[nodiscard]] const Path &GetConfigDirectory() const;
		[[nodiscard]] const ApplicationConfig &GetConfig() const;

		void SetConfig(const ApplicationConfig &config);
		void SetUiConfig(const UIConfig &config);
		[[nodiscard]] bool SaveUserSettings(std::string &error) const;

		void ApplySpecification(ApplicationSpecification &specification) const;

		[[nodiscard]] static ApplicationConfig CreateDefaultConfig(const Path &configDirectory = Path{});
		[[nodiscard]] static Path GetDefaultConfigPath(const Path &configDirectory);
		[[nodiscard]] static Path GetUserSettingsPath(const Path &configDirectory);
		[[nodiscard]] static Path GetLayoutPath(const Path &configDirectory);

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
