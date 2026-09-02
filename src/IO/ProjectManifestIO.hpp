#pragma once

#include <cstdint>
#include <string>
#include <utility>
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
		// Irrep -> user's own custom label (e.g. "b_1*" -> "\pi*"), shown ALONGSIDE the automatic
		// irrep, never replacing it - point-group/irrep naming is a property of the material/defect
		// (the project), not any single structure window. Vector, not a map: preserves the user's
		// own row order and tolerates duplicate/blank-key rows while editing without extra fuss.
		std::vector<std::pair<std::string, std::string>> irrepLabelOverrides;
		// Atoms-displacement comparison (T08 item 0 / T16 item 8) - last comparison file + slider
		// value, remembered per-project so reopening doesn't lose what was being compared. The
		// computed result itself is NOT persisted (recomputed by pressing "Compare" again) - see
		// DisplacementComparisonPanel.
		Path displacementComparisonPath; // empty = none
		float displacementThresholdAngstrom = 0.0f;
		// Materials Collection (Faza 3 of the supercell-generation plan) - project-scoped ase.db
		// library, relative to the project dir. "materials/materials.db" once CreateNew() has run;
		// empty = not yet created (older manifests loaded before this field existed). The .db file
		// itself is created lazily by MaterialLibraryIO on the first AddMaterial(), not here.
		Path materialsLibraryPath; // "materials/materials.db" relative to the project dir when set
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
