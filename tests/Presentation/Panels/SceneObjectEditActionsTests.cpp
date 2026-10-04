#include <gtest/gtest.h>

#include <algorithm>

#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr glm::vec3 kClipboardOffset(0.5f, 0.0f, 0.0f);

		void ClearSceneObjectClipboards()
		{
			GetScenePathClipboard().clear();
			GetScenePlaneClipboard().clear();
			GetSceneOrbitalClipboard().clear();
			GetSceneFreeLabelClipboard().clear();
		}
	} // namespace

	TEST(ScenePlaneClipboardTests, PasteOffsetsAndDetachesAnAnchoredPlane)
	{
		ClearSceneObjectClipboards();
		RendererWindowState window;
		RendererWindowState::ScenePlane plane;
		plane.center = glm::vec3(1.0f, 2.0f, 3.0f);
		plane.anchorAtoms = {0, 1, 2};
		GetScenePlaneClipboard() = {plane};

		PasteScenePlanesFromClipboard(window);

		ASSERT_EQ(window.scenePlanes.size(), 1u);
		EXPECT_EQ(window.scenePlanes[0].center, plane.center + kClipboardOffset);
		EXPECT_TRUE(window.scenePlanes[0].anchorAtoms.empty());
		ClearSceneObjectClipboards();
	}

	TEST(SceneOrbitalClipboardTests, PasteOffsetsAndDetachesAnAnchoredOrbital)
	{
		ClearSceneObjectClipboards();
		RendererWindowState window;
		RendererWindowState::SceneOrbital orbital;
		orbital.centerA = glm::vec3(1.0f, 2.0f, 3.0f);
		orbital.centerB = glm::vec3(4.0f, 5.0f, 6.0f);
		orbital.anchorAtoms = {0, 1};
		GetSceneOrbitalClipboard() = {orbital};

		PasteSceneOrbitalsFromClipboard(window);

		ASSERT_EQ(window.sceneOrbitals.size(), 1u);
		EXPECT_EQ(window.sceneOrbitals[0].centerA, orbital.centerA + kClipboardOffset);
		EXPECT_EQ(window.sceneOrbitals[0].centerB, orbital.centerB + kClipboardOffset);
		EXPECT_TRUE(window.sceneOrbitals[0].anchorAtoms.empty());
		ClearSceneObjectClipboards();
	}

	TEST(SceneFreeLabelClipboardTests, PasteOffsetsAFreeLabel)
	{
		ClearSceneObjectClipboards();
		RendererWindowState window;
		RendererWindowState::FreeLabel label;
		label.worldPosition = glm::vec3(1.0f, 2.0f, 3.0f);
		GetSceneFreeLabelClipboard() = {label};

		PasteSceneFreeLabelsFromClipboard(window);

		ASSERT_EQ(window.freeLabels.size(), 1u);
		EXPECT_EQ(window.freeLabels[0].worldPosition, label.worldPosition + kClipboardOffset);
		ClearSceneObjectClipboards();
	}

	TEST(SceneObjectEditActionsTests, OutlinerAndKeyboardShareCopyDuplicatePasteAndDeleteActions)
	{
		ClearSceneObjectClipboards();
		RendererWindowState window;
		RendererWindowState::ScenePlane plane;
		plane.id = window.sceneRegistry.AllocateObjectId();
		plane.center = glm::vec3(2.0f, 3.0f, 4.0f);
		window.scenePlanes.push_back(plane);
		window.selectedScenePlanes = {plane.id};

		EXPECT_TRUE(ExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Plane, SceneObjectEditAction::Copy));
		ASSERT_EQ(GetScenePlaneClipboard().size(), 1u);
		EXPECT_EQ(GetScenePlaneClipboard()[0].id, plane.id);

		EXPECT_TRUE(ExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Plane, SceneObjectEditAction::Duplicate));
		ASSERT_EQ(window.scenePlanes.size(), 2u);
		const SceneObjectId duplicateId = window.scenePlanes.back().id;
		EXPECT_NE(duplicateId, plane.id);
		EXPECT_EQ(window.scenePlanes.back().center, plane.center + kClipboardOffset);

		EXPECT_TRUE(ExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Plane, SceneObjectEditAction::Paste));
		ASSERT_EQ(window.scenePlanes.size(), 3u);
		const SceneObjectId pastedId = window.scenePlanes.back().id;
		EXPECT_NE(pastedId, plane.id);
		EXPECT_NE(pastedId, duplicateId);
		EXPECT_EQ(window.selectedScenePlanes, std::vector<SceneObjectId>{pastedId});

		EXPECT_TRUE(ExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Plane, SceneObjectEditAction::Delete));
		ASSERT_EQ(window.scenePlanes.size(), 2u);
		EXPECT_TRUE(window.selectedScenePlanes.empty());
		EXPECT_TRUE(std::none_of(
			window.scenePlanes.begin(), window.scenePlanes.end(),
			[pastedId](const RendererWindowState::ScenePlane &candidate) { return candidate.id == pastedId; }));
		ClearSceneObjectClipboards();
	}

	TEST(SceneObjectEditActionsTests, PasteAvailabilityTracksEachKindsClipboard)
	{
		ClearSceneObjectClipboards();
		RendererWindowState window;
		EXPECT_FALSE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Path, SceneObjectEditAction::Paste));
		EXPECT_FALSE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Plane, SceneObjectEditAction::Paste));
		EXPECT_FALSE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Orbital, SceneObjectEditAction::Paste));
		EXPECT_FALSE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::FreeLabel, SceneObjectEditAction::Paste));

		GetScenePathClipboard().emplace_back();
		GetScenePlaneClipboard().emplace_back();
		GetSceneOrbitalClipboard().emplace_back();
		GetSceneFreeLabelClipboard().emplace_back();
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Path, SceneObjectEditAction::Paste));
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Plane, SceneObjectEditAction::Paste));
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Orbital, SceneObjectEditAction::Paste));
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::FreeLabel, SceneObjectEditAction::Paste));
		ClearSceneObjectClipboards();
	}
} // namespace DefectStudio::Tests
