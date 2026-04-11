#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

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
		std::filesystem::path path;
		std::string error;
	};

	class ConfigManager
	{
	public:
		static constexpr std::string_view SchemaVersionKey = "schema_version";
		static constexpr std::string_view SchemaVersion = "1";
		static constexpr std::string_view DefaultConfigFileName = "default.yaml";
		static constexpr std::string_view UiSettingsFileName = "ui_settings.yaml";

		[[nodiscard]] static ConfigDocument CreateDefaultDocument();
		[[nodiscard]] static ConfigDocument CreateUiSettingsDocument();

		[[nodiscard]] static ConfigLoadResult LoadFile(const std::filesystem::path &path);
		static bool SaveFile(const std::filesystem::path &path, const ConfigDocument &document, std::string &error);

		[[nodiscard]] static std::string GetString(const ConfigDocument &document, std::string_view key, std::string_view fallback = {});
		[[nodiscard]] static bool GetBool(const ConfigDocument &document, std::string_view key, bool fallback = false);
		[[nodiscard]] static int GetInt(const ConfigDocument &document, std::string_view key, int fallback = 0);
		[[nodiscard]] static double GetDouble(const ConfigDocument &document, std::string_view key, double fallback = 0.0);

		[[nodiscard]] static std::filesystem::path GetDefaultConfigPath(const std::filesystem::path &configDirectory);
		[[nodiscard]] static std::filesystem::path GetUiSettingsPath(const std::filesystem::path &configDirectory);

	private:
		[[nodiscard]] static ConfigFormat DetectFormat(const std::filesystem::path &path, const std::string &text);
	};
} // namespace DefectStudio