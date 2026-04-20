#include "Core/dspch.hpp"



#include <nlohmann/json.hpp>
#include <optional>
#include <yaml-cpp/yaml.h>

#include "Core/Utils/Path.hpp"
#include "App/ConfigManager.hpp"
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
			std::string joined;
			for (const auto &item : node)
			{
				std::string serialized;
				if (item.IsScalar())
					serialized = item.Scalar();
				else if (item.IsNull())
					serialized = "null";
				else
				{
					YAML::Emitter itemEmitter;
					itemEmitter << item;
					if (!itemEmitter.good())
					{
						error = "Unsupported YAML sequence item in config file";
						return;
					}
					serialized = TrimCopy(itemEmitter.c_str());
				}

				if (!joined.empty())
					joined += ", ";
				joined += serialized;
			}

			document.Set(prefix, joined);
			return;
		}

		if (!node.IsScalar())
		{
			error = "Unsupported YAML node kind in config file";
			return;
		}

		document.Set(prefix, node.Scalar());
	}

	static bool LoadJsonDocument(const Path &path, ConfigDocument &document, std::string &error)
	{
		std::ifstream stream(path.Native(), std::ios::binary);
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

	static bool LoadYamlDocument(const Path &path, ConfigDocument &document, std::string &error)
	{
		try
		{
			const YAML::Node parsed = YAML::LoadFile(path.String());
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

	static bool WriteJsonFile(const Path &path, const ConfigDocument &document, std::string &error)
	{
		std::ofstream stream(path.Native(), std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			error = "Unable to open JSON config file for writing";
			return false;
		}

		nlohmann::json output = document.values;

		stream << output.dump(2) << '\n';
		return true;
	}

	static bool WriteYamlFile(const Path &path, const ConfigDocument &document, std::string &error)
	{
		std::ofstream stream(path.Native(), std::ios::binary | std::ios::trunc);
		if (!stream)
		{
			error = "Unable to open YAML config file for writing";
			return false;
		}

		struct YamlTreeNode
		{
			std::map<std::string, YamlTreeNode> children;
			std::optional<std::string> value;
		};

		auto insertByDottedPath = [](YamlTreeNode &root, const std::string &dottedKey, const std::string &value,
		                            std::string &insertError) {
			if (dottedKey.empty())
			{
				insertError = "Empty key is not allowed in YAML config";
				return;
			}

			YamlTreeNode *current = &root;
			std::size_t segmentBegin = 0;
			while (true)
			{
				const std::size_t separator = dottedKey.find('.', segmentBegin);
				const std::string segment = dottedKey.substr(
					segmentBegin,
					separator == std::string::npos ? std::string::npos : separator - segmentBegin);

				if (segment.empty())
				{
					insertError = "Invalid dotted path in YAML config key: " + dottedKey;
					return;
				}

				if (separator == std::string::npos)
				{
					YamlTreeNode &leaf = current->children[segment];
					if (!leaf.children.empty())
					{
						insertError = "Conflicting YAML config key path: " + dottedKey;
						return;
					}
					leaf.value = value;
					return;
				}

				YamlTreeNode &child = current->children[segment];
				if (child.value.has_value())
				{
					insertError = "Conflicting YAML config key path: " + dottedKey;
					return;
				}

				current = &child;
				segmentBegin = separator + 1;
			}
		};

		auto emitTree = [](const YamlTreeNode &node, YAML::Emitter &emitter, const auto &emitRef) -> void {
			emitter << YAML::BeginMap;
			for (const auto &[key, child] : node.children)
			{
				emitter << YAML::Key << key << YAML::Value;
				if (!child.children.empty())
					emitRef(child, emitter, emitRef);
				else
					emitter << child.value.value_or(std::string());
			}
			emitter << YAML::EndMap;
		};

		YamlTreeNode root;
		for (const auto &[key, value] : document.values)
		{
			insertByDottedPath(root, key, value, error);
			if (!error.empty())
				return false;
		}

		YAML::Emitter emitter;
		emitTree(root, emitter, emitTree);

		if (!emitter.good())
		{
			error = "Unable to emit YAML config";
			return false;
		}

		stream << emitter.c_str() << '\n';
		return true;
	}

	static ConfigFormat DetectFormatImpl(const Path &path, std::string_view text)
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

	ConfigManager::ConfigManager(Path configDirectory):
		m_ConfigDirectory(std::move(configDirectory))
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

		if (!LoadOrCreateDocument(GetDefaultConfigPath(m_ConfigDirectory), CreateDefaultDocument(), m_DefaultDocument, error))
			return false;

		if (!LoadOrCreateDocument(GetUiSettingsPath(m_ConfigDirectory), CreateUiSettingsDocument(), m_UiSettingsDocument, error))
			return false;

		m_Initialized = true;
		return true;
	}

	const Path &ConfigManager::GetConfigDirectory() const
	{
		return m_ConfigDirectory;
	}

	const ConfigDocument &ConfigManager::GetDefaultDocument() const
	{
		return m_DefaultDocument;
	}

	const ConfigDocument &ConfigManager::GetUiSettingsDocument() const
	{
		return m_UiSettingsDocument;
	}

	void ConfigManager::SetUiValue(std::string key, std::string value)
	{
		m_UiSettingsDocument.Set(std::move(key), std::move(value));
	}

	bool ConfigManager::SaveUiSettings(std::string &error) const
	{
		if (!EnsureInitialized(error))
			return false;

		return SaveFile(GetUiSettingsPath(m_ConfigDirectory), m_UiSettingsDocument, error);
	}

	std::string ConfigManager::GetDefaultString(std::string_view key, std::string_view fallback) const
	{
		return GetString(m_DefaultDocument, key, fallback);
	}

	bool ConfigManager::GetDefaultBool(std::string_view key, bool fallback) const
	{
		return GetBool(m_DefaultDocument, key, fallback);
	}

	int ConfigManager::GetDefaultInt(std::string_view key, int fallback) const
	{
		return GetInt(m_DefaultDocument, key, fallback);
	}

	double ConfigManager::GetDefaultDouble(std::string_view key, double fallback) const
	{
		return GetDouble(m_DefaultDocument, key, fallback);
	}

	std::string ConfigManager::GetUiString(std::string_view key, std::string_view fallback) const
	{
		return GetString(m_UiSettingsDocument, key, fallback);
	}

	bool ConfigManager::GetUiBool(std::string_view key, bool fallback) const
	{
		return GetBool(m_UiSettingsDocument, key, fallback);
	}

	int ConfigManager::GetUiInt(std::string_view key, int fallback) const
	{
		return GetInt(m_UiSettingsDocument, key, fallback);
	}

	double ConfigManager::GetUiDouble(std::string_view key, double fallback) const
	{
		return GetDouble(m_UiSettingsDocument, key, fallback);
	}

	bool ConfigManager::LoadOrCreateDocument(const Path &path,
	                                        const ConfigDocument &fallback,
	                                        ConfigDocument &target,
	                                        std::string &error) const
	{
		if (FileSystem::Exists(path.Native()))
		{
			const auto result = LoadFile(path);
			if (result.success)
			{
				target = result.document;
				return true;
			}
		}

		target = fallback;
		return SaveFile(path, target, error);
	}

	bool ConfigManager::EnsureInitialized(std::string &error) const
	{
		if (m_Initialized)
			return true;

		error = "ConfigManager is not initialized";
		return false;
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
		document.Set("font_scale_step", "0.10");
		document.Set("clear_color", "0.10, 0.10, 0.12, 1.00");
		return document;
	}

	[[nodiscard]] ConfigLoadResult ConfigManager::LoadFile(const Path &path)
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

	[[nodiscard]] ConfigFormat ConfigManager::DetectFormat(const Path &path, const std::string &text)
	{
		return DetectFormatImpl(path, text);
	}

	bool ConfigManager::SaveFile(const Path &path, const ConfigDocument &document, std::string &error)
	{
		if (!path.parent_path().empty())
		{
			std::error_code directoryError;
			FileSystem::CreateDirectories(path.parent_path().Native(), directoryError);
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

	[[nodiscard]] Path ConfigManager::GetDefaultConfigPath(const Path &configDirectory)
	{
		return configDirectory / Path(DefaultConfigFileName);
	}

	[[nodiscard]] Path ConfigManager::GetUiSettingsPath(const Path &configDirectory)
	{
		return configDirectory / Path(UiSettingsFileName);
	}
} // namespace DefectStudio