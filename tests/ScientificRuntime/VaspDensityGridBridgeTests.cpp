#include <gtest/gtest.h>

#include <algorithm>

#include "ScientificRuntime/Python/VaspDensityGridBridge.hpp"
#include "ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// fixtures/density/CHGCAR: cubic a = 2 A, 4x4x4, ISPIN=2. Total density 1 e/A^3 everywhere;
		// magnetization +0.5 e/A^3 on the x = 0 slab, -0.25 on x = 1, zero elsewhere.
		[[nodiscard]] Path Chgcar() { return ScientificRuntimeFixturePath("density/CHGCAR"); }
	} // namespace

	TEST(VaspDensityGridBridgeTests, ReadsTheSpinDensityInPhysicalUnitsAndCOrder)
	{
		const Result<DensityGrid> result = VaspDensityGridBridge{}.LoadDensityGrid(Chgcar(), DensityComponent::Magnetization);
		if (!result)
			GTEST_SKIP() << "puntukas unavailable: " << result.Error().technicalDetails;

		const OrbitalGridData &grid = result->grid;
		EXPECT_EQ(grid.dimensions, glm::ivec3(4));
		EXPECT_FLOAT_EQ(grid.cell[0].x, 2.0f);
		ASSERT_EQ(grid.values.size(), 64u);
		// x slowest: the first 16 values are the x = 0 slab, the next 16 the x = 1 slab.
		EXPECT_TRUE(std::all_of(grid.values.begin(), grid.values.begin() + 16, [](float v) { return v == 0.5f; }));
		EXPECT_TRUE(std::all_of(grid.values.begin() + 16, grid.values.begin() + 32, [](float v) { return v == -0.25f; }));
		EXPECT_NEAR(result->statistics.integral, 0.5f, 1e-5f);
		EXPECT_NEAR(result->statistics.absIntegral, 1.5f, 1e-5f);
		EXPECT_FLOAT_EQ(result->statistics.minimum, -0.25f);
		EXPECT_FLOAT_EQ(result->statistics.maximum, 0.5f);
		EXPECT_EQ(result->statistics.atomCount, 1);
	}

	TEST(VaspDensityGridBridgeTests, TotalAndSpinChannelsAndReferenceSubtraction)
	{
		const VaspDensityGridBridge bridge;
		const Result<DensityGrid> total = bridge.LoadDensityGrid(Chgcar(), DensityComponent::Total);
		if (!total)
			GTEST_SKIP() << "puntukas unavailable: " << total.Error().technicalDetails;
		EXPECT_NEAR(total->statistics.integral, 8.0f, 1e-4f);

		const Result<DensityGrid> up = bridge.LoadDensityGrid(Chgcar(), DensityComponent::SpinUp);
		ASSERT_TRUE(up) << up.Error().technicalDetails;
		EXPECT_FLOAT_EQ(up->grid.values.front(), 0.75f); // (1 + 0.5) / 2

		const Result<DensityGrid> difference = bridge.LoadDensityGrid(Chgcar(), DensityComponent::Total, Chgcar());
		ASSERT_TRUE(difference) << difference.Error().technicalDetails;
		EXPECT_FLOAT_EQ(difference->statistics.maximum, 0.0f);
		EXPECT_FLOAT_EQ(difference->statistics.minimum, 0.0f);
	}

	TEST(VaspDensityGridBridgeTests, MissingFileIsAnError)
	{
		const Result<DensityGrid> result =
			VaspDensityGridBridge{}.LoadDensityGrid(ScientificRuntimeFixturePath("density/NO_SUCH_CHGCAR"), DensityComponent::Total);
		EXPECT_FALSE(result);
	}
} // namespace DefectStudio::Tests
