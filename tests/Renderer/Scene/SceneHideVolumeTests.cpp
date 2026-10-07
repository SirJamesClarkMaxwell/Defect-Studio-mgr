#include <gtest/gtest.h>
#include <glm/gtc/quaternion.hpp>
#include "Renderer/Scene/SceneHideVolume.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		RendererStructureData StructureWith(std::vector<glm::vec3> positions, float cubicCell = 10.0f)
		{
			RendererStructureData structure;
			structure.lattice = glm::mat3(cubicCell);
			for (const glm::vec3 &position : positions)
			{
				RendererAtomData atom;
				atom.element = "C";
				atom.cartesianPosition = position;
				structure.atoms.push_back(atom);
			}
			return structure;
		}

		SceneHideVolume Sphere(glm::vec3 center, float radius)
		{
			SceneHideVolume volume;
			volume.id = SceneObjectId{1};
			volume.kind = HideVolumeKind::Sphere;
			volume.frame = HideVolumeFrame::Anchored;
			volume.center = center;
			volume.halfExtents = glm::vec3(radius);
			return volume;
		}

		bool Covers(const SceneHideVolume &volume, glm::vec3 point, const RendererStructureData &structure)
		{
			return PointInHideVolume(volume, point, structure.lattice, structure);
		}
	} // namespace

	// --- shape tests: each surface crossed from both sides ------------------------------------

	TEST(SceneHideVolumeTests, SphereCoversInsideAndNotOutside)
	{
		const RendererStructureData structure = StructureWith({});
		const SceneHideVolume volume = Sphere(glm::vec3(1, 2, 3), 2.0f);

		EXPECT_TRUE(Covers(volume, glm::vec3(1, 2, 3), structure));
		EXPECT_TRUE(Covers(volume, glm::vec3(1, 2, 3) + glm::vec3(0, 0, 1.99f), structure));
		EXPECT_FALSE(Covers(volume, glm::vec3(1, 2, 3) + glm::vec3(0, 0, 2.01f), structure));
		// Along a diagonal, so a per-axis test would wrongly accept this one.
		const glm::vec3 diagonal = glm::normalize(glm::vec3(1, 1, 1)) * 2.01f;
		EXPECT_FALSE(Covers(volume, glm::vec3(1, 2, 3) + diagonal, structure));
	}

	TEST(SceneHideVolumeTests, BoxCoversPerAxisHalfSides)
	{
		const RendererStructureData structure = StructureWith({});
		SceneHideVolume volume = Sphere(glm::vec3(0.0f), 1.0f);
		volume.kind = HideVolumeKind::Box;
		volume.halfExtents = glm::vec3(1, 2, 3);

		EXPECT_TRUE(Covers(volume, glm::vec3(0.99f, 1.99f, 2.99f), structure));
		EXPECT_FALSE(Covers(volume, glm::vec3(1.01f, 0, 0), structure));
		EXPECT_FALSE(Covers(volume, glm::vec3(0, 2.01f, 0), structure));
		EXPECT_FALSE(Covers(volume, glm::vec3(0, 0, 3.01f), structure));
		// A corner is outside even though every coordinate is within the largest half-side.
		EXPECT_FALSE(Covers(volume, glm::vec3(2.5f, 2.5f, 2.5f), structure));
	}

	TEST(SceneHideVolumeTests, BoxRespectsItsOwnOrientation)
	{
		const RendererStructureData structure = StructureWith({});
		SceneHideVolume volume = Sphere(glm::vec3(0.0f), 1.0f);
		volume.kind = HideVolumeKind::Box;
		volume.halfExtents = glm::vec3(3, 0.5f, 0.5f);
		volume.orientation = glm::mat3_cast(glm::quat(glm::radians(glm::vec3(0, 0, 90))));

		// The long axis now runs along world +Y, not +X.
		EXPECT_TRUE(Covers(volume, glm::vec3(0, 2.5f, 0), structure));
		EXPECT_FALSE(Covers(volume, glm::vec3(2.5f, 0, 0), structure));
	}

	TEST(SceneHideVolumeTests, CylinderTestsRadiusAndAxialSpanSeparately)
	{
		const RendererStructureData structure = StructureWith({});
		SceneHideVolume volume = Sphere(glm::vec3(0.0f), 1.0f);
		volume.kind = HideVolumeKind::Cylinder;
		volume.halfExtents = glm::vec3(2.0f, 0.0f, 5.0f); // radius 2, half-height 5 along +Z

		EXPECT_TRUE(Covers(volume, glm::vec3(1.99f, 0, 4.99f), structure));
		EXPECT_FALSE(Covers(volume, glm::vec3(2.01f, 0, 0), structure)); // outside the radius
		EXPECT_FALSE(Covers(volume, glm::vec3(0, 0, 5.01f), structure)); // past the end cap
		// Radial distance is the full in-plane distance, not a per-axis one.
		const glm::vec3 radial = glm::normalize(glm::vec3(1, 1, 0)) * 2.01f;
		EXPECT_FALSE(Covers(volume, radial, structure));
	}

	TEST(SceneHideVolumeTests, InvertFlipsEveryShape)
	{
		const RendererStructureData structure = StructureWith({});
		for (const HideVolumeKind kind : {HideVolumeKind::Sphere, HideVolumeKind::Box, HideVolumeKind::Cylinder})
		{
			SceneHideVolume volume = Sphere(glm::vec3(0.0f), 2.0f);
			volume.kind = kind;
			volume.halfExtents = glm::vec3(2.0f, 2.0f, 2.0f);
			const glm::vec3 inside(0.1f, 0.1f, 0.1f);
			const glm::vec3 outside(50.0f, 50.0f, 50.0f);

			EXPECT_TRUE(Covers(volume, inside, structure));
			EXPECT_FALSE(Covers(volume, outside, structure));

			volume.invert = true;
			EXPECT_FALSE(Covers(volume, inside, structure));
			EXPECT_TRUE(Covers(volume, outside, structure));
		}
	}

	// --- anchoring ---------------------------------------------------------------------------

	TEST(SceneHideVolumeTests, AnchoredCenterFollowsTheAtomsAndIgnoresStoredCenter)
	{
		const RendererStructureData structure = StructureWith({glm::vec3(7, 0, 0), glm::vec3(9, 0, 0)});
		SceneHideVolume volume = Sphere(glm::vec3(-100.0f), 1.5f);
		volume.anchorAtoms = {0, 1};

		const std::optional<glm::vec3> center = ResolveHideVolumeCenter(volume, structure);
		ASSERT_TRUE(center.has_value());
		EXPECT_NEAR(glm::length(*center - glm::vec3(8, 0, 0)), 0.0f, 1e-5f);
		// The stored centre at -100 must not be what the point test uses.
		EXPECT_TRUE(Covers(volume, glm::vec3(8, 0, 0), structure));
	}

	TEST(SceneHideVolumeTests, AnchorsOutOfRangeFallBackToTheStoredCenter)
	{
		const RendererStructureData structure = StructureWith({glm::vec3(7, 0, 0)});
		SceneHideVolume volume = Sphere(glm::vec3(1, 1, 1), 1.0f);
		volume.anchorAtoms = {99};

		EXPECT_FALSE(ResolveHideVolumeCenter(volume, structure).has_value());
		EXPECT_TRUE(Covers(volume, glm::vec3(1, 1, 1), structure));
	}

	TEST(SceneHideVolumeTests, PartiallyResolvableAnchorsUseWhatIsLeft)
	{
		const RendererStructureData structure = StructureWith({glm::vec3(4, 0, 0)});
		SceneHideVolume volume = Sphere(glm::vec3(-100.0f), 1.0f);
		volume.anchorAtoms = {0, 99};

		const std::optional<glm::vec3> center = ResolveHideVolumeCenter(volume, structure);
		ASSERT_TRUE(center.has_value());
		EXPECT_NEAR(glm::length(*center - glm::vec3(4, 0, 0)), 0.0f, 1e-5f);
	}

	// --- the reason the feature exists: a preset that survives a change of cell ---------------

	TEST(SceneHideVolumeTests, AnchoredVolumeCoversTheSamePhysicalRadiusInEverySupercell)
	{
		// The same preset - "3 A around the anchor atom" - applied to a small cell and to a cell
		// four times larger. An Anchored volume is in Angstrom, so the physical ball is identical.
		const RendererStructureData small = StructureWith({glm::vec3(5, 5, 5)}, 10.0f);
		const RendererStructureData large = StructureWith({glm::vec3(5, 5, 5)}, 40.0f);

		SceneHideVolume volume = Sphere(glm::vec3(0.0f), 3.0f);
		volume.anchorAtoms = {0};

		for (const RendererStructureData *structure : {&small, &large})
		{
			EXPECT_TRUE(Covers(volume, glm::vec3(5, 5, 5) + glm::vec3(2.9f, 0, 0), *structure));
			EXPECT_FALSE(Covers(volume, glm::vec3(5, 5, 5) + glm::vec3(3.1f, 0, 0), *structure));
		}
	}

	TEST(SceneHideVolumeTests, FractionalVolumeScalesWithTheCell)
	{
		// The mirror image of the test above: a Fractional volume is a fraction of the lattice, so
		// the same preset covers a physically larger region in a larger cell. Both behaviours have
		// to hold, or "a preset for diamond" quietly means the wrong thing after a supercell change.
		const RendererStructureData small = StructureWith({}, 10.0f);
		const RendererStructureData large = StructureWith({}, 40.0f);

		SceneHideVolume volume = Sphere(glm::vec3(0.5f, 0.5f, 0.5f), 0.2f);
		volume.frame = HideVolumeFrame::Fractional;

		// 0.2 of a 10 A cell is 2 A; of a 40 A cell, 8 A. The cell centre is 5 A and 20 A.
		EXPECT_TRUE(Covers(volume, glm::vec3(5 + 1.9f, 5, 5), small));
		EXPECT_FALSE(Covers(volume, glm::vec3(5 + 2.1f, 5, 5), small));
		EXPECT_TRUE(Covers(volume, glm::vec3(20 + 7.9f, 20, 20), large));
		EXPECT_FALSE(Covers(volume, glm::vec3(20 + 8.1f, 20, 20), large));
	}

	TEST(SceneHideVolumeTests, SingularLatticeMakesFractionalTestsFalseInsteadOfNaN)
	{
		RendererStructureData structure = StructureWith({});
		structure.lattice = glm::mat3(0.0f);

		SceneHideVolume volume = Sphere(glm::vec3(0.5f), 0.5f);
		volume.frame = HideVolumeFrame::Fractional;

		EXPECT_FALSE(Covers(volume, glm::vec3(0, 0, 0), structure));
		EXPECT_FALSE(Covers(volume, glm::vec3(5, 5, 5), structure));
	}

	// --- the mask the rest of the renderer consumes -------------------------------------------

	TEST(SceneHideVolumeTests, CoveredAtomsAreAscendingUniqueAcrossVolumes)
	{
		const RendererStructureData structure = StructureWith(
			{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(20, 0, 0), glm::vec3(21, 0, 0)});

		const SceneHideVolume first = Sphere(glm::vec3(0.5f, 0, 0), 2.0f); // atoms 0, 1
		SceneHideVolume second = Sphere(glm::vec3(1.0f, 0, 0), 2.0f);      // atoms 0, 1 again
		second.id = SceneObjectId{2};
		SceneHideVolume third = Sphere(glm::vec3(20.5f, 0, 0), 2.0f);      // atoms 2, 3
		third.id = SceneObjectId{3};

		const std::vector<SceneHideVolume> volumes{first, second, third};
		EXPECT_EQ(AtomsCoveredByHideVolumes(structure, volumes), (std::vector<std::size_t>{0, 1, 2, 3}));
	}

	TEST(SceneHideVolumeTests, VolumeWithTheEyeClearedContributesNothing)
	{
		const RendererStructureData structure = StructureWith({glm::vec3(0, 0, 0)});
		SceneHideVolume volume = Sphere(glm::vec3(0.0f), 2.0f);

		EXPECT_EQ(AtomsCoveredByHideVolumes(structure, std::vector<SceneHideVolume>{volume}).size(), 1u);

		volume.visible = false;
		EXPECT_TRUE(AtomsCoveredByHideVolumes(structure, std::vector<SceneHideVolume>{volume}).empty());
	}

	TEST(SceneHideVolumeTests, NoVolumesCoverNothing)
	{
		const RendererStructureData structure = StructureWith({glm::vec3(0, 0, 0)});
		EXPECT_TRUE(AtomsCoveredByHideVolumes(structure, std::span<const SceneHideVolume>{}).empty());
	}
} // namespace DefectStudio::Tests
