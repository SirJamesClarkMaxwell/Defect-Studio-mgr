#include <gtest/gtest.h>

#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Presentation/Panels/SceneOutlinerPanel.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneObjectEditingTests, EraseSceneArrowsResyncsRegistryAndOutlinerRows)
	{
		RendererWindowState window;
		for (int index = 0; index < 3; ++index)
		{
			RendererWindowState::SceneArrow arrow;
			arrow.id = window.sceneRegistry.AllocateObjectId();
			window.sceneArrows.push_back(arrow);
		}
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		const SceneObjectId deletedId = window.sceneArrows[1].id;
		const std::vector<SceneObjectId> expectedRowIds{window.sceneArrows[0].id, window.sceneArrows[2].id};
		window.selectedSceneArrows = {deletedId};

		EraseSceneArrows(window, {deletedId});

		ASSERT_EQ(window.sceneRegistry.ArrowEntities().size(), 2u);
		EXPECT_EQ(window.sceneRegistry.ArrowEntityAt(0).GetComponent<SceneObjectComponent>().sourceIndex, 0u);
		EXPECT_EQ(window.sceneRegistry.ArrowEntityAt(1).GetComponent<SceneObjectComponent>().sourceIndex, 1u);
		EXPECT_FALSE(window.sceneRegistry.FindObject(deletedId));
		EXPECT_TRUE(window.selectedSceneArrows.empty());

		const std::vector<std::size_t> rowIndices =
			CollectSceneOutlinerSourceIndices(window.sceneRegistry, SceneObjectKind::SceneArrow);
		ASSERT_EQ(rowIndices.size(), 2u);
		std::vector<SceneObjectId> rowIds;
		for (const std::size_t index : rowIndices)
			rowIds.push_back(window.sceneArrows[index].id);
		EXPECT_EQ(rowIds, expectedRowIds);
	}

	TEST(SceneObjectEditingTests, EraseSceneOrbitalsResyncsRegistryAndOutlinerRows)
	{
		RendererWindowState window;
		for (int index = 0; index < 3; ++index)
		{
			RendererWindowState::SceneOrbital orbital;
			orbital.id = window.sceneRegistry.AllocateObjectId();
			window.sceneOrbitals.push_back(orbital);
		}
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		const SceneObjectId deletedId = window.sceneOrbitals[1].id;
		const std::vector<SceneObjectId> expectedRowIds{window.sceneOrbitals[0].id, window.sceneOrbitals[2].id};
		window.selectedSceneOrbitals = {deletedId};

		EraseSceneOrbitals(window, {deletedId});

		ASSERT_EQ(window.sceneRegistry.OrbitalEntities().size(), 2u);
		EXPECT_EQ(window.sceneRegistry.OrbitalEntityAt(0).GetComponent<SceneObjectComponent>().sourceIndex, 0u);
		EXPECT_EQ(window.sceneRegistry.OrbitalEntityAt(1).GetComponent<SceneObjectComponent>().sourceIndex, 1u);
		EXPECT_FALSE(window.sceneRegistry.FindObject(deletedId));
		EXPECT_TRUE(window.selectedSceneOrbitals.empty());

		const std::vector<std::size_t> rowIndices =
			CollectSceneOutlinerSourceIndices(window.sceneRegistry, SceneObjectKind::SceneOrbital);
		ASSERT_EQ(rowIndices.size(), 2u);
		std::vector<SceneObjectId> rowIds;
		for (const std::size_t index : rowIndices)
			rowIds.push_back(window.sceneOrbitals[index].id);
		EXPECT_EQ(rowIds, expectedRowIds);
	}

	TEST(SceneObjectEditingTests, AlignOrbitalOrientationsSkipsTwoCentrePresets)
	{
		std::vector<RendererWindowState::SceneOrbital> orbitals(4);
		orbitals[0].id = SceneObjectId{10};
		orbitals[0].preset = OrbitalPreset::Sigma;
		orbitals[0].rotationEuler = glm::vec3(90.0f, 80.0f, 70.0f);
		orbitals[1].id = SceneObjectId{20};
		orbitals[1].preset = OrbitalPreset::P;
		orbitals[1].rotationEuler = glm::vec3(10.0f, 20.0f, 30.0f);
		orbitals[2].id = SceneObjectId{30};
		orbitals[2].preset = OrbitalPreset::Sp3;
		orbitals[2].rotationEuler = glm::vec3(-1.0f, -2.0f, -3.0f);
		orbitals[3].id = SceneObjectId{40};
		orbitals[3].preset = OrbitalPreset::D;
		orbitals[3].rotationEuler = glm::vec3(4.0f, 5.0f, 6.0f);

		AlignSelectedSceneOrbitalOrientations(
			orbitals, {SceneObjectId{10}, SceneObjectId{20}, SceneObjectId{30}});

		EXPECT_EQ(orbitals[0].rotationEuler, glm::vec3(90.0f, 80.0f, 70.0f));
		EXPECT_EQ(orbitals[1].rotationEuler, glm::vec3(10.0f, 20.0f, 30.0f));
		EXPECT_EQ(orbitals[2].rotationEuler, glm::vec3(10.0f, 20.0f, 30.0f));
		EXPECT_EQ(orbitals[3].rotationEuler, glm::vec3(4.0f, 5.0f, 6.0f));
	}

	TEST(SceneObjectEditingTests, EraseScenePlanesUpdatesTheVectorBackedOutlinerRows)
	{
		RendererWindowState window;
		for (int index = 0; index < 3; ++index)
		{
			RendererWindowState::ScenePlane plane;
			plane.id = window.sceneRegistry.AllocateObjectId();
			window.scenePlanes.push_back(plane);
		}
		const SceneObjectId firstId = window.scenePlanes[0].id;
		const SceneObjectId deletedId = window.scenePlanes[1].id;
		const SceneObjectId lastId = window.scenePlanes[2].id;
		window.selectedScenePlanes = {deletedId};

		EraseScenePlanes(window, {deletedId});

		ASSERT_EQ(window.scenePlanes.size(), 2u);
		EXPECT_EQ(window.scenePlanes[0].id, firstId);
		EXPECT_EQ(window.scenePlanes[1].id, lastId);
		EXPECT_TRUE(window.selectedScenePlanes.empty());
		EXPECT_TRUE(window.sceneRegistry.Registry().view<const SceneObjectComponent>().empty());
	}
} // namespace DefectStudio::Tests
