#include <gtest/gtest.h>

#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneObjectEditingTests, EraseSceneOrbitalsRemovesIdsAndClearsSelection)
	{
		RendererWindowState window;
		RendererWindowState::SceneOrbital first;
		first.id = SceneObjectId{1};
		RendererWindowState::SceneOrbital second;
		second.id = SceneObjectId{2};
		window.sceneOrbitals = {first, second};
		window.selectedSceneOrbitals = {first.id};

		EraseSceneOrbitals(window, {first.id, SceneObjectId{99}});

		ASSERT_EQ(window.sceneOrbitals.size(), 1u);
		EXPECT_EQ(window.sceneOrbitals.front().id, second.id);
		EXPECT_TRUE(window.selectedSceneOrbitals.empty());
	}

	TEST(SceneObjectEditingTests, EraseScenePlanesRemovesIdsAndClearsSelection)
	{
		RendererWindowState window;
		RendererWindowState::ScenePlane first;
		first.id = SceneObjectId{3};
		RendererWindowState::ScenePlane second;
		second.id = SceneObjectId{4};
		window.scenePlanes = {first, second};
		window.selectedScenePlanes = {second.id};

		EraseScenePlanes(window, {second.id, second.id});

		ASSERT_EQ(window.scenePlanes.size(), 1u);
		EXPECT_EQ(window.scenePlanes.front().id, first.id);
		EXPECT_TRUE(window.selectedScenePlanes.empty());
	}
} // namespace DefectStudio::Tests
