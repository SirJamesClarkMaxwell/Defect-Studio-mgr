#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/Scene/SceneHideVolume.hpp"

namespace DefectStudio::Tests
{
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

		// Every vertex on the surface: 2 % in is covered, 2 % out is not.
		void ExpectVerticesOnSurface(const SceneHideVolume &volume, const RendererStructureData &structure)
		{
			const std::vector<glm::vec3> triangles = BuildHideVolumeTriangles(volume, structure);
			ASSERT_FALSE(triangles.empty());
			ASSERT_EQ(triangles.size() % 3, 0u);

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
	} // namespace

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
