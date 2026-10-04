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

		[[nodiscard]] bool FileDirty(const StructureRegistry &registry, const StructureId &id)
		{
			return registry.Find(id).lock()->structureFileDirty;
		}

		// Records every call and writes a marker file, so tests see what would reach the POSCAR.
		struct FakeWriter
		{
			std::vector<std::filesystem::path> written;
			bool fail = false;

			[[nodiscard]] StructureFileWriter Bind()
			{
				return [this](const CrystalStructure &, const Path &path) -> Result<void> {
					if (fail)
						return StructuredError(ErrorCategory::IO, Severity::Error, "fake write failed", "", "");
					written.push_back(path.Native());
					std::ofstream(path.Native()) << "written";
					return Result<void>{};
				};
			}
		};

		void RemoveRoot(const std::filesystem::path &root)
		{
			std::error_code ignored;
			std::filesystem::remove_all(root, ignored);
		}
	} // namespace

	TEST(ProjectSceneSaveTests, MarkModifiedOnlyIncreasesRevisionAndUndoAfterSaveIsDirty)
	{
		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{})->id;
		ASSERT_TRUE(registry.MarkModified(id));
		ASSERT_TRUE(registry.MarkModified(id));
		EXPECT_EQ(Revision(registry, id), 2);
		EXPECT_FALSE(FileDirty(registry, id)); // scene edits never dirty the POSCAR
		ASSERT_TRUE(registry.UpdateSavedRevision(id, 2));

		// Undo of a scene edit also calls MarkModified - the project must read dirty again.
		ASSERT_TRUE(registry.MarkModified(id));
		EXPECT_EQ(Revision(registry, id), 3);
		EXPECT_NE(Revision(registry, id), SavedRevision(registry, id));
		EXPECT_FALSE(registry.MarkModified(StructureId{}));
	}

	TEST(ProjectSceneSaveTests, MarkStructureFileModifiedBumpsRevisionAndSetsFlag)
	{
		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{})->id;
		ASSERT_TRUE(registry.MarkStructureFileModified(id));
		EXPECT_EQ(Revision(registry, id), 1);
		EXPECT_TRUE(FileDirty(registry, id));
		ASSERT_TRUE(registry.ClearStructureFileDirty(id));
		EXPECT_FALSE(FileDirty(registry, id));
		EXPECT_FALSE(registry.MarkStructureFileModified(StructureId{}));
		EXPECT_FALSE(registry.ClearStructureFileDirty(StructureId{}));
	}

	TEST(ProjectSceneSaveTests, SuccessfulSaveWritesBothFilesAndMarksSaved)
	{
		const std::filesystem::path root = MakeTempRoot("ok");
		const Path projectDirectory = Path::FromResolved(root);
		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{})->id;
		ASSERT_TRUE(registry.MarkModified(id));
		ProjectManifest manifest = ProjectManifestIO::CreateNew("scene-save");

		FakeWriter writer;
		std::vector<StructuredError> warnings;
		std::string error;
		const StructureId ids[] = {id};
		ASSERT_TRUE(SaveProjectWithSceneObjects(
			projectDirectory, manifest, OneLabelFile(), registry, ids, writer.Bind(), warnings, error)) << error;
		EXPECT_TRUE(writer.written.empty()); // scene-only edit: POSCAR untouched
		EXPECT_TRUE(warnings.empty());
		EXPECT_EQ(SavedRevision(registry, id), Revision(registry, id));
		EXPECT_TRUE(std::filesystem::exists(SceneObjectsIO::FilePath(projectDirectory).Native()));
		EXPECT_TRUE(std::filesystem::exists(ProjectManifestIO::ManifestPath(projectDirectory).Native()));

		SceneObjectsFile reloaded;
		std::vector<StructuredError> loadWarnings;
		ASSERT_TRUE(SceneObjectsIO::Load(projectDirectory, reloaded, loadWarnings, error)) << error;
		EXPECT_EQ(std::get<PersistedFreeLabel>(reloaded.structures.at(0).objects.at(0)).text, "saved");

		RemoveRoot(root);
	}

	TEST(ProjectSceneSaveTests, EditedStructureInsideProjectIsWrittenAndCleared)
	{
		const std::filesystem::path root = MakeTempRoot("poscar");
		std::filesystem::create_directories(root / "NV");
		const std::filesystem::path poscar = root / "NV" / "POSCAR";
		std::ofstream(poscar) << "original";
		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{}, Path::FromResolved(poscar))->id;
		ASSERT_TRUE(registry.MarkStructureFileModified(id));
		ProjectManifest manifest = ProjectManifestIO::CreateNew("scene-save");

		FakeWriter writer;
		std::vector<StructuredError> warnings;
		std::string error;
		const StructureId ids[] = {id};
		ASSERT_TRUE(SaveProjectWithSceneObjects(
			Path::FromResolved(root), manifest, OneLabelFile(), registry, ids, writer.Bind(), warnings, error)) << error;
		ASSERT_EQ(writer.written.size(), 1u);
		EXPECT_EQ(writer.written[0], poscar);
		EXPECT_TRUE(warnings.empty());
		EXPECT_FALSE(FileDirty(registry, id));
		EXPECT_EQ(SavedRevision(registry, id), Revision(registry, id));

		RemoveRoot(root);
	}

	TEST(ProjectSceneSaveTests, EditedStructureOutsideProjectIsNotWritten)
	{
		const std::filesystem::path root = MakeTempRoot("outside");
		const std::filesystem::path project = root / "project";
		const std::filesystem::path dataRoot = root / "data";
		std::filesystem::create_directories(project);
		std::filesystem::create_directories(dataRoot);
		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{}, Path::FromResolved(dataRoot / "POSCAR"))->id;
		ASSERT_TRUE(registry.MarkStructureFileModified(id));
		ProjectManifest manifest = ProjectManifestIO::CreateNew("scene-save");

		FakeWriter writer;
		std::vector<StructuredError> warnings;
		std::string error;
		const StructureId ids[] = {id};
		ASSERT_TRUE(SaveProjectWithSceneObjects(
			Path::FromResolved(project), manifest, OneLabelFile(), registry, ids, writer.Bind(), warnings, error)) << error;
		EXPECT_TRUE(writer.written.empty());
		EXPECT_FALSE(std::filesystem::exists(dataRoot / "POSCAR"));
		ASSERT_EQ(warnings.size(), 1u);
		EXPECT_EQ(warnings[0].severity, Severity::Warning);
		EXPECT_EQ(warnings[0].code, "project_save.structure_outside_project");
		EXPECT_TRUE(FileDirty(registry, id));

		RemoveRoot(root);
	}

	// "New project" creates the manifest in its own subfolder and registers the folder holding the
	// structures as a data root, so the edited POSCAR is outside projectDirectory but inside a root.
	TEST(ProjectSceneSaveTests, EditedStructureInDataRootIsWritten)
	{
		const std::filesystem::path root = MakeTempRoot("data_root");
		const std::filesystem::path project = root / "NewProject";
		std::filesystem::create_directories(project);
		std::filesystem::create_directories(root / "NV");
		const std::filesystem::path poscar = root / "NV" / "POSCAR";
		std::ofstream(poscar) << "original";
		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{}, Path::FromResolved(poscar))->id;
		ASSERT_TRUE(registry.MarkStructureFileModified(id));
		ProjectManifest manifest = ProjectManifestIO::CreateNew("scene-save");
		manifest.roots.push_back({"root-a", Path::FromResolved(root / "elsewhere"), "elsewhere"});
		manifest.roots.push_back({"root-b", Path::FromResolved(root), "data"});

		FakeWriter writer;
		std::vector<StructuredError> warnings;
		std::string error;
		const StructureId ids[] = {id};
		ASSERT_TRUE(SaveProjectWithSceneObjects(
			Path::FromResolved(project), manifest, OneLabelFile(), registry, ids, writer.Bind(), warnings, error)) << error;
		ASSERT_EQ(writer.written.size(), 1u);
		EXPECT_EQ(writer.written[0], poscar);
		EXPECT_TRUE(warnings.empty());
		EXPECT_FALSE(FileDirty(registry, id));

		RemoveRoot(root);
	}

	TEST(ProjectSceneSaveTests, StructureWriteFailureStopsBeforeSceneFiles)
	{
		const std::filesystem::path root = MakeTempRoot("writefail");
		const std::filesystem::path poscar = root / "POSCAR";
		StructureRegistry registry;
		const StructureId id = registry.Add(CrystalStructure{}, Path::FromResolved(poscar))->id;
		ASSERT_TRUE(registry.MarkStructureFileModified(id));
		ProjectManifest manifest = ProjectManifestIO::CreateNew("scene-save");

		FakeWriter writer;
		writer.fail = true;
		std::vector<StructuredError> warnings;
		std::string error;
		const StructureId ids[] = {id};
		EXPECT_FALSE(SaveProjectWithSceneObjects(
			Path::FromResolved(root), manifest, OneLabelFile(), registry, ids, writer.Bind(), warnings, error));
		EXPECT_FALSE(error.empty());
		EXPECT_FALSE(std::filesystem::exists(SceneObjectsIO::FilePath(Path::FromResolved(root)).Native()));
		EXPECT_TRUE(FileDirty(registry, id));
		EXPECT_EQ(SavedRevision(registry, id), 0);

		RemoveRoot(root);
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

		FakeWriter writer;
		std::vector<StructuredError> warnings;
		std::string error;
		const StructureId ids[] = {id};
		EXPECT_FALSE(SaveProjectWithSceneObjects(
			projectDirectory, manifest, OneLabelFile(), registry, ids, writer.Bind(), warnings, error));
		EXPECT_FALSE(error.empty());
		EXPECT_EQ(SavedRevision(registry, id), 0);
		EXPECT_NE(Revision(registry, id), SavedRevision(registry, id));

		RemoveRoot(root);
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

		RemoveRoot(root);
	}
} // namespace DefectStudio::Tests
