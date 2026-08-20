#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "IO/ProjectRootsIO.hpp"

namespace DefectStudio
{
	// A real project: a user-chosen directory holding manifest.yaml, distinct from the data
	// `roots` it references (which can be anywhere, including a mounted drive - see T07.5.2).
	// T07.5.1 scope cut for this pass: no PathResolver, tags, migration pipeline, or
	// canonical/recovery save split - see docs/work/project/TODO.md.
	struct ProjectManifest
	{
		std::string uuid;
		std::string name;
		std::int64_t createdAt = 0; // epoch seconds, Time::Now()
		std::int64_t lastModified = 0; // epoch seconds, bumped on every Save()
		int formatVersion = 1;
		std::vector<ProjectRootEntry> roots;
		Path bulkDirectory; // empty = unset, see T07.5.5
	};

	class ProjectManifestIO
	{
	public:
		[[nodiscard]] static Path ManifestPath(const Path &projectDirectory);

		// Fresh manifest with a new uuid/timestamps/empty roots - does not touch disk.
		[[nodiscard]] static ProjectManifest CreateNew(const std::string &name);

		[[nodiscard]] static bool Load(const Path &projectDirectory, ProjectManifest &outManifest, std::string &outError);
		// Bumps lastModified to now before writing.
		[[nodiscard]] static bool Save(const Path &projectDirectory, ProjectManifest &manifest, std::string &outError);
	};
} // namespace DefectStudio
