#include "Core/dspch.hpp"

#include "Presentation/ProjectSceneSave.hpp"

#include <algorithm>
#include <filesystem>

#include "IO/TextFileIO.hpp"
#include "Renderer/Scene/ScenePathPersistence.hpp"

namespace DefectStudio
{
bool SaveProjectWithSceneObjects(const Path &projectDirectory, ProjectManifest &manifest,
								 const SceneObjectsFile &sceneObjects, StructureRegistry &registry,
								 std::span<const StructureId> savedStructures,
								 const StructureFileWriter &writeStructureFile,
								 std::vector<StructuredError> &outWarnings, std::string &outError)
{
	SceneObjectsFile migratedSceneObjects = sceneObjects;
	const auto migration = MigratePersistedSceneArrows(migratedSceneObjects, outWarnings);
	if (!migration)
	{
		outError = migration.Error().technicalDetails;
		outWarnings.push_back(migration.Error());
		return false;
	}
	auto insideDirectory = [](const Path &directory, const std::filesystem::path &source) {
		if (directory.Empty())
			return false;
		const std::filesystem::path normalized = directory.Native().lexically_normal();
		auto directoryIt = normalized.begin();
		auto sourceIt = source.begin();
		for (; directoryIt != normalized.end(); ++directoryIt, ++sourceIt)
			if (sourceIt == source.end() || *directoryIt != *sourceIt)
				return false;
		return true;
	};
	// The data roots are part of the project too - the Project Tree lists them and the app already
	// authorizes structure writes into them (Add to Project).
	auto insideProject = [&](const Path &sourcePath) {
		if (sourcePath.Empty())
			return false;
		const std::filesystem::path source = sourcePath.Native().lexically_normal();
		return insideDirectory(projectDirectory, source) ||
			std::any_of(manifest.roots.begin(), manifest.roots.end(),
				[&](const ProjectRootEntry &root) { return insideDirectory(root.path, source); });
	};
	std::vector<StructureId> writtenStructures;
	for (const StructureId &id : savedStructures)
	{
		const auto record = registry.Find(id).lock();
		if (record == nullptr || !record->structureFileDirty)
			continue;
		if (!insideProject(record->sourcePath))
		{
			outWarnings.emplace_back(ErrorCategory::IO, Severity::Warning,
				"Structure file is outside the project", "The edited structure was not written: " + record->sourcePath.String(),
				"Move the structure into the project before saving it.", "ProjectSceneSave",
				"project_save.structure_outside_project");
			continue;
		}
		const Result<void> result = writeStructureFile(record->structure, record->sourcePath);
		if (!result)
		{
			outError = result.Error().technicalDetails.empty() ? result.Error().userMessage : result.Error().technicalDetails;
			return false;
		}
		writtenStructures.push_back(id);
	}
	if (!SceneObjectsIO::Save(projectDirectory, migratedSceneObjects, outError))
		return false;
	if (!ProjectManifestIO::Save(projectDirectory, manifest, outError))
		return false;
	for (const StructureId &id : savedStructures)
	{
		const auto record = registry.Find(id).lock();
		if (record != nullptr)
		{
			registry.UpdateSavedRevision(id, record->revision);
			if (std::find(writtenStructures.begin(), writtenStructures.end(), id) != writtenStructures.end())
				registry.ClearStructureFileDirty(id);
		}
	}
	return true;
}
} // namespace DefectStudio
