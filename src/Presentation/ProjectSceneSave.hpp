#pragma once

#include <span>
#include <string>

#include "Core/Utils/Path.hpp"
#include "Domain/DomainIds.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "IO/ProjectManifestIO.hpp"
#include "IO/SceneObjectsIO.hpp"

namespace DefectStudio
{
	// Ctrl+S sequence for scene objects: scene_objects.yaml first, manifest.yaml second, each its own
	// temp + rename. Only when BOTH writes succeed is savedRevision set to revision for every record in
	// `savedStructures`; any failure returns false with outError and leaves every savedRevision
	// unchanged, so the project stays dirty. No two-file transaction on purpose: neither file depends
	// on the other, so a crash between the two renames still leaves a loadable project.
	[[nodiscard]] bool SaveProjectWithSceneObjects(
		const Path &projectDirectory,
		ProjectManifest &manifest,
		const SceneObjectsFile &sceneObjects,
		StructureRegistry &registry,
		std::span<const StructureId> savedStructures,
		std::string &outError);
} // namespace DefectStudio
