#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	enum class ConfigFormat
	{
		Unknown,
		Json,
		Yaml
	};

	struct ConfigDocument
	{
		std::map<std::string, std::string> values;

		[[nodiscard]] bool Has(std::string_view key) const;
		[[nodiscard]] std::string Get(std::string_view key, std::string_view fallback = {}) const;
		void Set(std::string key, std::string value);
	};

	struct ConfigLoadResult
	{
		bool success = false;
		ConfigFormat format = ConfigFormat::Unknown;
		ConfigDocument document;
		Path path;
		std::string error;
	};

	class ConfigManager
	{
	public:
		static constexpr std::string_view SchemaVersionKey = "schema_version";
		static constexpr std::string_view SchemaVersion = "1";
		static constexpr std::string_view DefaultConfigFileName = "default.yaml";
		static constexpr std::string_view UiSettingsFileName = "ui_settings.yaml";

		explicit ConfigManager(Path configDirectory);

		[[nodiscard]] bool Initialize(std::string &error);

		[[nodiscard]] const Path &GetConfigDirectory() const;
		[[nodiscard]] const ConfigDocument &GetDefaultDocument() const;
		[[nodiscard]] const ConfigDocument &GetUiSettingsDocument() const;

		void SetUiValue(std::string key, std::string value);
		[[nodiscard]] bool SaveUiSettings(std::string &error) const;

		[[nodiscard]] std::string GetDefaultString(std::string_view key, std::string_view fallback = {}) const;
		[[nodiscard]] bool GetDefaultBool(std::string_view key, bool fallback = false) const;
		[[nodiscard]] int GetDefaultInt(std::string_view key, int fallback = 0) const;
		[[nodiscard]] double GetDefaultDouble(std::string_view key, double fallback = 0.0) const;

		[[nodiscard]] std::string GetUiString(std::string_view key, std::string_view fallback = {}) const;
		[[nodiscard]] bool GetUiBool(std::string_view key, bool fallback = false) const;
		[[nodiscard]] int GetUiInt(std::string_view key, int fallback = 0) const;
		[[nodiscard]] double GetUiDouble(std::string_view key, double fallback = 0.0) const;

		[[nodiscard]] static ConfigDocument CreateDefaultDocument();
		[[nodiscard]] static ConfigDocument CreateUiSettingsDocument();

		[[nodiscard]] static ConfigLoadResult LoadFile(const Path &path);
		static bool SaveFile(const Path &path, const ConfigDocument &document, std::string &error);

		[[nodiscard]] static std::string GetString(const ConfigDocument &document, std::string_view key, std::string_view fallback = {});
		[[nodiscard]] static bool GetBool(const ConfigDocument &document, std::string_view key, bool fallback = false);
		[[nodiscard]] static int GetInt(const ConfigDocument &document, std::string_view key, int fallback = 0);
		[[nodiscard]] static double GetDouble(const ConfigDocument &document, std::string_view key, double fallback = 0.0);

		[[nodiscard]] static Path GetDefaultConfigPath(const Path &configDirectory);
		[[nodiscard]] static Path GetUiSettingsPath(const Path &configDirectory);

	private:
		[[nodiscard]] bool LoadOrCreateDocument(const Path &path,
		                                       const ConfigDocument &fallback,
		                                       ConfigDocument &target,
		                                       std::string &error) const;

		[[nodiscard]] bool EnsureInitialized(std::string &error) const;

		[[nodiscard]] static ConfigFormat DetectFormat(const Path &path, const std::string &text);

	private:
		Path m_ConfigDirectory;
		ConfigDocument m_DefaultDocument;
		ConfigDocument m_UiSettingsDocument;
		bool m_Initialized = false;
	};
} // namespace DefectStudio