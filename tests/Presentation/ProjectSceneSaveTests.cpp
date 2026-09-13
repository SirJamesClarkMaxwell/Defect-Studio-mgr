#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "Core/Utils/Time.hpp"
#include "Presentation/ProjectSceneSave.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] std::filesystem::path MakeTempRoot(const char *tag)
		{
			const std::filesystem::path root = std::filesystem::temp_directory_path() /
				(std::string("ds_project_scene_save_") + tag + "_" + std::to_string(Time::NowSteady().time_since_epoch().count()));
			std::filesystem::create_directories(root);
			return root;
		}

		[[nodiscard]] SceneObjectsFile OneLabelFile()
		{
			PersistedFreeLabel label;
			label.text = "saved";
			SceneObjectsFile file;
			file.structures.push_back({"POSCAR", {label}});
			return file;
		}

		[[nodiscard]] int Revision(const StructureRegistry &registry, const StructureId &id)
		{
			return registry.Find(id).lock()->revision;
		}

		[[nodiscard]] int SavedRevision(const StructureRegistry &registry, const StructureId &id)
		{
			return registry.Find(id).lock()->savedRevision;
		}
	} // namespace

	TEST(ProjectSceneSaveTests, MarkModifiedOnlyIncreasesRevisionAndUndoAfterSaveIsDirty)
	{
		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{})->id;
		ASSERT_TRUE(registry.MarkModified(id));
		ASSERT_TRUE(registry.MarkModified(id));
		EXPECT_EQ(Revision(registry, id), 2);
		ASSERT_TRUE(registry.UpdateSavedRevision(id, 2));

		// Undo of a scene edit also calls MarkModified - the project must read dirty again.
		ASSERT_TRUE(registry.MarkModified(id));
		EXPECT_EQ(Revision(registry, id), 3);
		EXPECT_NE(Revision(registry, id), SavedRevision(registry, id));
		EXPECT_FALSE(registry.MarkModified(StructureId{}));
	}

	TEST(ProjectSceneSaveTests, SuccessfulSaveWritesBothFilesAndMarksSaved)
	{
		const std::filesystem::path root = MakeTempRoot("ok");
		const Path projectDirectory = Path::FromResolved(root);
		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{})->id;
		ASSERT_TRUE(registry.MarkModified(id));
		ProjectManifest manifest = ProjectManifestIO::CreateNew("scene-save");

		std::string error;
		const StructureId ids[] = {id};
		ASSERT_TRUE(SaveProjectWithSceneObjects(projectDirectory, manifest, OneLabelFile(), registry, ids, error)) << error;
		EXPECT_EQ(SavedRevision(registry, id), Revision(registry, id));
		EXPECT_TRUE(std::filesystem::exists(SceneObjectsIO::FilePath(projectDirectory).Native()));
		EXPECT_TRUE(std::filesystem::exists(ProjectManifestIO::ManifestPath(projectDirectory).Native()));

		SceneObjectsFile reloaded;
		std::vector<StructuredError> warnings;
		ASSERT_TRUE(SceneObjectsIO::Load(projectDirectory, reloaded, warnings, error)) << error;
		EXPECT_EQ(std::get<PersistedFreeLabel>(reloaded.structures.at(0).objects.at(0)).text, "saved");

		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}

	TEST(ProjectSceneSaveTests, FailedSaveLeavesProjectDirty)
	{
		const std::filesystem::path root = MakeTempRoot("fail");
		// A regular file where the project directory's parent should be: nothing can be written below it.
		const std::filesystem::path blocker = root / "blocker";
		std::ofstream(blocker) << "not a directory";
		const Path projectDirectory = Path::FromResolved(blocker / "project");

		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{})->id;
		ASSERT_TRUE(registry.MarkModified(id));
		ProjectManifest manifest = ProjectManifestIO::CreateNew("scene-save");

		std::string error;
		const StructureId ids[] = {id};
		EXPECT_FALSE(SaveProjectWithSceneObjects(projectDirectory, manifest, OneLabelFile(), registry, ids, error));
		EXPECT_FALSE(error.empty());
		EXPECT_EQ(SavedRevision(registry, id), 0);
		EXPECT_NE(Revision(registry, id), SavedRevision(registry, id));

		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}

	TEST(ProjectSceneSaveTests, MissingSceneFileLoadsAsEmpty)
	{
		const std::filesystem::path root = MakeTempRoot("missing");
		SceneObjectsFile loaded;
		loaded.structures.push_back({"stale", {}});
		std::vector<StructuredError> warnings;
		std::string error;
		EXPECT_TRUE(SceneObjectsIO::Load(Path::FromResolved(root), loaded, warnings, error)) << error;
		EXPECT_TRUE(loaded.structures.empty());
		EXPECT_TRUE(warnings.empty());

		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}
} // namespace DefectStudio::Tests
