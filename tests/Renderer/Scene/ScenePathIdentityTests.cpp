#include <gtest/gtest.h>

#include <vector>

#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	TEST(ScenePathIdentityTests, SelectionSurvivesDeletingADifferentPath)
	{
		RendererWindowState window;
		const auto first = SceneSystem::AppendScenePath(window, ScenePath{});
		const auto second = SceneSystem::AppendScenePath(window, ScenePath{});
		window.selectedScenePaths = {second};
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		ASSERT_TRUE(window.paths->ErasePath(first));
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);

		EXPECT_EQ(window.selectedScenePaths, (std::vector<SceneObjectId>{second}));
		EXPECT_EQ(SceneSystem::ResolveSourceIndices(window.sceneRegistry, window.selectedScenePaths),
			(std::vector<std::size_t>{0}));
		EXPECT_FALSE(window.sceneRegistry.FindObject(first));
		EXPECT_EQ(window.sceneRegistry.PathEntityAt(0).GetComponent<SceneObjectComponent>().id, second);
	}

	TEST(ScenePathIdentityTests, DestroyedIdsStayDeadAndSourceIndicesDropThem)
	{
		RendererWindowState window;
		const auto live = SceneSystem::AppendScenePath(window, ScenePath{});
		const auto dead = SceneSystem::AppendScenePath(window, ScenePath{});
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		ASSERT_TRUE(window.paths->ErasePath(dead));
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		const auto replacement = SceneSystem::AppendScenePath(window, ScenePath{});
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		EXPECT_NE(replacement, dead);
		EXPECT_FALSE(window.sceneRegistry.IsAlive(dead));
		EXPECT_EQ(SceneSystem::ResolveSourceIndices(window.sceneRegistry, {live, dead, replacement}),
			(std::vector<std::size_t>{0, 1}));
	}

	TEST(ScenePathIdentityTests, SnapshotRestorePreservesIdsAndRegistryLookups)
	{
		RendererWindowState window;
		const auto id = SceneSystem::AppendScenePath(window, ScenePath{});
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		const auto snapshot = CaptureSceneObjectsSnapshot(window);
		ASSERT_TRUE(window.paths->ErasePath(id));
		SceneSystem::SyncLabelEntities(window.sceneRegistry, window);
		EXPECT_FALSE(window.sceneRegistry.FindObject(id));

		RestoreSceneObjectsSnapshot(window, snapshot);
		EXPECT_TRUE(window.paths->Store().Contains(id));
		ASSERT_TRUE(window.sceneRegistry.FindObject(id));
		EXPECT_EQ(window.sceneRegistry.PathEntityAt(0).GetComponent<SceneObjectComponent>().id, id);
		EXPECT_GT(window.sceneRegistry.AllocateObjectId().value, id.value);
	}
}
