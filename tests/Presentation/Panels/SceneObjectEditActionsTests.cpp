#include <gtest/gtest.h>

#include <algorithm>

#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Presentation/Panels/SceneDensityEditor.hpp"

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
			GetPersistedSceneFreeLabelClipboard().clear();
			GetScenePinnedMeasurementClipboard().clear();
			GetSceneDensityClipboard().clear();
		}
	} // namespace

	TEST(ScenePlaneClipboardTests, PasteOffsetsAndDetachesAnAnchoredPlane)
	{
		ClearSceneObjectClipboards();
		RendererWindowState window;
		PersistedScenePlane plane;
		plane.center = glm::vec3(1.0f, 2.0f, 3.0f);
		plane.anchorAtoms = {{0, "C", {}}, {1, "C", {}}, {2, "C", {}}};
		GetScenePlaneClipboard() = {SceneClipboardObject{plane}};

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
		PersistedSceneOrbital orbital;
		orbital.centerA = glm::vec3(1.0f, 2.0f, 3.0f);
		orbital.centerB = glm::vec3(4.0f, 5.0f, 6.0f);
		orbital.anchorAtoms = {{0, "C", {}}, {1, "C", {}}};
		GetSceneOrbitalClipboard() = {SceneClipboardObject{orbital}};

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
		PersistedFreeLabel label;
		label.position = glm::vec3(1.0f, 2.0f, 3.0f);
		GetPersistedSceneFreeLabelClipboard() = {SceneClipboardObject{label}};

		PastePersistedSceneFreeLabelsFromClipboard(window);

		ASSERT_EQ(window.freeLabels.size(), 1u);
		EXPECT_EQ(window.freeLabels[0].worldPosition, label.position + kClipboardOffset);
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
		ASSERT_TRUE(std::holds_alternative<PersistedScenePlane>(GetScenePlaneClipboard()[0].persisted));
		EXPECT_EQ(std::get<PersistedScenePlane>(GetScenePlaneClipboard()[0].persisted).center, plane.center);

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
		EXPECT_EQ(window.scenePlanes.back().center, plane.center + kClipboardOffset);
		EXPECT_NE(window.scenePlanes.back().persistKey, window.scenePlanes.front().persistKey);
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
		EXPECT_FALSE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::PinnedMeasurement, SceneObjectEditAction::Paste));
		EXPECT_FALSE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Density, SceneObjectEditAction::Paste));

		GetScenePathClipboard().emplace_back();
		GetScenePlaneClipboard().emplace_back();
		GetSceneOrbitalClipboard().emplace_back();
		GetPersistedSceneFreeLabelClipboard().emplace_back();
		GetScenePinnedMeasurementClipboard().emplace_back();
		GetSceneDensityClipboard().emplace_back();
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Path, SceneObjectEditAction::Paste));
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Plane, SceneObjectEditAction::Paste));
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Orbital, SceneObjectEditAction::Paste));
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::FreeLabel, SceneObjectEditAction::Paste));
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::PinnedMeasurement, SceneObjectEditAction::Paste));
		EXPECT_TRUE(CanExecuteSceneObjectEditAction(
			window, SceneObjectEditKind::Density, SceneObjectEditAction::Paste));
		ClearSceneObjectClipboards();
	}
} // namespace DefectStudio::Tests
