#include <gtest/gtest.h>

#include <fstream>

#include "Presentation/Panels/FnvCorrectionInputs.hpp"

namespace DefectStudio::Tests
{
	TEST(FnvCorrectionInputsTests, TheChargeComesFromTheFolderName)
	{
		EXPECT_EQ(ChargeFromCalculationFolder(Path("data/GeV/q-1/LOCPOT")), std::optional<int>(-1));
		EXPECT_EQ(ChargeFromCalculationFolder(Path("data/GeV/q+1/LOCPOT")), std::optional<int>(1));
		EXPECT_EQ(ChargeFromCalculationFolder(Path("data/GeV/q0/LOCPOT")), std::optional<int>(0));
		EXPECT_EQ(ChargeFromCalculationFolder(Path("data/GeV/q-2/LOCPOT")), std::optional<int>(-2));
		EXPECT_EQ(ChargeFromCalculationFolder(Path("Diament/GeN/Stan_1-/LOCPOT")), std::optional<int>(-1));
		EXPECT_EQ(ChargeFromCalculationFolder(Path("Diament/GeV/Stan_2-/LOCPOT")), std::optional<int>(-2));
		EXPECT_EQ(ChargeFromCalculationFolder(Path("Diament/GeV/stan_+1/LOCPOT")), std::optional<int>(1));
		EXPECT_EQ(ChargeFromCalculationFolder(Path::FromUtf8("Praca-Inżynierska/GeV/q-1/LOCPOT")), std::optional<int>(-1));
		EXPECT_FALSE(ChargeFromCalculationFolder(Path("data/GeV/relax/LOCPOT")).has_value());
		EXPECT_FALSE(ChargeFromCalculationFolder(Path("data/quartz/LOCPOT")).has_value());
		EXPECT_FALSE(ChargeFromCalculationFolder(Path("LOCPOT")).has_value());
	}

	TEST(FnvCorrectionInputsTests, TheBulkReferenceIsFoundNextToTheDefectFolder)
	{
		const Path root = Path::FromResolved(FileSystem::TempDirectoryPath()) / "ds-fnv-inputs-test";
		FileSystem::RemoveAll(root.Native());
		ASSERT_TRUE(FileSystem::CreateDirectories((root / "data" / "GeV" / "q-1").Native()));
		ASSERT_TRUE(FileSystem::CreateDirectories((root / "data" / "bulk").Native()));
		std::ofstream((root / "data" / "GeV" / "q-1" / "LOCPOT").Native()) << "defect";
		std::ofstream((root / "data" / "bulk" / "LOCPOT").Native()) << "bulk";

		const std::optional<Path> bulk = FindBulkLocpotNear(root / "data" / "GeV" / "q-1" / "LOCPOT");
		ASSERT_TRUE(bulk.has_value());
		EXPECT_EQ(bulk->filename().Utf8(), "LOCPOT");
		EXPECT_EQ(bulk->parent_path().filename().Utf8(), "bulk");

		FileSystem::RemoveAll((root / "data" / "bulk").Native());
		EXPECT_FALSE(FindBulkLocpotNear(root / "data" / "GeV" / "q-1" / "LOCPOT").has_value());
		FileSystem::RemoveAll(root.Native());
	}

	TEST(FnvCorrectionInputsTests, FractionalCoordinatesWrapIntoTheCell)
	{
		const glm::mat3 lattice(glm::vec3(10, 0, 0), glm::vec3(0, 20, 0), glm::vec3(0, 0, 5));
		const std::optional<glm::dvec3> inside = FractionalCoordinates(lattice, glm::vec3(4.6876f, 10.6f, 2.5f));
		ASSERT_TRUE(inside.has_value());
		EXPECT_NEAR(inside->x, 0.46876, 1e-6);
		EXPECT_NEAR(inside->y, 0.53, 1e-6);
		EXPECT_NEAR(inside->z, 0.5, 1e-6);

		const std::optional<glm::dvec3> outside = FractionalCoordinates(lattice, glm::vec3(-1.0f, 25.0f, 5.0f));
		ASSERT_TRUE(outside.has_value());
		EXPECT_NEAR(outside->x, 0.9, 1e-6);
		EXPECT_NEAR(outside->y, 0.25, 1e-6);
		EXPECT_NEAR(outside->z, 0.0, 1e-6);

		EXPECT_FALSE(FractionalCoordinates(glm::mat3(0.0f), glm::vec3(1.0f)).has_value());
	}

	TEST(FnvCorrectionInputsTests, TheCentreIsTheCentroidOfTheImpurities)
	{
		const Path root = Path::FromResolved(FileSystem::TempDirectoryPath()) / "ds-fnv-centre-test";
		FileSystem::RemoveAll(root.Native());
		ASSERT_TRUE(FileSystem::CreateDirectories((root / "defect").Native()));
		ASSERT_TRUE(FileSystem::CreateDirectories((root / "bulk").Native()));
		// A LOCPOT header (VASP 5, Direct); the Ge and N straddle the cell boundary along a3.
		std::ofstream((root / "defect" / "LOCPOT").Native()) << "GeN\n 2.0\n 7 0 0\n 0 7 0\n 0 0 7\n"
			"   C   Ge_d   N\n   2   1   1\nDirect\n 0.0 0.0 0.0\n 0.25 0.25 0.25\n"
			" 0.4 0.5 0.98\n 0.5 0.6 0.04\n";
		std::ofstream((root / "bulk" / "LOCPOT").Native()) << "bulk\n 2.0\n 7 0 0\n 0 7 0\n 0 0 7\n"
			"   C\n   2\nSelective dynamics\nDirect\n 0 0 0 T T T\n 0.25 0.25 0.25 T T T\n";

		const auto defect = ReadLocpotStructure(root / "defect" / "LOCPOT");
		const auto bulk = ReadLocpotStructure(root / "bulk" / "LOCPOT");
		ASSERT_TRUE(defect.has_value());
		ASSERT_TRUE(bulk.has_value());
		EXPECT_EQ(defect->species.size(), 4u);
		EXPECT_NEAR(defect->lattice[0].x, 14.0, 1e-12);
		EXPECT_EQ(FnvAxisLabels(defect->lattice), (std::array<std::string, 3>{"a1 (x)", "a2 (y)", "a3 (z)"}));

		const auto centre = FindImpurityCentre(*defect, *bulk);
		ASSERT_TRUE(centre.has_value());
		EXPECT_EQ(centre->species, "Ge+N");
		EXPECT_NEAR(centre->fractional.x, 0.45, 1e-12);
		EXPECT_NEAR(centre->fractional.y, 0.55, 1e-12);
		EXPECT_NEAR(centre->fractional.z, 0.01, 1e-12); // across the boundary, not 0.51
		EXPECT_FALSE(FindImpurityCentre(*bulk, *bulk).has_value());

		// The CONTCAR next to the LOCPOT wins; a VASP 4 file (no symbols line) is refused.
		std::ofstream((root / "defect" / "CONTCAR").Native()) << "x\n 1.0\n 7 0 0\n 0 7 0\n 0 0 7\n"
			"   2   1\nDirect\n 0 0 0\n 0.1 0.1 0.1\n 0.2 0.2 0.2\n";
		EXPECT_FALSE(ReadLocpotStructure(root / "defect" / "LOCPOT").has_value());
		EXPECT_EQ(FnvAxisLabels({glm::dvec3(1, 1, 0), glm::dvec3(0, 1, 1), glm::dvec3(1, 0, 1)}),
			(std::array<std::string, 3>{"a1", "a2", "a3"}));
		FileSystem::RemoveAll(root.Native());
	}
} // namespace DefectStudio::Tests
