#include "Core/dspch.hpp"
#include "Presentation/ProjectSceneSave.hpp"

#include "IO/TextFileIO.hpp"

namespace DefectStudio
{
	bool SaveProjectWithSceneObjects(const Path &projectDirectory, ProjectManifest &manifest, const SceneObjectsFile &sceneObjects, StructureRegistry &registry, std::span<const StructureId> savedStructures, std::string &outError)
	{
		if (!SceneObjectsIO::Save(projectDirectory, sceneObjects, outError)) return false;
		if (!ProjectManifestIO::Save(projectDirectory, manifest, outError)) return false;
		for (const StructureId &id : savedStructures) { const auto record=registry.Find(id).lock(); if(record!=nullptr) registry.UpdateSavedRevision(id,record->revision); }
		return true;
	}
} // namespace DefectStudio
