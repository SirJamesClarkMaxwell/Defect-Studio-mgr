#pragma once

#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"

namespace DefectStudio
{
	// One registered project-tree root: a folder the user browses, with a free-text label
	// identifying where it physically comes from (e.g. "server-A", "local-cache"). No real
	// ServerProfile type exists yet (T07.5.2 is unimplemented) - label is just a string for now.
	struct ProjectRootEntry
	{
		std::string id;
		Path path;
		std::string label;
	};

	// Small stand-in for real T07.5.1 manifest.yaml persistence (which doesn't exist yet) - see
	// EditorLayer.cpp's other ponytail placeholders it partially supersedes. Read/write
	// install/users/default/config/project_roots.yaml: a flat list of {id, path, label}.
	class ProjectRootsIO
	{
	public:
		[[nodiscard]] static Path DefaultFilePath();

		// Fresh short random id for a new entry - shared by Load()'s one-time migration seed and
		// by EditorLayer's "+ Add Root" handler, so there's one id scheme, not two.
		[[nodiscard]] static std::string GenerateRootId();

		// If `filePath` doesn't exist yet, falls back to a one-time migration (see .cpp): seeds
		// from the older last_project_root.txt placeholder if present, else from the hardcoded
		// dev-machine default, else an empty list - and persists whatever it seeded so this
		// fallback only ever runs once.
		[[nodiscard]] static bool Load(const Path &filePath, std::vector<ProjectRootEntry> &outRoots, std::string &outError);
		[[nodiscard]] static bool Save(const Path &filePath, const std::vector<ProjectRootEntry> &roots, std::string &outError);
	};
} // namespace DefectStudio
