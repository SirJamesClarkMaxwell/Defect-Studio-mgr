#include <gtest/gtest.h>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneHideVolume.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Four atoms in a row, three bonds chaining them. Atom 0 sits at the origin, so a small
		// volume there covers it alone and the 0-1 bond is the only one with a covered endpoint.
		RendererWindowState WindowWithChain()
		{
			RendererWindowState windowState;
			windowState.structure.lattice = glm::mat3(50.0f);
			for (int index = 0; index < 4; ++index)
			{
				RendererAtomData atom;
				atom.element = "C";
				atom.cartesianPosition = glm::vec3(static_cast<float>(index) * 5.0f, 0.0f, 0.0f);
				windowState.structure.atoms.push_back(atom);
			}
			for (std::uint32_t index = 0; index + 1 < 4; ++index)
			{
				RendererBondData bond;
				bond.firstAtomIndex = index;
				bond.secondAtomIndex = index + 1;
				windowState.structure.bonds.push_back(bond);
			}
			return windowState;
		}

		SceneHideVolume SphereAtOrigin(float radius)
		{
			SceneHideVolume volume;
			volume.id = SceneObjectId{1};
			volume.kind = HideVolumeKind::Sphere;
			volume.frame = HideVolumeFrame::Anchored;
			volume.center = glm::vec3(0.0f);
			volume.halfExtents = glm::vec3(radius);
			return volume;
		}
	} // namespace

	TEST(SceneHideVolumeMaskTests, CoveredAtomsLoseTheEyeAndTheRestKeepIt)
	{
		RendererWindowState windowState = WindowWithChain();
		windowState.sceneHideVolumes.push_back(SphereAtOrigin(2.0f)); // atom 0 only

		ApplyHideVolumeMaskToWindowState(windowState);

		EXPECT_FALSE(windowState.structure.atoms[0].visible);
		EXPECT_TRUE(windowState.structure.atoms[1].visible);
		EXPECT_TRUE(windowState.structure.atoms[2].visible);
		EXPECT_TRUE(windowState.structure.atoms[3].visible);
	}

	TEST(SceneHideVolumeMaskTests, AManualHideOutsideEveryVolumeSurvivesTheMask)
	{
		// The whole reason the mask is a mask and not an owner of VisibilityComponent: editing a
		// radius must not resurrect what the user hid by hand with H.
		RendererWindowState windowState = WindowWithChain();
		windowState.structure.atoms[3].visible = false; // hidden by hand, far from the volume
		windowState.sceneHideVolumes.push_back(SphereAtOrigin(2.0f));

		ApplyHideVolumeMaskToWindowState(windowState);

		EXPECT_FALSE(windowState.structure.atoms[0].visible); // by the volume
		EXPECT_FALSE(windowState.structure.atoms[3].visible); // still by hand
	}

	TEST(SceneHideVolumeMaskTests, ABondGoesWithEitherOfItsEndpoints)
	{
		RendererWindowState windowState = WindowWithChain();
		windowState.sceneHideVolumes.push_back(SphereAtOrigin(2.0f)); // atom 0

		ApplyHideVolumeMaskToWindowState(windowState);

		EXPECT_FALSE(windowState.structure.bonds[0].visible); // 0-1, one endpoint covered
		EXPECT_TRUE(windowState.structure.bonds[1].visible);  // 1-2
		EXPECT_TRUE(windowState.structure.bonds[2].visible);  // 2-3
	}

	TEST(SceneHideVolumeMaskTests, TheTwoColumnsAreMaskedByTheVolumesOwnTwoColumns)
	{
		// A volume carries the same eye/camera split as every other scene object, and it maps
		// straight through: its eye cuts what is on screen, its camera cuts what an export contains.
		// So a volume can drop the bulk from a render while leaving it visible to work with.
		RendererWindowState windowState = WindowWithChain();
		SceneHideVolume volume = SphereAtOrigin(2.0f);
		volume.visible = false;   // not cutting the viewport
		volume.renderable = true; // but still cutting the export
		windowState.sceneHideVolumes.push_back(volume);

		ApplyHideVolumeMaskToWindowState(windowState);

		EXPECT_TRUE(windowState.structure.atoms[0].visible);
		EXPECT_FALSE(windowState.structure.atoms[0].renderable);
		EXPECT_FALSE(windowState.structure.bonds[0].renderable);
	}

	TEST(SceneHideVolumeMaskTests, AVolumeWithBothColumnsClearedChangesNothing)
	{
		RendererWindowState windowState = WindowWithChain();
		SceneHideVolume volume = SphereAtOrigin(100.0f); // would cover everything
		volume.visible = false;
		volume.renderable = false;
		windowState.sceneHideVolumes.push_back(volume);

		ApplyHideVolumeMaskToWindowState(windowState);

		for (const RendererAtomData &atom : windowState.structure.atoms)
		{
			EXPECT_TRUE(atom.visible);
			EXPECT_TRUE(atom.renderable);
		}
	}

	TEST(SceneHideVolumeMaskTests, ApplyingTheMaskTwiceIsTheSameAsOnce)
	{
		// The mask is recomputed on every push from the ECS mirror, so it has to be idempotent -
		// and it must not accumulate, or an atom could never come back when a volume is deleted.
		// RendererWindowState is non-copyable, so the pair is built twice rather than copied.
		RendererWindowState once = WindowWithChain();
		once.sceneHideVolumes.push_back(SphereAtOrigin(7.0f)); // atoms 0 and 1
		RendererWindowState twice = WindowWithChain();
		twice.sceneHideVolumes.push_back(SphereAtOrigin(7.0f));

		ApplyHideVolumeMaskToWindowState(once);
		ApplyHideVolumeMaskToWindowState(twice);
		ApplyHideVolumeMaskToWindowState(twice);

		ASSERT_EQ(once.structure.atoms.size(), twice.structure.atoms.size());
		for (std::size_t index = 0; index < once.structure.atoms.size(); ++index)
		{
			EXPECT_EQ(once.structure.atoms[index].visible, twice.structure.atoms[index].visible) << index;
			EXPECT_EQ(once.structure.atoms[index].renderable, twice.structure.atoms[index].renderable) << index;
		}
	}

	TEST(SceneHideVolumeMaskTests, NoVolumesLeaveEverythingAlone)
	{
		RendererWindowState windowState = WindowWithChain();
		windowState.structure.atoms[2].visible = false;

		ApplyHideVolumeMaskToWindowState(windowState);

		EXPECT_TRUE(windowState.structure.atoms[0].visible);
		EXPECT_FALSE(windowState.structure.atoms[2].visible);
		for (const RendererBondData &bond : windowState.structure.bonds)
		{
			EXPECT_TRUE(bond.visible);
		}
	}
} // namespace DefectStudio::Tests
