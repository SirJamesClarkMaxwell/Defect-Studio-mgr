#include <gtest/gtest.h>

#include <cmath>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/Scene/SceneHideVolume.hpp"
#include "Renderer/Scene/SceneHideVolumeSilhouette.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneHideVolumeMeshTests, PerspectiveSilhouetteLiesOnTheSphereAndEveryCameraRayIsTangent)
	{
		RendererStructureData structure;
		structure.atoms = {{"C", {1, 2, 3}}};
		SceneHideVolume volume;
		volume.anchorAtoms = {0};
		volume.center = glm::vec3(100); // The resolved anchor, not this stored centre, is used.
		volume.halfExtents.x = 2;
		RendererViewCamera camera;
		camera.SetProjection(CameraProjection::Perspective);
		camera.SetOrbitState(glm::vec3(0), 9, 0.7f, 0.4f);
		const glm::vec3 offset(2, -1, 0.5f);
		const glm::vec3 center = structure.atoms.front().cartesianPosition;
		const glm::vec3 eye = camera.Position() - offset;
		const float distance = glm::length(camera.Position() - center - offset);
		const glm::vec3 normal = (eye - center) / distance;
		const glm::vec3 circleCenter = center + 4.0f / distance * normal;
		const float circleRadius = 2.0f * std::sqrt(1.0f - 4.0f / (distance * distance));
		const auto edges = BuildHideVolumeSilhouette(volume, structure, camera, offset);
		ASSERT_EQ(edges.size(), 192u);
		for (const auto &point : edges)
		{
			EXPECT_NEAR(glm::length(point - center), 2.0f, 1e-5f);
			EXPECT_NEAR(glm::dot(point - center, eye - point), 0.0f, 2e-5f);
			EXPECT_NEAR(glm::dot(point - circleCenter, normal), 0.0f, 1e-5f);
			EXPECT_NEAR(glm::length(point - circleCenter), circleRadius, 1e-5f);
		}
		volume.frame = HideVolumeFrame::Fractional;
		EXPECT_TRUE(BuildHideVolumeSilhouette(volume, structure, camera, offset).empty());
		volume.frame = HideVolumeFrame::Anchored;
		volume.halfExtents.x = distance;
		EXPECT_TRUE(BuildHideVolumeSilhouette(volume, structure, camera, offset).empty());
		volume.halfExtents.x = distance + 1;
		EXPECT_TRUE(BuildHideVolumeSilhouette(volume, structure, camera, offset).empty());
	}

	TEST(SceneHideVolumeMeshTests, OrthographicSilhouetteHasTheFullRadiusPerpendicularToTheView)
	{
		RendererStructureData structure;
		SceneHideVolume volume;
		volume.center = glm::vec3(1, 2, 3);
		volume.halfExtents.x = 2;
		RendererViewCamera camera;
		camera.SetOrbitState(glm::vec3(0), 10, 0.7f, 0.5f);
		const auto view = camera.ViewMatrix();
		const glm::vec3 normal(view[0][2], view[1][2], view[2][2]);
		const auto edges = BuildHideVolumeSilhouette(volume, structure, camera, glm::vec3(3));
		ASSERT_EQ(edges.size(), 192u);
		for (const auto &point : edges)
		{
			EXPECT_NEAR(glm::length(point - volume.center), 2.0f, 1e-5f);
			EXPECT_NEAR(glm::dot(point - volume.center, normal), 0.0f, 1e-5f);
		}
		volume.halfExtents.x = 0;
		EXPECT_TRUE(BuildHideVolumeSilhouette(volume, structure, camera, glm::vec3(0)).empty());
		volume.halfExtents.x = 2;
		volume.kind = HideVolumeKind::Box;
		EXPECT_TRUE(BuildHideVolumeSilhouette(volume, structure, camera, glm::vec3(0)).empty());
	}

	namespace
	{
		[[nodiscard]] RendererStructureData StructureWithLattice(const glm::mat3 &lattice)
		{
			RendererStructureData structure;
			structure.lattice = lattice;
			for (int index = 0; index < 3; ++index)
			{
				RendererAtomData atom;
				atom.element = "C";
				atom.cartesianPosition = glm::vec3(2.0f + static_cast<float>(index), 3.0f, 4.0f);
				structure.atoms.push_back(atom);
			}
			return structure;
		}

		[[nodiscard]] SceneHideVolume Volume(HideVolumeKind kind, HideVolumeFrame frame)
		{
			SceneHideVolume volume;
			volume.kind = kind;
			volume.frame = frame;
			if (frame == HideVolumeFrame::Anchored)
			{
				volume.center = glm::vec3(1.0f, -2.0f, 0.5f);
				volume.halfExtents = glm::vec3(1.5f, 0.8f, 2.0f);
			}
			else
			{
				volume.center = glm::vec3(0.5f, 0.4f, 0.6f);
				volume.halfExtents = glm::vec3(0.2f, 0.15f, 0.25f);
			}
			// Tilted, so a mesh built axis-aligned by mistake fails the surface check.
			volume.orientation = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(30.0f), glm::normalize(glm::vec3(1, 2, 3))));
			return volume;
		}

		// Every point on the surface: 2 % in is covered, 2 % out is not.
		void ExpectPointsOnSurface(
			const SceneHideVolume &volume, const RendererStructureData &structure, const std::vector<glm::vec3> &triangles)
		{
			SceneHideVolume plain = volume;
			plain.invert = false;
			const glm::vec3 centre = HideVolumeWorldCenter(volume, structure);
			for (const glm::vec3 &vertex : triangles)
			{
				const glm::vec3 inward = centre + 0.98f * (vertex - centre);
				const glm::vec3 outward = centre + 1.02f * (vertex - centre);
				EXPECT_TRUE(PointInHideVolume(plain, inward, structure.lattice, structure));
				EXPECT_FALSE(PointInHideVolume(plain, outward, structure.lattice, structure));
			}
		}

		void ExpectVerticesOnSurface(const SceneHideVolume &volume, const RendererStructureData &structure)
		{
			const std::vector<glm::vec3> triangles = BuildHideVolumeTriangles(volume, structure);
			ASSERT_FALSE(triangles.empty());
			ASSERT_EQ(triangles.size() % 3, 0u);
			ExpectPointsOnSurface(volume, structure, triangles);
		}

		[[nodiscard]] RendererStructureData SkewedCell()
		{
			return StructureWithLattice(glm::mat3(glm::vec3(10, 0, 0), glm::vec3(3, 8, 0), glm::vec3(1, 2, 12)));
		}
	} // namespace

	TEST(SceneHideVolumeMeshTests, TheWireframeLiesOnTheSurface)
	{
		const RendererStructureData structure = SkewedCell();
		for (const HideVolumeKind kind : {HideVolumeKind::Sphere, HideVolumeKind::Box, HideVolumeKind::Cylinder})
			for (const HideVolumeFrame frame : {HideVolumeFrame::Anchored, HideVolumeFrame::Fractional})
			{
				SCOPED_TRACE(static_cast<int>(kind) * 10 + static_cast<int>(frame));
				const SceneHideVolume volume = Volume(kind, frame);
				const std::vector<glm::vec3> lines = BuildHideVolumeEdges(volume, structure);
				ASSERT_FALSE(lines.empty());
				ASSERT_EQ(lines.size() % 2, 0u);
				ExpectPointsOnSurface(volume, structure, lines);
			}
		EXPECT_EQ(BuildHideVolumeEdges(Volume(HideVolumeKind::Box, HideVolumeFrame::Anchored), structure).size(), 24u);
	}

	TEST(SceneHideVolumeMeshTests, TheUnitTransformAgreesWithThePointTest)
	{
		// The shader clip and the atom mask must cut the same region.
		const RendererStructureData structure = SkewedCell();
		for (const HideVolumeKind kind : {HideVolumeKind::Sphere, HideVolumeKind::Box, HideVolumeKind::Cylinder})
			for (const HideVolumeFrame frame : {HideVolumeFrame::Anchored, HideVolumeFrame::Fractional})
			{
				SCOPED_TRACE(static_cast<int>(kind) * 10 + static_cast<int>(frame));
				const SceneHideVolume volume = Volume(kind, frame);
				const std::optional<glm::mat4> toUnit = HideVolumeWorldToUnit(volume, structure);
				ASSERT_TRUE(toUnit.has_value());
				const glm::vec3 centre = HideVolumeWorldCenter(volume, structure);
				for (int step = 0; step < 400; ++step)
				{
					// A deterministic spray around the centre, well past the surface.
					const float t = static_cast<float>(step);
					const glm::vec3 point = centre + 4.0f * glm::vec3(std::sin(t * 1.3f), std::cos(t * 0.7f), std::sin(t * 2.1f + 1.0f));
					const glm::vec3 unit = glm::vec3(*toUnit * glm::vec4(point, 1.0f));
					const bool insideUnit = kind == HideVolumeKind::Sphere ? glm::dot(unit, unit) <= 1.0f
						: kind == HideVolumeKind::Box ? glm::all(glm::lessThanEqual(glm::abs(unit), glm::vec3(1.0f)))
						: (unit.x * unit.x + unit.y * unit.y <= 1.0f && std::abs(unit.z) <= 1.0f);
					const float margin = kind == HideVolumeKind::Box ? glm::abs(glm::max(glm::max(std::abs(unit.x), std::abs(unit.y)), std::abs(unit.z)) - 1.0f)
						: glm::abs(glm::length(kind == HideVolumeKind::Sphere ? unit : glm::vec3(unit.x, unit.y, 0.0f)) - 1.0f);
					if (margin < 1e-3f)
						continue; // on the surface, either answer is fine
					EXPECT_EQ(insideUnit, PointInHideVolume(volume, point, structure.lattice, structure)) << step;
				}
			}
		const RendererStructureData singular = StructureWithLattice(glm::mat3(0.0f));
		EXPECT_FALSE(HideVolumeWorldToUnit(Volume(HideVolumeKind::Sphere, HideVolumeFrame::Fractional), singular).has_value());
	}

	TEST(SceneHideVolumeMeshTests, CoverageCountsActiveVolumesOnly)
	{
		const RendererStructureData structure = StructureWithLattice(glm::mat3(10.0f));
		SceneHideVolume volume = Volume(HideVolumeKind::Sphere, HideVolumeFrame::Anchored);
		const glm::vec3 centre = HideVolumeWorldCenter(volume, structure);
		std::vector<SceneHideVolume> volumes{volume};
		EXPECT_TRUE(PointCoveredByHideVolumes(volumes, centre, structure));
		volumes[0].invert = true;
		EXPECT_FALSE(PointCoveredByHideVolumes(volumes, centre, structure));
		volumes[0].invert = false;
		volumes[0].active = false;
		EXPECT_FALSE(PointCoveredByHideVolumes(volumes, centre, structure));
	}

	TEST(SceneHideVolumeMeshTests, EveryShapeAndFrameMeshesItsOwnSurface)
	{
		// Skewed, non-cubic cell: a Fractional sphere is an ellipsoid and a box a parallelepiped.
		const RendererStructureData structure =
			StructureWithLattice(glm::mat3(glm::vec3(10, 0, 0), glm::vec3(3, 8, 0), glm::vec3(1, 2, 12)));
		for (const HideVolumeKind kind : {HideVolumeKind::Sphere, HideVolumeKind::Box, HideVolumeKind::Cylinder})
			for (const HideVolumeFrame frame : {HideVolumeFrame::Anchored, HideVolumeFrame::Fractional})
			{
				SCOPED_TRACE(static_cast<int>(kind) * 10 + static_cast<int>(frame));
				ExpectVerticesOnSurface(Volume(kind, frame), structure);
			}
	}

	TEST(SceneHideVolumeMeshTests, InvertDoesNotChangeTheSurface)
	{
		const RendererStructureData structure = StructureWithLattice(glm::mat3(10.0f));
		SceneHideVolume volume = Volume(HideVolumeKind::Box, HideVolumeFrame::Anchored);
		const std::vector<glm::vec3> plain = BuildHideVolumeTriangles(volume, structure);
		volume.invert = true;
		EXPECT_EQ(BuildHideVolumeTriangles(volume, structure), plain);
	}

	TEST(SceneHideVolumeMeshTests, AnAnchoredMeshFollowsItsAtoms)
	{
		const RendererStructureData structure = StructureWithLattice(glm::mat3(10.0f));
		SceneHideVolume volume = Volume(HideVolumeKind::Sphere, HideVolumeFrame::Anchored);
		volume.anchorAtoms = {0, 2};
		const glm::vec3 centroid(3.0f, 3.0f, 4.0f);
		EXPECT_NEAR(glm::distance(HideVolumeWorldCenter(volume, structure), centroid), 0.0f, 1e-5f);
		ExpectVerticesOnSurface(volume, structure);
	}

	TEST(SceneHideVolumeMeshTests, AFractionalCentreIsMappedThroughTheLattice)
	{
		const RendererStructureData structure = StructureWithLattice(glm::mat3(glm::vec3(10, 0, 0), glm::vec3(0, 20, 0), glm::vec3(0, 0, 30)));
		const SceneHideVolume volume = Volume(HideVolumeKind::Sphere, HideVolumeFrame::Fractional);
		EXPECT_NEAR(glm::distance(HideVolumeWorldCenter(volume, structure), glm::vec3(5.0f, 8.0f, 18.0f)), 0.0f, 1e-4f);
	}

	TEST(SceneHideVolumeMeshTests, DegenerateInputsGiveNoMesh)
	{
		const RendererStructureData singular = StructureWithLattice(glm::mat3(0.0f));
		EXPECT_TRUE(BuildHideVolumeTriangles(Volume(HideVolumeKind::Sphere, HideVolumeFrame::Fractional), singular).empty());

		const RendererStructureData structure = StructureWithLattice(glm::mat3(10.0f));
		SceneHideVolume flat = Volume(HideVolumeKind::Box, HideVolumeFrame::Anchored);
		flat.halfExtents.y = 0.0f;
		EXPECT_TRUE(BuildHideVolumeTriangles(flat, structure).empty());
	}
} // namespace DefectStudio::Tests
