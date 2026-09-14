#include "Core/dspch.hpp"

#include "Presentation/ProjectSceneSave.hpp"

#include <algorithm>
#include <filesystem>

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
bool SaveProjectWithSceneObjects(const Path &projectDirectory, ProjectManifest &manifest,
								 const SceneObjectsFile &sceneObjects, StructureRegistry &registry,
								 std::span<const StructureId> savedStructures,
								 const StructureFileWriter &writeStructureFile,
								 std::vector<StructuredError> &outWarnings, std::string &outError)
{
	const std::filesystem::path project = projectDirectory.Native().lexically_normal();
	auto insideProject = [&](const Path &sourcePath) {
		if (sourcePath.Empty())
			return false;
		const std::filesystem::path source = sourcePath.Native().lexically_normal();
		auto projectIt = project.begin();
		auto sourceIt = source.begin();
		for (; projectIt != project.end(); ++projectIt, ++sourceIt)
			if (sourceIt == source.end() || *projectIt != *sourceIt)
				return false;
		return true;
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
	if (!SceneObjectsIO::Save(projectDirectory, sceneObjects, outError))
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
