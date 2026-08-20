#include <gtest/gtest.h>

#include <algorithm>

#include "Renderer/Scene/IsosurfaceMesher.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Linear gradient along x: value ranges from -2.5 to 2.5 across the grid, so it crosses
		// both +iso and -iso for any 0 < iso < 2.5 - exercises both lobes in one fixture.
		[[nodiscard]] OrbitalGridData BuildLinearGradientGrid()
		{
			OrbitalGridData grid;
			grid.dimensions = glm::ivec3(6, 6, 6);
			grid.cell = glm::mat3(1.0f);
			grid.values.resize(6u * 6u * 6u);
			for (int i = 0; i < 6; ++i)
				for (int j = 0; j < 6; ++j)
					for (int k = 0; k < 6; ++k)
						grid.values[static_cast<std::size_t>(i) * 36 + static_cast<std::size_t>(j) * 6 +
							static_cast<std::size_t>(k)] = static_cast<float>(i) - 2.5f;
			return grid;
		}

		// A rounded blob centered in the grid, clamped to >= 0 everywhere - crosses +iso partway
		// out from the center but never goes negative, so only the positive lobe should appear.
		[[nodiscard]] OrbitalGridData BuildPositiveBlobGrid()
		{
			OrbitalGridData grid;
			grid.dimensions = glm::ivec3(6, 6, 6);
			grid.cell = glm::mat3(1.0f);
			grid.values.resize(6u * 6u * 6u);
			const glm::vec3 center(2.5f, 2.5f, 2.5f);
			for (int i = 0; i < 6; ++i)
				for (int j = 0; j < 6; ++j)
					for (int k = 0; k < 6; ++k)
					{
						const float distance = glm::length(glm::vec3(i, j, k) - center);
						grid.values[static_cast<std::size_t>(i) * 36 + static_cast<std::size_t>(j) * 6 +
							static_cast<std::size_t>(k)] = std::max(0.0f, 3.0f - distance);
					}
			return grid;
		}
	} // namespace

	TEST(IsosurfaceMesherTests, ReturnsEmptyForNonPositiveIsoValue)
	{
		EXPECT_TRUE(GenerateIsosurfaceMesh(BuildLinearGradientGrid(), 0.0f).empty());
		EXPECT_TRUE(GenerateIsosurfaceMesh(BuildLinearGradientGrid(), -1.0f).empty());
	}

	TEST(IsosurfaceMesherTests, ReturnsEmptyWhenIsoValueExceedsFieldRange)
	{
		EXPECT_TRUE(GenerateIsosurfaceMesh(BuildLinearGradientGrid(), 100.0f).empty());
	}

	TEST(IsosurfaceMesherTests, ProducesTriangleTriplesWithinFieldBounds)
	{
		const std::vector<IsosurfaceVertex> vertices = GenerateIsosurfaceMesh(BuildLinearGradientGrid(), 1.0f);

		ASSERT_FALSE(vertices.empty());
		EXPECT_EQ(vertices.size() % 3, 0u);

		for (const IsosurfaceVertex &vertex : vertices)
		{
			EXPECT_NEAR(glm::length(vertex.normal), 1.0f, 1e-3f);
			EXPECT_GE(vertex.position.x, 0.0f);
			EXPECT_LE(vertex.position.x, 1.0f);
		}
	}

	TEST(IsosurfaceMesherTests, BothLobesPresentWhenFieldCrossesBothThresholds)
	{
		const std::vector<IsosurfaceVertex> vertices = GenerateIsosurfaceMesh(BuildLinearGradientGrid(), 1.0f);

		bool hasPositiveLobe = false;
		bool hasNegativeLobe = false;
		for (const IsosurfaceVertex &vertex : vertices)
		{
			hasPositiveLobe |= vertex.sign > 0.0f;
			hasNegativeLobe |= vertex.sign < 0.0f;
		}
		EXPECT_TRUE(hasPositiveLobe);
		EXPECT_TRUE(hasNegativeLobe);
	}

	TEST(IsosurfaceMesherTests, OnlyPositiveLobeWhenFieldNeverGoesNegative)
	{
		const std::vector<IsosurfaceVertex> vertices = GenerateIsosurfaceMesh(BuildPositiveBlobGrid(), 1.0f);

		ASSERT_FALSE(vertices.empty());
		for (const IsosurfaceVertex &vertex : vertices)
			EXPECT_GT(vertex.sign, 0.0f);
	}
} // namespace DefectStudio::Tests
