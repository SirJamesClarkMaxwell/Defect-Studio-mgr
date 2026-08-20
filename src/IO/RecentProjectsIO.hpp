#pragma once

#include <cstdint>
#include <vector>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	struct RecentProjectEntry
	{
		Path projectDirectory;
		std::int64_t lastOpenedAt = 0; // epoch seconds
	};

	// Machine-scoped list of recently opened project directories (each holding its own
	// manifest.yaml, see ProjectManifestIO) - not part of any single project. Same small-file
	// pattern as every other install/users/default/config/ placeholder this session: plain
	// TextFileIO+YAML::Emitter, not routed through ConfigManager.
	class RecentProjectsIO
	{
	public:
		[[nodiscard]] static Path DefaultFilePath();

		[[nodiscard]] static bool Load(const Path &filePath, std::vector<RecentProjectEntry> &outEntries, std::string &outError);
		[[nodiscard]] static bool Save(const Path &filePath, const std::vector<RecentProjectEntry> &entries, std::string &outError);

		// Moves `projectDirectory` to the front (or inserts it) with the current time, drops
		// anything past `maxEntries`. Does not touch disk - call Save() with the result.
		static void Touch(std::vector<RecentProjectEntry> &entries, const Path &projectDirectory, std::size_t maxEntries = 10);
	};
} // namespace DefectStudio
