#include <gtest/gtest.h>

#include "IO/SceneObjectsIO.hpp"
#include "Presentation/Panels/SceneHideVolumeEditor.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Scene/SceneObjectPersistence.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Atoms on the x axis every 3 A from the origin, in a 40 A cubic cell, with real ECS entities
		// so PushSelectionAndVisibilityToWindowState rebuilds the flags the way the app does.
		void BuildRow(RendererWindowState &window, const int count = 6)
		{
			window.structure.lattice = glm::mat3(40.0f);
			for (int index = 0; index < count; ++index)
			{
				RendererAtomData atom;
				atom.element = "C";
				atom.cartesianPosition = glm::vec3(3.0f * static_cast<float>(index), 0.0f, 0.0f);
				window.structure.atoms.push_back(atom);
			}
			SceneSystem::SyncSceneWithStructure(window.sceneRegistry, window.structure);
			SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);
		}

		void SelectAtoms(RendererWindowState &window, const std::vector<std::size_t> &atoms)
		{
			SceneSystem::ApplySelectionAndVisibilityToScene(window.sceneRegistry, atoms, {}, {}, {});
			SceneSystem::PushSelectionAndVisibilityToWindowState(window.sceneRegistry, window);
		}

		[[nodiscard]] std::vector<bool> VisibleFlags(const RendererWindowState &window)
		{
			std::vector<bool> flags;
			for (const RendererAtomData &atom : window.structure.atoms)
				flags.push_back(atom.visible);
			return flags;
		}
	} // namespace

	TEST(SceneHideVolumeEditorTests, ANewVolumeAnchorsOnTheSelectionAndCoversIt)
	{
		RendererWindowState window;
		BuildRow(window);
		SelectAtoms(window, {0, 1});

		const SceneHideVolume sphere = MakeSceneHideVolume(window, HideVolumeKind::Sphere);
		EXPECT_EQ(sphere.frame, HideVolumeFrame::Anchored);
		EXPECT_EQ(sphere.anchorAtoms, (std::vector<std::size_t>{0, 1}));
		EXPECT_TRUE(sphere.invert);
		EXPECT_FLOAT_EQ(sphere.halfExtents.x, kDefaultHideVolumeRadius); // 1.5 + 1 < 4
		EXPECT_EQ(sphere.name.rfind("Kula", 0), 0u);

		SelectAtoms(window, {0, 5}); // 15 A apart: 7.5 + 1
		EXPECT_FLOAT_EQ(MakeSceneHideVolume(window, HideVolumeKind::Sphere).halfExtents.x, 8.5f);

		const SceneHideVolume cylinder = MakeSceneHideVolume(window, HideVolumeKind::Cylinder);
		EXPECT_FLOAT_EQ(cylinder.halfExtents.x, 8.5f);
		EXPECT_FLOAT_EQ(cylinder.halfExtents.z, 8.5f);
	}

	TEST(SceneHideVolumeEditorTests, WithNothingSelectedTheVolumeSitsInTheMiddleOfTheCell)
	{
		RendererWindowState window;
		BuildRow(window);
		const SceneHideVolume box = MakeSceneHideVolume(window, HideVolumeKind::Box);
		EXPECT_TRUE(box.anchorAtoms.empty());
		EXPECT_EQ(box.center, glm::vec3(20.0f));
		EXPECT_EQ(box.halfExtents, glm::vec3(kDefaultHideVolumeRadius));
	}

	TEST(SceneHideVolumeEditorTests, AddingHidesTheBulkAtOnceAndUndoBringsItBack)
	{
		RendererWindowState window;
		BuildRow(window);
		SelectAtoms(window, {0});
		const SceneObjectsSnapshot before = CaptureSceneObjectsSnapshot(window);

		AddSceneHideVolume(window, HideVolumeKind::Sphere); // inverted, radius 4 around atom 0
		ASSERT_EQ(window.sceneHideVolumes.size(), 1u);
		EXPECT_EQ(window.selectedSceneHideVolumes, std::vector<SceneObjectId>{window.sceneHideVolumes.front().id});
		EXPECT_TRUE(window.sceneHideVolumes.front().id.IsValid());
		EXPECT_EQ(VisibleFlags(window), (std::vector<bool>{true, true, false, false, false, false}));

		// The undo path: restoring a snapshot must re-apply the mask, not leave the atoms dark.
		RestoreSceneObjectsSnapshot(window, before);
		EXPECT_TRUE(window.sceneHideVolumes.empty());
		EXPECT_TRUE(window.selectedSceneHideVolumes.empty());
		EXPECT_EQ(VisibleFlags(window), std::vector<bool>(6, true));
	}

	TEST(SceneHideVolumeEditorTests, EraseAndDuplicateRefreshTheMask)
	{
		RendererWindowState window;
		BuildRow(window);
		SelectAtoms(window, {5});
		AddSceneHideVolume(window, HideVolumeKind::Sphere);
		const SceneObjectId original = window.sceneHideVolumes.front().id;
		window.sceneHideVolumes.front().invert = false; // hide inside: atoms 4 and 5

		DuplicateSelectedSceneHideVolumes(window);
		ASSERT_EQ(window.sceneHideVolumes.size(), 2u);
		EXPECT_NE(window.sceneHideVolumes.back().id, original);
		EXPECT_TRUE(window.sceneHideVolumes.back().persistKey.empty());
		EXPECT_EQ(window.selectedSceneHideVolumes, std::vector<SceneObjectId>{window.sceneHideVolumes.back().id});
		EXPECT_EQ(VisibleFlags(window), (std::vector<bool>{true, true, true, true, false, false}));

		EraseSceneHideVolumes(window, {original, window.sceneHideVolumes.back().id});
		EXPECT_TRUE(window.sceneHideVolumes.empty());
		EXPECT_EQ(VisibleFlags(window), std::vector<bool>(6, true));
	}

	TEST(SceneHideVolumeEditorTests, PasteIntoAnotherStructureReanchorsOnItsSelection)
	{
		RendererWindowState source;
		BuildRow(source);
		SelectAtoms(source, {1});
		AddSceneHideVolume(source, HideVolumeKind::Sphere);
		CopySceneHideVolumesToClipboard(source);
		ASSERT_EQ(GetSceneHideVolumeClipboard().size(), 1u);
		EXPECT_EQ(GetSceneHideVolumeClipboard().front().center, glm::vec3(3.0f, 0.0f, 0.0f)); // resolved

		RendererWindowState target;
		BuildRow(target);
		SelectAtoms(target, {4});
		PasteSceneHideVolumesFromClipboard(target);
		ASSERT_EQ(target.sceneHideVolumes.size(), 1u);
		EXPECT_EQ(target.sceneHideVolumes.front().anchorAtoms, std::vector<std::size_t>{4});
		EXPECT_FLOAT_EQ(target.sceneHideVolumes.front().halfExtents.x, kDefaultHideVolumeRadius);
		EXPECT_EQ(VisibleFlags(target), (std::vector<bool>{false, false, false, true, true, true}));
	}

	TEST(SceneHideVolumeEditorTests, PasteWithoutASelectionKeepsTheAnchorsThatStillResolve)
	{
		RendererWindowState source;
		BuildRow(source);
		SelectAtoms(source, {1, 5});
		AddSceneHideVolume(source, HideVolumeKind::Sphere);
		CopySceneHideVolumesToClipboard(source);

		RendererWindowState smaller; // atoms 0..3 only: index 5 does not exist here
		BuildRow(smaller, 4);
		PasteSceneHideVolumesFromClipboard(smaller);
		ASSERT_EQ(smaller.sceneHideVolumes.size(), 1u);
		EXPECT_EQ(smaller.sceneHideVolumes.front().anchorAtoms, std::vector<std::size_t>{1});
		EXPECT_NE(smaller.sceneHideVolumes.front().id, source.sceneHideVolumes.front().id);
	}

	TEST(SceneHideVolumeEditorTests, ReanchoringFollowsTheNewSelection)
	{
		RendererWindowState window;
		BuildRow(window);
		SelectAtoms(window, {0});
		AddSceneHideVolume(window, HideVolumeKind::Sphere);
		SelectAtoms(window, {5});
		AnchorSelectedSceneHideVolumesToSelectedAtoms(window);
		EXPECT_EQ(window.sceneHideVolumes.front().anchorAtoms, std::vector<std::size_t>{5});
		EXPECT_EQ(VisibleFlags(window), (std::vector<bool>{false, false, false, false, true, true}));
		EXPECT_EQ(CountAtomsHiddenBySceneHideVolume(window, window.sceneHideVolumes.front()), 4u);
	}

	TEST(SceneHideVolumeEditorTests, LoadingAProjectAppliesTheMask)
	{
		RendererWindowState saved;
		BuildRow(saved);
		SelectAtoms(saved, {0});
		AddSceneHideVolume(saved, HideVolumeKind::Sphere);

		std::vector<StructuredError> warnings;
		RendererWindowState reopened;
		BuildRow(reopened);
		ApplyPersistedSceneObjects(reopened, ExtractPersistedSceneObjects(saved), warnings);
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(reopened.sceneHideVolumes.size(), 1u);
		EXPECT_EQ(VisibleFlags(reopened), (std::vector<bool>{true, true, false, false, false, false}));
	}
} // namespace DefectStudio::Tests
