#include "Core/dspch.hpp"

#include "App/ConfigManager.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <sstream>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace DefectStudio
{
	struct JsonValue;

	struct YamlNode;

	static std::string TrimCopy(std::string_view value)
	{
		const auto first = value.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos)
			return {};

		const auto last = value.find_last_not_of(" \t\r\n");
		return std::string(value.substr(first, last - first + 1));
	}

	static std::string ToLowerCopy(std::string_view value)
	{
		std::string result(value);
		for (char &character : result)
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		return result;
	}

	static std::string JsonValueToString(const nlohmann::json &value)
	{
		if (value.is_string())
			return value.get<std::string>();
		if (value.is_boolean())
			return value.get<bool>() ? "true" : "false";
		if (value.is_number_integer())
			return std::to_string(value.get<long long>());
		if (value.is_number_unsigned())
			return std::to_string(value.get<unsigned long long>());
		if (value.is_number_float())
			return value.dump();
		if (value.is_null())
			return "null";

		return value.dump();
	}

	static void FlattenJson(const nlohmann::json &value, const std::string &prefix, ConfigDocument &document, std::string &error)
	{
		if (value.is_object())
		{
			for (const auto &entry : value.items())
			{
				const std::string childKey = prefix.empty() ? entry.key() : prefix + "." + entry.key();
				FlattenJson(entry.value(), childKey, document, error);
				if (!error.empty())
					return;
			}
			return;
		}

		if (value.is_array())
		{
			error = "JSON arrays are not supported in config files yet";
			return;
		}

		document.Set(prefix, JsonValueToString(value));
	}

	static void FlattenYaml(const YAML::Node &node, const std::string &prefix, ConfigDocument &document, std::string &error)
	{
		if (node.IsMap())
		{
			for (const auto &entry : node)
			{
				const std::string key = entry.first.as<std::string>();
				const std::string childKey = prefix.empty() ? key : prefix + "." + key;
				FlattenYaml(entry.second, childKey, document, error);
				if (!error.empty())
					return;
			}
			return;
		}

		if (node.IsSequence())
		{
			error = "YAML sequences are not supported in config files yet";
			return;
		}

		if (!node.IsScalar())
		{
			error = "Unsupported YAML node kind in config file";
			return;
		}

		document.Set(prefix, node.Scalar());
	}

	static bool LoadJsonDocument(const std::filesystem::path &path, ConfigDocument &document, std::string &error)
	{
		std::ifstream stream(path, std::ios::binary);
		if (!stream)
		{
			error = "Unable to open config file";
			return false;
		}

		try
		{
			nlohmann::json parsed = nlohmann::json::parse(stream, nullptr, true, true);
			if (!parsed.is_object())
			{
				error = "JSON config root must be an object";
				return false;
			}

			FlattenJson(parsed, std::string(), document, error);
			return error.empty();
		}
		catch (const nlohmann::json::exception &exception)
		{
			error = exception.what();
			return false;
		}
	}

	static bool LoadYamlDocument(const std::filesystem::path &path, ConfigDocument &document, std::string &error)
	{
		try
		{
			const YAML::Node parsed = YAML::LoadFile(path.string());
			if (!parsed || !parsed.IsMap())
			{
				error = "YAML config root must be a mapping";
				return false;
			}

			FlattenYaml(parsed, std::string(), document, error);
			return error.empty();
		}
		catch (const YAML::Exception &exception)
		{
			error = exception.what();
			return false;
		}
	}

	static bool WriteJsonFile(const std::filesystem::path &path, const ConfigDocument &document, std::string &error)
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			error = "Unable to open JSON config file for writing";
			return false;
		}

		nlohmann::json output = document.values;

		stream << output.dump(2) << '\n';
		return true;
	}

	static bool WriteYamlFile(const std::filesystem::path &path, const ConfigDocument &document, std::string &error)
	{
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			error = "Unable to open YAML config file for writing";
			return false;
		}

		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		for (const auto &[key, value] : document.values)
			emitter << YAML::Key << key << YAML::Value << value;
		emitter << YAML::EndMap;

		if (!emitter.good())
		{
			error = "Unable to emit YAML config";
			return false;
		}

		stream << emitter.c_str() << '\n';
		return true;
	}

	static ConfigFormat DetectFormatImpl(const std::filesystem::path &path, std::string_view text)
	{
		const std::string extension = ToLowerCopy(path.extension().string());
		if (extension == ".json")
			return ConfigFormat::Json;

		if (extension == ".yaml" || extension == ".yml")
			return ConfigFormat::Yaml;

		const std::string_view trimmed = std::string_view(TrimCopy(text));
		if (!trimmed.empty() && trimmed.front() == '{')
			return ConfigFormat::Json;

		if (!trimmed.empty())
			return ConfigFormat::Yaml;

		return ConfigFormat::Unknown;
	}

	[[nodiscard]] bool ConfigDocument::Has(std::string_view key) const
	{
		return values.find(std::string(key)) != values.end();
	}

	[[nodiscard]] std::string ConfigDocument::Get(std::string_view key, std::string_view fallback) const
	{
		const auto it = values.find(std::string(key));
		if (it == values.end())
			return std::string(fallback);

		return it->second;
	}

	void ConfigDocument::Set(std::string key, std::string value)
	{
		values[std::move(key)] = std::move(value);
	}

	[[nodiscard]] ConfigDocument ConfigManager::CreateDefaultDocument()
	{
		ConfigDocument document;
		document.Set(std::string(SchemaVersionKey), std::string(SchemaVersion));
		document.Set("log.level", "info");
		document.Set("trace_events", "false");
		document.Set("window.width", "1280");
		document.Set("window.height", "720");
		document.Set("window.title", "DefectStudio");
		return document;
	}

	[[nodiscard]] ConfigDocument ConfigManager::CreateUiSettingsDocument()
	{
		ConfigDocument document;
		document.Set(std::string(SchemaVersionKey), std::string(SchemaVersion));
		document.Set("show_demo_window", "true");
		document.Set("reset_layout", "false");
		document.Set("font_scale", "1.0");
		document.Set("clear_color", "0.10, 0.10, 0.12, 1.00");
		return document;
	}

	[[nodiscard]] ConfigLoadResult ConfigManager::LoadFile(const std::filesystem::path &path)
	{
		ConfigLoadResult result;
		result.path = path;

		std::string error;
		const ConfigFormat format = DetectFormat(path, path.string());
		result.format = format;

		if (format == ConfigFormat::Json)
			result.success = LoadJsonDocument(path, result.document, error);
		else if (format == ConfigFormat::Yaml)
			result.success = LoadYamlDocument(path, result.document, error);
		else
		{
			error = "Unable to detect config file format";
			result.success = false;
		}

		result.error = std::move(error);
		return result;
	}

	[[nodiscard]] ConfigFormat ConfigManager::DetectFormat(const std::filesystem::path &path, const std::string &text)
	{
		return DetectFormatImpl(path, text);
	}

	bool ConfigManager::SaveFile(const std::filesystem::path &path, const ConfigDocument &document, std::string &error)
	{
		if (!path.parent_path().empty())
		{
			std::error_code directoryError;
			std::filesystem::create_directories(path.parent_path(), directoryError);
			if (directoryError)
			{
				error = directoryError.message();
				return false;
			}
		}

		const ConfigFormat format = DetectFormatImpl(path, path.string());
		if (format == ConfigFormat::Json)
			return WriteJsonFile(path, document, error);

		return WriteYamlFile(path, document, error);
	}

	[[nodiscard]] std::string ConfigManager::GetString(const ConfigDocument &document, std::string_view key,
		                                              std::string_view fallback)
	{
		return document.Get(key, fallback);
	}

	[[nodiscard]] bool ConfigManager::GetBool(const ConfigDocument &document, std::string_view key, bool fallback)
	{
		const std::string value = ToLowerCopy(document.Get(key, fallback ? "true" : "false"));
		if (value == "true" || value == "1" || value == "yes" || value == "on")
			return true;

		if (value == "false" || value == "0" || value == "no" || value == "off")
			return false;

		return fallback;
	}

	[[nodiscard]] int ConfigManager::GetInt(const ConfigDocument &document, std::string_view key, int fallback)
	{
		const std::string value = document.Get(key);
		if (value.empty())
			return fallback;

		char *end = nullptr;
		const long parsed = std::strtol(value.c_str(), &end, 10);
		if (end == value.c_str() || *end != '\0')
			return fallback;

		return static_cast<int>(parsed);
	}

	[[nodiscard]] double ConfigManager::GetDouble(const ConfigDocument &document, std::string_view key, double fallback)
	{
		const std::string value = document.Get(key);
		if (value.empty())
			return fallback;

		std::istringstream stream(value);
		stream.imbue(std::locale::classic());
		double parsed = 0.0;
		stream >> parsed;
		if (stream.fail())
			return fallback;

		stream >> std::ws;
		if (!stream.eof())
			return fallback;

		return parsed;
	}

	[[nodiscard]] std::filesystem::path ConfigManager::GetDefaultConfigPath(const std::filesystem::path &configDirectory)
	{
		return configDirectory / std::filesystem::path(DefaultConfigFileName);
	}

	[[nodiscard]] std::filesystem::path ConfigManager::GetUiSettingsPath(const std::filesystem::path &configDirectory)
	{
		return configDirectory / std::filesystem::path(UiSettingsFileName);
	}
} // namespace DefectStudio