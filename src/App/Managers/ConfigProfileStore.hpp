#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "App/ApplicationState.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	class ConfigManager;

	struct ConfigProfileEntry
	{
		std::string name;
		Path path;
	};

	class ConfigProfileStore
	{
	public:
		explicit ConfigProfileStore(WeakRef<ConfigManager> configManager = {});

		void Bind(WeakRef<ConfigManager> configManager);
		[[nodiscard]] const std::vector<ConfigProfileEntry> &Refresh();
		[[nodiscard]] const std::vector<ConfigProfileEntry> &Entries() const;
		[[nodiscard]] Path ProfilePath(std::string_view name) const;
		[[nodiscard]] bool Save(std::string_view name, const ApplicationConfig &config, std::string &error);
		[[nodiscard]] bool Load(const Path &path, ApplicationConfig &config, std::string &error);
		[[nodiscard]] bool Export(const Path &path, std::string &error);

	private:
		[[nodiscard]] Ref<ConfigManager> requireManager(std::string &error) const;
		[[nodiscard]] static std::string SanitizeProfileName(std::string_view name);

	private:
		WeakRef<ConfigManager> m_ConfigManager;
		std::vector<ConfigProfileEntry> m_Entries;
	};
} // namespace DefectStudio
