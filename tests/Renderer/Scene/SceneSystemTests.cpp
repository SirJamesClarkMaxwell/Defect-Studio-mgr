#include <gtest/gtest.h>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] RendererStructureData BuildThreeAtomOneBondStructure()
		{
			RendererStructureData structure;
			structure.atoms = {
				RendererAtomData{"C", glm::vec3(0.0f), glm::vec3(1.0f), 0.4f, true},
				RendererAtomData{"N", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), 0.4f, true},
				RendererAtomData{"O", glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f), 0.4f, false}};
			RendererBondData bond;
			bond.firstAtomIndex = 0;
			bond.secondAtomIndex = 1;
			structure.bonds = {bond};
			return structure;
		}
	} // namespace

	TEST(SceneSystemTests, SyncBuildsOneEntityPerAtomAndBond)
	{
		SceneRegistry scene;
		const RendererStructureData structure = BuildThreeAtomOneBondStructure();
		SceneSystem::SyncSceneWithStructure(scene, structure);

		ASSERT_EQ(scene.AtomEntities().size(), 3u);
		ASSERT_EQ(scene.BondEntities().size(), 1u);

		Entity secondAtom = scene.AtomEntityAt(1);
		ASSERT_TRUE(secondAtom);
		EXPECT_EQ(secondAtom.GetComponent<AtomComponent>().element, "N");
		EXPECT_TRUE(secondAtom.HasComponent<SelectionComponent>());
		EXPECT_FALSE(secondAtom.GetComponent<SelectionComponent>().selected);

		Entity thirdAtom = scene.AtomEntityAt(2);
		EXPECT_FALSE(thirdAtom.GetComponent<VisibilityComponent>().visible);

		Entity bondEntity = scene.BondEntityAt(0);
		ASSERT_TRUE(bondEntity);
		EXPECT_EQ(bondEntity.GetComponent<BondComponent>().firstAtomEntity, static_cast<entt::entity>(scene.AtomEntityAt(0)));
		EXPECT_EQ(bondEntity.GetComponent<BondComponent>().secondAtomEntity, static_cast<entt::entity>(secondAtom));
	}

	TEST(SceneSystemTests, PushSelectionAndVisibilityWritesBackToWindowState)
	{
		SceneRegistry scene;
		RendererWindowState windowState;
		windowState.structure = BuildThreeAtomOneBondStructure();
		SceneSystem::SyncSceneWithStructure(scene, windowState.structure);

		scene.AtomEntityAt(1).GetComponent<SelectionComponent>().selected = true;
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, windowState);

		ASSERT_EQ(windowState.selectedAtomIndices.size(), 1u);
		EXPECT_EQ(windowState.selectedAtomIndices.front(), 1u);
		EXPECT_TRUE(windowState.structure.atoms[0].visible);
		EXPECT_FALSE(windowState.structure.atoms[2].visible);
	}

	TEST(SceneSystemTests, PushSelectionAndVisibilityWritesBackBondSelectionAndVisibility)
	{
		SceneRegistry scene;
		RendererWindowState windowState;
		windowState.structure = BuildThreeAtomOneBondStructure();
		SceneSystem::SyncSceneWithStructure(scene, windowState.structure);

		Entity bondEntity = scene.BondEntityAt(0);
		bondEntity.GetComponent<SelectionComponent>().selected = true;
		bondEntity.GetComponent<VisibilityComponent>().visible = false;
		SceneSystem::PushSelectionAndVisibilityToWindowState(scene, windowState);

		ASSERT_EQ(windowState.selectedBondIndices.size(), 1u);
		EXPECT_EQ(windowState.selectedBondIndices.front(), 0u);
		EXPECT_FALSE(windowState.structure.bonds[0].visible);
	}

	TEST(SceneSystemTests, SyncDestroysPreviousEntitiesOnReload)
	{
		SceneRegistry scene;
		const RendererStructureData first = BuildThreeAtomOneBondStructure();
		SceneSystem::SyncSceneWithStructure(scene, first);
		const entt::entity staleEntity = static_cast<entt::entity>(scene.AtomEntityAt(0));

		RendererStructureData second;
		second.atoms = {RendererAtomData{"H", glm::vec3(0.0f), glm::vec3(1.0f), 0.2f, true}};
		SceneSystem::SyncSceneWithStructure(scene, second);

		EXPECT_FALSE(scene.Registry().valid(staleEntity));
		ASSERT_EQ(scene.AtomEntities().size(), 1u);
		EXPECT_EQ(scene.AtomEntityAt(0).GetComponent<AtomComponent>().element, "H");
	}

	TEST(SceneSystemTests, ResolveAtomIndicesByPositionMatchesNearestWithinTolerance)
	{
		const RendererStructureData structure = BuildThreeAtomOneBondStructure();

		const std::vector<std::size_t> resolved = SceneSystem::ResolveAtomIndicesByPosition(
			structure, {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.05f, 0.0f, 0.0f)});

		ASSERT_EQ(resolved.size(), 2u);
		EXPECT_EQ(resolved[0], 1u);
		EXPECT_EQ(resolved[1], 0u);
	}

	TEST(SceneSystemTests, ResolveAtomIndicesByPositionDropsUnmatchedPositions)
	{
		const RendererStructureData structure = BuildThreeAtomOneBondStructure();

		const std::vector<std::size_t> resolved = SceneSystem::ResolveAtomIndicesByPosition(
			structure, {glm::vec3(50.0f, 50.0f, 50.0f)});

		EXPECT_TRUE(resolved.empty());
	}

	TEST(SceneSystemTests, AnchoredArrowFollowsMovedAtomAndKeepsItsBufferGap)
	{
		RendererWindowState windowState;
		windowState.structure.atoms = {
			RendererAtomData{"C", glm::vec3(0.0f), glm::vec3(1.0f), 0.5f},
			RendererAtomData{"O", glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(1.0f), 0.25f}};
		RendererWindowState::SceneArrow arrow;
		arrow.points = {glm::vec3(0.0f), glm::vec3(5.0f, 2.0f, 0.0f), glm::vec3(10.0f, 0.0f, 0.0f)};
		arrow.startAnchorAtom = 0;
		arrow.endAnchorAtom = 1;
		arrow.atomBuffer = 1.0f;
		windowState.sceneArrows.push_back(arrow);

		SceneSystem::RefreshAnchoredSceneArrows(windowState);
		EXPECT_NEAR(windowState.sceneArrows[0].start().x, 0.5f, 1e-5f);
		EXPECT_NEAR(windowState.sceneArrows[0].end().x, 9.75f, 1e-5f);
		ASSERT_EQ(windowState.sceneArrows[0].points.size(), 3u);
		EXPECT_EQ(windowState.sceneArrows[0].points[1], glm::vec3(5.0f, 2.0f, 0.0f));

		windowState.structure.atoms[1].cartesianPosition.x = 20.0f;
		SceneSystem::RefreshAnchoredSceneArrows(windowState);
		EXPECT_NEAR(windowState.sceneArrows[0].end().x, 19.75f, 1e-5f);
		EXPECT_NEAR(
			glm::distance(windowState.sceneArrows[0].end(), windowState.structure.atoms[1].cartesianPosition),
			0.25f, 1e-5f);
	}

	TEST(SceneSystemTests, AnchoredArrowUsesItsLivePerArrowBuffer)
	{
		RendererWindowState windowState;
		windowState.structure.atoms = {
			RendererAtomData{"C", glm::vec3(0.0f), glm::vec3(1.0f), 0.5f},
			RendererAtomData{"O", glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(1.0f), 0.25f}};
		RendererWindowState::SceneArrow arrow;
		arrow.startAnchorAtom = 0;
		arrow.endAnchorAtom = 1;
		arrow.atomBuffer = 1.0f;
		windowState.sceneArrows.push_back(arrow);

		SceneSystem::RefreshAnchoredSceneArrows(windowState);
		const glm::vec3 firstStart = windowState.sceneArrows[0].start();
		const glm::vec3 firstEnd = windowState.sceneArrows[0].end();
		windowState.sceneArrows[0].atomBuffer = 2.0f;
		SceneSystem::RefreshAnchoredSceneArrows(windowState);
		EXPECT_NE(windowState.sceneArrows[0].start(), firstStart);
		EXPECT_NE(windowState.sceneArrows[0].end(), firstEnd);

		windowState.sceneArrows[0].atomBuffer = 0.0f;
		SceneSystem::RefreshAnchoredSceneArrows(windowState);
		EXPECT_EQ(windowState.sceneArrows[0].start(), windowState.structure.atoms[0].cartesianPosition);
		EXPECT_EQ(windowState.sceneArrows[0].end(), windowState.structure.atoms[1].cartesianPosition);
	}

	TEST(SceneSystemTests, FreeArrowStartStaysPlacedWhileAnchoredEndTracksItsAtom)
	{
		RendererWindowState windowState;
		windowState.structure.atoms = {
			RendererAtomData{"C", glm::vec3(0.0f), glm::vec3(1.0f), 0.5f},
			RendererAtomData{"O", glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(1.0f), 0.25f}};
		RendererWindowState::SceneArrow arrow;
		arrow.start() = glm::vec3(-2.0f, 1.0f, 0.0f);
		arrow.startAnchorAtom.reset();
		arrow.endAnchorAtom = 1;
		arrow.atomBuffer = 1.0f;
		windowState.sceneArrows.push_back(arrow);

		SceneSystem::RefreshAnchoredSceneArrows(windowState);
		EXPECT_EQ(windowState.sceneArrows[0].start(), glm::vec3(-2.0f, 1.0f, 0.0f));
		const glm::vec3 firstEnd = windowState.sceneArrows[0].end();
		windowState.structure.atoms[1].cartesianPosition = glm::vec3(12.0f, 3.0f, 0.0f);
		SceneSystem::RefreshAnchoredSceneArrows(windowState);
		EXPECT_EQ(windowState.sceneArrows[0].start(), glm::vec3(-2.0f, 1.0f, 0.0f));
		EXPECT_NE(windowState.sceneArrows[0].end(), firstEnd);
	}

	TEST(SceneSystemTests, ArrowBufferClampNeverInvertsCloseAnchors)
	{
		RendererWindowState windowState;
		windowState.structure.atoms = {
			RendererAtomData{"C", glm::vec3(0.0f), glm::vec3(1.0f), 2.0f},
			RendererAtomData{"C", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), 2.0f}};
		RendererWindowState::SceneArrow arrow;
		arrow.startAnchorAtom = 0;
		arrow.endAnchorAtom = 1;
		arrow.atomBuffer = 1.0f;
		windowState.sceneArrows.push_back(arrow);

		SceneSystem::RefreshAnchoredSceneArrows(windowState);

		EXPECT_LT(windowState.sceneArrows[0].start().x, windowState.sceneArrows[0].end().x);
		EXPECT_NEAR(windowState.sceneArrows[0].end().x - windowState.sceneArrows[0].start().x, 0.1f, 1e-5f);
	}
} // namespace DefectStudio::Tests
