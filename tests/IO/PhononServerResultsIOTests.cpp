#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "Core/Utils/Uuid.hpp"
#include "IO/PhononServerResultsIO.hpp"

namespace DefectStudio::Tests
{
	class PhononServerResultsIOTests : public testing::Test
	{
	protected:
		Path root = Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_server_hr_" + ToString(GenerateUuid())));
		void SetUp() override { std::filesystem::create_directories(root.Native()); }
		void TearDown() override { std::error_code ignored; std::filesystem::remove_all(root.Native(), ignored); }
		void Hr(const Path &directory)
		{
			std::filesystem::create_directories(directory.Native());
			std::ofstream((directory / "hr_factors.dat").Native()) << "0.1\n";
		}
	};

	TEST_F(PhononServerResultsIOTests, FindsSizesAndVariantsSorted)
	{
		const Path system = root / "PL-spectra/dimer/CH0/R2SCAN/9x9x1";
		Hr(system / "spectral-function/12x12x1/displacements");
		Hr(system / "spectral-function/9x9x1/displacements");
		Hr(system / "spectral-function/10x10x1/displacements_cutoff4A");
		Hr(system / "spectral-function/10x10x1/displacements_cutoff2.5A");
		std::filesystem::create_directories((system / "spectral-function/11x11x1/displacements").Native()); // no hr_factors.dat
		Hr(system / "spectral-function/big/displacements");     // not AxBxC
		Hr(system / "spectral-function/0x9x1/displacements");   // not positive
		Hr(system / "embedded/spectral-function/9x9x1/displacements"); // inside a skipped directory

		const auto found = FindServerHrResults(root, {});
		ASSERT_EQ(found.size(), 4u);
		EXPECT_EQ(found[0].variant, "displacements");
		EXPECT_EQ(found[0].supercell, (std::array<int, 3>{9, 9, 1}));
		EXPECT_EQ(found[1].supercell, (std::array<int, 3>{12, 12, 1}));
		EXPECT_EQ(found[0].systemDirectory, system);
		EXPECT_EQ(found[1].hrDirectory, system / "spectral-function/12x12x1/displacements");
		EXPECT_EQ(found[0].method, "displacements");
		EXPECT_FALSE(found[0].cutoffAngstrom.has_value());
		EXPECT_EQ(found[2].variant, "displacements_cutoff2.5A");
		EXPECT_EQ(found[2].method, "displacements");
		ASSERT_TRUE(found[2].cutoffAngstrom.has_value());
		EXPECT_DOUBLE_EQ(*found[2].cutoffAngstrom, 2.5);
		EXPECT_EQ(found[3].variant, "displacements_cutoff4A");
		EXPECT_DOUBLE_EQ(*found[3].cutoffAngstrom, 4.0);
	}

	TEST_F(PhononServerResultsIOTests, RootMayBeTheHrDirectoryOrTheSizeDirectory)
	{
		const Path hr = root / "sys/spectral-function/12x12x1/displacements";
		Hr(hr);
		const auto fromHr = FindServerHrResults(hr, {});
		ASSERT_EQ(fromHr.size(), 1u);
		EXPECT_EQ(fromHr[0].hrDirectory, hr);
		EXPECT_EQ(fromHr[0].systemDirectory, root / "sys");
		EXPECT_EQ(FindServerHrResults(root / "sys/spectral-function/12x12x1", {}).size(), 1u);
		EXPECT_EQ(FindServerHrResults(root / "sys/spectral-function", {}).size(), 1u);
	}

	TEST_F(PhononServerResultsIOTests, CancelledScanAndMissingRootReturnNothing)
	{
		Hr(root / "sys/spectral-function/12x12x1/displacements");
		EXPECT_TRUE(FindServerHrResults(root, [] { return true; }).empty());
		EXPECT_TRUE(FindServerHrResults(root / "missing", {}).empty());
		EXPECT_TRUE(FindServerHrResults(Path{}, {}).empty());
	}
} // namespace DefectStudio::Tests
