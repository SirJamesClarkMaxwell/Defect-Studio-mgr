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
} // namespace DefectStudio::Tests
