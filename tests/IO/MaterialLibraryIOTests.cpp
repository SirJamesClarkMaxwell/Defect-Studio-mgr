#include <gtest/gtest.h>

#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Domain/Crystal/BravaisLattice.hpp"
#include "IO/MaterialLibraryIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] Path MakeTempLibraryPath()
		{
			return Path::FromResolved(FileSystem::TempDirectoryPath()) /
				("defectstudio_material_library_" + std::to_string(Time::NowSteady().time_since_epoch().count()) + ".db");
		}

		// A path inside a directory that does not exist yet - mirrors the real project-scoped
		// default ("materials/materials.db"), whose "materials" subdirectory ProjectManifest's
		// CreateNew() never creates on disk. Returns the .db path; the parent directory is the
		// caller's to clean up.
		[[nodiscard]] Path MakeFreshLibraryPathInMissingDirectory()
		{
			const Path missingDirectory = Path::FromResolved(FileSystem::TempDirectoryPath()) /
				("defectstudio_material_library_fresh_" + std::to_string(Time::NowSteady().time_since_epoch().count()));
			return missingDirectory / "materials.db";
		}
	} // namespace

	TEST(MaterialLibraryIOTests, AddListLoadRemoveRoundtrip)
	{
		CrystalStructure structure;
		structure.cell = BuildLatticeCell(CrystalSystem::Cubic, LatticeParameters{.a = 3.615f});
		structure.atoms = { AtomSite{"Cu", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };

		const Path libraryPath = MakeTempLibraryPath();
		MaterialLibraryIO library(libraryPath);

		const Result<MaterialLibraryEntry> added = library.AddMaterial(structure, "Copper bulk", "test entry");
		if (!added)
			GTEST_SKIP() << "ase unavailable in current environment: " << added.Error().technicalDetails;
		EXPECT_EQ(added->name, "Copper bulk");

		const Result<std::vector<MaterialLibraryEntry>> listed = library.ListMaterials();
		ASSERT_TRUE(listed);
		ASSERT_EQ(listed->size(), 1u);
		EXPECT_EQ((*listed)[0].id, added->id);

		const Result<CrystalStructure> loaded = library.LoadMaterial(added->id);
		ASSERT_TRUE(loaded);
		EXPECT_EQ(loaded->atoms.size(), 1u);
		EXPECT_EQ(loaded->atoms[0].species, "Cu");

		const Result<void> removed = library.RemoveMaterial(added->id);
		EXPECT_TRUE(removed);
		const Result<std::vector<MaterialLibraryEntry>> listedAfterRemove = library.ListMaterials();
		ASSERT_TRUE(listedAfterRemove);
		EXPECT_TRUE(listedAfterRemove->empty());

		std::error_code removeError;
		FileSystem::Remove(libraryPath.Native(), removeError);
	}

	// Regression test for the fresh-project gap: Task 9 will call ListMaterials() (and
	// LoadMaterial/RemoveMaterial) against a project's default materials library path before
	// AddMaterial() has ever run there, so the "materials" subdirectory does not exist on disk yet.
	// Previously only AddMaterial created that missing directory as a side effect, so this call
	// failed with a generic sqlite "unable to open database file" subprocess error instead of
	// succeeding with an empty list.
	TEST(MaterialLibraryIOTests, ListSucceedsAgainstNeverTouchedLibraryDirectory)
	{
		const Path libraryPath = MakeFreshLibraryPathInMissingDirectory();
		const Path libraryDirectory = libraryPath.parent_path();
		ASSERT_FALSE(FileSystem::Exists(libraryDirectory.Native()));

		MaterialLibraryIO library(libraryPath);

		const Result<std::vector<MaterialLibraryEntry>> listed = library.ListMaterials();
		if (!listed)
			GTEST_SKIP() << "ase unavailable in current environment: " << listed.Error().technicalDetails;
		EXPECT_TRUE(listed->empty());

		std::error_code removeError;
		FileSystem::RemoveAll(libraryDirectory.Native(), removeError);
	}
} // namespace DefectStudio::Tests
