#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "Renderer/Scene/SceneOrbitalGeometry.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererWindowState::SceneOrbital MakeShapeTestOrbital(OrbitalPreset preset)
		{
			RendererWindowState::SceneOrbital orbital;
			orbital.preset = preset;
			orbital.resolution = 24;
			return orbital;
		}

		[[nodiscard]] float MeshExtentAlong(
			const std::vector<IsosurfaceVertex> &vertices, const glm::vec3 &centre, const glm::vec3 &axis)
		{
			const glm::vec3 unitAxis = glm::normalize(axis);
			float minimum = std::numeric_limits<float>::max();
			float maximum = std::numeric_limits<float>::lowest();
			for (const IsosurfaceVertex &vertex : vertices)
			{
				const float projection = glm::dot(vertex.position - centre, unitAxis);
				minimum = std::min(minimum, projection);
				maximum = std::max(maximum, projection);
			}
			return maximum - minimum;
		}

		[[nodiscard]] float MeshRadialExtent(
			const std::vector<IsosurfaceVertex> &vertices, const glm::vec3 &centre, const glm::vec3 &axis)
		{
			const glm::vec3 unitAxis = glm::normalize(axis);
			float extent = 0.0f;
			for (const IsosurfaceVertex &vertex : vertices)
			{
				const glm::vec3 offset = vertex.position - centre;
				extent = std::max(extent, glm::length(offset - glm::dot(offset, unitAxis) * unitAxis));
			}
			return extent;
		}
	} // namespace

	TEST(SceneOrbitalShapeTests, StretchUsesTheRotatedOrbitalsOwnAxes)
	{
		const RendererStructureData structure;
		RendererWindowState::SceneOrbital orbital = MakeShapeTestOrbital(OrbitalPreset::P);
		orbital.centerA = glm::vec3(2.0f, -1.0f, 0.5f);
		orbital.rotationEuler = glm::vec3(0.0f, 90.0f, 0.0f);
		const std::vector<IsosurfaceVertex> plain = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_FALSE(plain.empty());

		orbital.stretch.z = 2.0f;
		const std::vector<IsosurfaceVertex> stretched = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_EQ(stretched.size(), plain.size());

		// A 90-degree Y rotation maps the orbital's local z onto world x. Only that extent grows.
		EXPECT_NEAR(MeshExtentAlong(stretched, orbital.centerA, glm::vec3(1.0f, 0.0f, 0.0f)) /
			MeshExtentAlong(plain, orbital.centerA, glm::vec3(1.0f, 0.0f, 0.0f)), 2.0f, 0.02f);
		EXPECT_NEAR(MeshExtentAlong(stretched, orbital.centerA, glm::vec3(0.0f, 1.0f, 0.0f)) /
			MeshExtentAlong(plain, orbital.centerA, glm::vec3(0.0f, 1.0f, 0.0f)), 1.0f, 0.02f);
		EXPECT_NEAR(MeshExtentAlong(stretched, orbital.centerA, glm::vec3(0.0f, 0.0f, 1.0f)) /
			MeshExtentAlong(plain, orbital.centerA, glm::vec3(0.0f, 0.0f, 1.0f)), 1.0f, 0.02f);
	}

	TEST(SceneOrbitalShapeTests, TwoCentreWidthStretchIsSymmetricAndKeepsTheBondLength)
	{
		const RendererStructureData structure;
		RendererWindowState::SceneOrbital orbital = MakeShapeTestOrbital(OrbitalPreset::Sigma);
		orbital.centerA = glm::vec3(-0.75f, -0.5f, -0.25f);
		orbital.centerB = glm::vec3(0.75f, 0.5f, 0.25f);
		const SceneOrbitalCenters centres = ResolveSceneOrbitalCenters(orbital, structure);
		const glm::vec3 bondAxis = orbital.centerB - orbital.centerA;
		const std::vector<IsosurfaceVertex> plain = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_FALSE(plain.empty());

		orbital.stretch = glm::vec3(2.0f, 2.0f, 1.0f);
		const std::vector<IsosurfaceVertex> widened = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_EQ(widened.size(), plain.size());

		EXPECT_NEAR(MeshRadialExtent(widened, centres.centroid, bondAxis) /
			MeshRadialExtent(plain, centres.centroid, bondAxis), 2.0f, 0.02f);
		EXPECT_NEAR(MeshExtentAlong(widened, centres.centroid, bondAxis) /
			MeshExtentAlong(plain, centres.centroid, bondAxis), 1.0f, 0.02f);

		const glm::vec3 unitBond = glm::normalize(bondAxis);
		for (std::size_t index = 0; index < plain.size(); ++index)
		{
			const glm::vec3 plainOffset = plain[index].position - centres.centroid;
			const glm::vec3 widenedOffset = widened[index].position - centres.centroid;
			const glm::vec3 plainRadial = plainOffset - glm::dot(plainOffset, unitBond) * unitBond;
			const glm::vec3 widenedRadial = widenedOffset - glm::dot(widenedOffset, unitBond) * unitBond;
			EXPECT_NEAR(glm::dot(widenedOffset, unitBond), glm::dot(plainOffset, unitBond), 1e-4f);
			EXPECT_LT(glm::length(widenedRadial - plainRadial * 2.0f), 1e-4f);
		}
	}

	TEST(SceneOrbitalShapeTests, TwoCentreAdvancedStretchUsesStableTransverseAxes)
	{
		const RendererStructureData structure;
		RendererWindowState::SceneOrbital orbital = MakeShapeTestOrbital(OrbitalPreset::Sigma);
		orbital.centerA = glm::vec3(0.0f, 0.0f, -0.75f);
		orbital.centerB = glm::vec3(0.0f, 0.0f, 0.75f);
		const std::vector<IsosurfaceVertex> plain = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_FALSE(plain.empty());

		orbital.stretch = glm::vec3(2.0f, 3.0f, 1.0f);
		const std::vector<IsosurfaceVertex> stretched = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_EQ(stretched.size(), plain.size());

		EXPECT_NEAR(MeshExtentAlong(stretched, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)) /
			MeshExtentAlong(plain, glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)), 2.0f, 0.02f);
		EXPECT_NEAR(MeshExtentAlong(stretched, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)) /
			MeshExtentAlong(plain, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), 3.0f, 0.02f);
		EXPECT_NEAR(MeshExtentAlong(stretched, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)) /
			MeshExtentAlong(plain, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)), 1.0f, 0.02f);
	}

	TEST(SceneOrbitalShapeTests, CoincidentTwoCentrePresetUsesThePresetFallbackAxis)
	{
		const RendererStructureData structure;
		RendererWindowState::SceneOrbital orbital = MakeShapeTestOrbital(OrbitalPreset::Sigma);
		orbital.centerA = orbital.centerB = glm::vec3(1.0f, -2.0f, 0.5f);
		const std::vector<IsosurfaceVertex> plain = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_FALSE(plain.empty());

		orbital.stretch.z = 2.0f;
		const std::vector<IsosurfaceVertex> stretched = BuildSceneOrbitalMesh(orbital, structure);
		ASSERT_EQ(stretched.size(), plain.size());

		// OrbitalPresets substitutes a +X bond for coincident centres, so local z is world x.
		EXPECT_NEAR(MeshExtentAlong(stretched, orbital.centerA, glm::vec3(1.0f, 0.0f, 0.0f)) /
			MeshExtentAlong(plain, orbital.centerA, glm::vec3(1.0f, 0.0f, 0.0f)), 2.0f, 0.02f);
		EXPECT_NEAR(MeshExtentAlong(stretched, orbital.centerA, glm::vec3(0.0f, 1.0f, 0.0f)) /
			MeshExtentAlong(plain, orbital.centerA, glm::vec3(0.0f, 1.0f, 0.0f)), 1.0f, 0.02f);
		EXPECT_NEAR(MeshExtentAlong(stretched, orbital.centerA, glm::vec3(0.0f, 0.0f, 1.0f)) /
			MeshExtentAlong(plain, orbital.centerA, glm::vec3(0.0f, 0.0f, 1.0f)), 1.0f, 0.02f);
	}

	TEST(SceneOrbitalShapeTests, InvalidStretchComponentsFallBackToOneWithoutChangingStoredValues)
	{
		const RendererStructureData structure;
		const RendererWindowState::SceneOrbital plainOrbital = MakeShapeTestOrbital(OrbitalPreset::S);
		const std::vector<IsosurfaceVertex> plain = BuildSceneOrbitalMesh(plainOrbital, structure);
		ASSERT_FALSE(plain.empty());

		RendererWindowState::SceneOrbital invalid = plainOrbital;
		invalid.stretch = glm::vec3(0.0f, std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity());
		const std::vector<IsosurfaceVertex> sanitized = BuildSceneOrbitalMesh(invalid, structure);
		ASSERT_EQ(sanitized.size(), plain.size());
		for (std::size_t index = 0; index < plain.size(); ++index)
			EXPECT_LT(glm::length(sanitized[index].position - plain[index].position), 1e-6f);

		EXPECT_FLOAT_EQ(invalid.stretch.x, 0.0f);
		EXPECT_TRUE(std::isnan(invalid.stretch.y));
		EXPECT_TRUE(std::isinf(invalid.stretch.z));
	}

	TEST(SceneOrbitalShapeTests, StretchAloneChangesTheMeshKey)
	{
		const RendererStructureData structure;
		const RendererWindowState::SceneOrbital base = MakeShapeTestOrbital(OrbitalPreset::P);
		RendererWindowState::SceneOrbital stretched = base;
		stretched.stretch.z = 2.0f;

		EXPECT_NE(MakeSceneOrbitalMeshKey(stretched, structure), MakeSceneOrbitalMeshKey(base, structure));
	}

	TEST(SceneOrbitalShapeTests, LargestValidStretchComponentGrowsTheBounds)
	{
		const RendererStructureData structure;
		RendererWindowState::SceneOrbital orbital = MakeShapeTestOrbital(OrbitalPreset::P);
		const float unstretched = SceneOrbitalWorldBounds(orbital, structure).radius;

		orbital.stretch = glm::vec3(2.0f, 3.0f, std::numeric_limits<float>::quiet_NaN());
		const float stretched = SceneOrbitalWorldBounds(orbital, structure).radius;
		EXPECT_NEAR(stretched, unstretched * 3.0f, unstretched * 0.05f);
	}
} // namespace DefectStudio::Tests
