#include "Core/dspch.hpp"

#include <algorithm>
#include <cctype>

#include "App/ConfigManager.hpp"
#include "App/ConfigProfileStore.hpp"

namespace DefectStudio
{
	ConfigProfileStore::ConfigProfileStore(WeakRef<ConfigManager> configManager)
		: m_ConfigManager(std::move(configManager))
	{
	}

	void ConfigProfileStore::Bind(WeakRef<ConfigManager> configManager)
	{
		m_ConfigManager = std::move(configManager);
	}

	const std::vector<ConfigProfileEntry> &ConfigProfileStore::Refresh()
	{
		m_Entries.clear();
		auto manager = m_ConfigManager.lock();
		if (manager == nullptr)
			return m_Entries;
		const Path profileDirectoryPath = manager->GetPaths().profilesDirectory;
		for (const Path &path : manager->ListYamlFiles(profileDirectoryPath))
		{
			ConfigProfileEntry entry;
			entry.path = path;
			entry.name = path.Native().stem().string();
			m_Entries.push_back(std::move(entry));
		}

		std::sort(m_Entries.begin(), m_Entries.end(), [](const auto &left, const auto &right) {
			return left.name < right.name;
		});
		return m_Entries;
	}

	const std::vector<ConfigProfileEntry> &ConfigProfileStore::Entries() const
	{
		return m_Entries;
	}

	Path ConfigProfileStore::ProfilePath(std::string_view name) const
	{
		const std::string sanitized = SanitizeProfileName(name);
		auto manager = m_ConfigManager.lock();
		if (manager == nullptr)
			return Path::FromResolved(sanitized + ".yaml");

		return manager->GetPaths().profilesDirectory / Path(sanitized + ".yaml");
	}

	bool ConfigProfileStore::Save(std::string_view name, const ApplicationConfig &config, std::string &error)
	{
		Ref<ConfigManager> manager = requireManager(error);
		if (manager == nullptr)
			return false;

		if (!manager->SaveConfigProfile(ProfilePath(name), config, error))
			return false;

		(void)Refresh();
		return true;
	}

	bool ConfigProfileStore::Load(const Path &path, ApplicationConfig &config, std::string &error)
	{
		Ref<ConfigManager> manager = requireManager(error);
		if (manager == nullptr)
			return false;

		return manager->LoadConfigProfile(path, config, error);
	}

	bool ConfigProfileStore::Export(const Path &path, std::string &error)
	{
		Ref<ConfigManager> manager = requireManager(error);
		if (manager == nullptr)
			return false;

		std::string text;
		if (!manager->LoadTextFile(path, text, error))
			return false;

		const Path destination = manager->GetPaths().exportsDirectory / path.filename();
		return manager->SaveTextFile(destination, text, error);
	}

	Ref<ConfigManager> ConfigProfileStore::requireManager(std::string &error) const
	{
		auto manager = m_ConfigManager.lock();
		if (manager != nullptr)
			return manager;

		error = "ConfigManager is not available";
		return {};
	}

	std::string ConfigProfileStore::SanitizeProfileName(std::string_view name)
	{
		std::string result;
		result.reserve(name.size());
		for (const char character : name)
		{
			const unsigned char value = static_cast<unsigned char>(character);
			if (std::isalnum(value) || character == '-' || character == '_')
				result.push_back(character);
			else if (std::isspace(value))
				result.push_back('_');
		}

		if (result.empty())
			result = "default";
		return result;
	}
} // namespace DefectStudio
