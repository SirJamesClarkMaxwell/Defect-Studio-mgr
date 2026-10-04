#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/DomainIds.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "IO/ProjectManifestIO.hpp"
#include "IO/SceneObjectsIO.hpp"

namespace DefectStudio
{
	// Writes one structure to its POSCAR path, replacing the existing file. The app passes a
	// PoscarWriter::Write(..., overwriteExisting = true) call; tests pass a fake.
	using StructureFileWriter = std::function<Result<void>(const CrystalStructure &structure, const Path &poscarPath)>;

	// Ctrl+S sequence:
	// 1. Every record in `savedStructures` with structureFileDirty:
	//    - sourcePath inside projectDirectory or inside one of manifest.roots ->
	//      writeStructureFile(structure, sourcePath); the first failure returns false with outError,
	//      before any scene/manifest write;
	//    - otherwise (a file in no project directory, or empty sourcePath) -> not written, flag kept,
	//      one Severity::Warning StructuredError with code "project_save.structure_outside_project".
	// 2. scene_objects.yaml, 3. manifest.yaml, each its own temp + rename.
	// Only when every write succeeded: savedRevision = revision for every record in savedStructures
	// and structureFileDirty cleared for every structure written in step 1. Any failure leaves every
	// savedRevision unchanged, so the project stays dirty.
	[[nodiscard]] bool SaveProjectWithSceneObjects(
		const Path &projectDirectory,
		ProjectManifest &manifest,
		const SceneObjectsFile &sceneObjects,
		StructureRegistry &registry,
		std::span<const StructureId> savedStructures,
		const StructureFileWriter &writeStructureFile,
		std::vector<StructuredError> &outWarnings,
		std::string &outError);
} // namespace DefectStudio
