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
} // namespace DefectStudio::Tests
