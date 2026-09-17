#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

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
	// An analytic orbital's sampling box is centred on whichever atom it belongs to, not on the
	// scene origin (see HydrogenicOrbital.hpp), so the mesh has to follow OrbitalGridData::origin.
	// A WAVECAR grid leaves it at zero, which is why every case above is unaffected.
	TEST(IsosurfaceMesherTests, GridOriginOffsetsTheWholeMesh)
	{
		OrbitalGridData grid = BuildLinearGradientGrid();
		const std::vector<IsosurfaceVertex> atOrigin = GenerateIsosurfaceMesh(grid, 1.0f);
		ASSERT_FALSE(atOrigin.empty());

		const glm::vec3 offset(3.0f, -7.0f, 0.5f);
		grid.origin = offset;
		const std::vector<IsosurfaceVertex> shifted = GenerateIsosurfaceMesh(grid, 1.0f);

		ASSERT_EQ(shifted.size(), atOrigin.size());
		for (std::size_t index = 0; index < shifted.size(); ++index)
		{
			EXPECT_NEAR(shifted[index].position.x, atOrigin[index].position.x + offset.x, 1e-5f);
			EXPECT_NEAR(shifted[index].position.y, atOrigin[index].position.y + offset.y, 1e-5f);
			EXPECT_NEAR(shifted[index].position.z, atOrigin[index].position.z + offset.z, 1e-5f);
			// A translation cannot turn a surface, so the normals and the phase tags stay put.
			EXPECT_NEAR(glm::length(shifted[index].normal - atOrigin[index].normal), 0.0f, 1e-5f);
			EXPECT_EQ(shifted[index].sign, atOrigin[index].sign);
		}
	}
} // namespace DefectStudio::Tests
