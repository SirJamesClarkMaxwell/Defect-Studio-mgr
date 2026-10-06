#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneObject.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Three atoms, one bond, one pinned bond-length measurement, one free label, two paths.
		[[nodiscard]] RendererWindowState BuildAnnotatedWindow()
		{
			RendererWindowState windowState;
			windowState.structure.atoms = {
				RendererAtomData{"C", glm::vec3(0.0f), glm::vec3(1.0f), 0.4f, true},
				RendererAtomData{"N", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f), 0.4f, true},
				RendererAtomData{"O", glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f), 0.4f, true}};
			RendererBondData bond;
			bond.firstAtomIndex = 0;
			bond.secondAtomIndex = 1;
			windowState.structure.bonds = {bond};

			RendererWindowState::PinnedMeasurement pin;
			pin.id = windowState.sceneRegistry.AllocateObjectId();
			pin.atomIndices = {0, 1};
			windowState.pinnedMeasurements.push_back(pin);

			RendererWindowState::FreeLabel label;
			label.id = windowState.sceneRegistry.AllocateObjectId();
			label.text = "note";
			label.worldPosition = glm::vec3(2.0f, 0.0f, 0.0f);
			windowState.freeLabels.push_back(label);

			for (int index = 0; index < 2; ++index)
			{
				ScenePath path;
				path.nodes = {{PathElementId{1}, glm::vec3(static_cast<float>(index), 0.0f, 0.0f), {}},
					{PathElementId{2}, glm::vec3(static_cast<float>(index), 1.0f, 0.0f), {}}};
				(void)SceneSystem::AppendScenePath(windowState, path);
			}

			SceneSystem::SyncSceneWithStructure(windowState.sceneRegistry, windowState.structure);
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
			return windowState;
		}

		[[nodiscard]] std::vector<SceneObjectId> CollectAtomIds(SceneRegistry &scene)
		{
			std::vector<SceneObjectId> ids;
			for (std::size_t index = 0; index < scene.AtomEntities().size(); ++index)
				ids.push_back(scene.AtomEntityAt(index).GetComponent<SceneObjectComponent>().id);
			return ids;
		}
	} // namespace

	// Criterion 1 - the property the whole task exists for.
	TEST(SceneObjectModelTests, IdsSurviveAStructureResync)
	{
		RendererWindowState windowState = BuildAnnotatedWindow();
		const std::vector<SceneObjectId> atomIdsBefore = CollectAtomIds(windowState.sceneRegistry);
		const SceneObjectId pathId = windowState.paths->Store().At(0)->id;
		const SceneObjectId labelId = windowState.freeLabels.front().id;

		SceneSystem::SyncSceneWithStructure(windowState.sceneRegistry, windowState.structure);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);

		EXPECT_EQ(CollectAtomIds(windowState.sceneRegistry), atomIdsBefore);
		EXPECT_EQ(windowState.paths->Store().At(0)->id, pathId);
		EXPECT_EQ(windowState.freeLabels.front().id, labelId);

		Entity resolvedPath = windowState.sceneRegistry.FindObject(pathId);
		ASSERT_TRUE(resolvedPath);
		const SceneObjectComponent &component = resolvedPath.GetComponent<SceneObjectComponent>();
		EXPECT_EQ(component.kind, SceneObjectKind::ScenePath);
		EXPECT_EQ(component.sourceIndex, 0u);
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(labelId));
	}

	// Criterion 2 - one enumeration point for every kind.
	TEST(SceneObjectModelTests, RegistryEnumeratesEveryObjectExactlyOnce)
	{
		RendererWindowState windowState = BuildAnnotatedWindow();

		std::unordered_set<SceneObjectId> seen;
		std::vector<std::size_t> countsByKind(static_cast<std::size_t>(SceneObjectKind::ScenePath) + 1, 0);
		const auto view = windowState.sceneRegistry.Registry().view<const SceneObjectComponent>();
		for (const entt::entity entity : view)
		{
			const SceneObjectComponent &component = view.get<const SceneObjectComponent>(entity);
			EXPECT_TRUE(component.id.IsValid());
			EXPECT_TRUE(seen.insert(component.id).second);
			countsByKind[static_cast<std::size_t>(component.kind)] += 1;
		}

		EXPECT_EQ(countsByKind[static_cast<std::size_t>(SceneObjectKind::Atom)], 3u);
		EXPECT_EQ(countsByKind[static_cast<std::size_t>(SceneObjectKind::Bond)], 1u);
		EXPECT_EQ(countsByKind[static_cast<std::size_t>(SceneObjectKind::PinnedMeasurement)], 1u);
		EXPECT_EQ(countsByKind[static_cast<std::size_t>(SceneObjectKind::FreeLabel)], 1u);
		EXPECT_EQ(countsByKind[static_cast<std::size_t>(SceneObjectKind::ScenePath)], 2u);
		EXPECT_EQ(seen.size(), 8u);
	}

	// Criterion 5 - the shared scene-object undo stack snapshots whole vectors, so a restore must not
	// renumber anything.
	TEST(SceneObjectModelTests, IdsSurviveAnUndoRedoRoundTrip)
	{
		RendererWindowState windowState = BuildAnnotatedWindow();
		const SceneObjectsSnapshot snapshot = CaptureSceneObjectsSnapshot(windowState);
		const SceneObjectId pathId = windowState.paths->Store().At(0)->id;
		const SceneObjectId pinId = windowState.pinnedMeasurements.front().id;

		windowState.paths->Clear();
		windowState.freeLabels.clear();
		windowState.pinnedMeasurements.clear();
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		EXPECT_FALSE(windowState.sceneRegistry.FindObject(pathId));

		RestoreSceneObjectsSnapshot(windowState, snapshot);

		EXPECT_EQ(windowState.paths->Store().At(0)->id, pathId);
		EXPECT_EQ(windowState.pinnedMeasurements.front().id, pinId);
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(pathId));
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(pinId));
	}

	// Ids restored from disk arrive explicitly: the allocator must skip past them, and a copied object
	// that still carries a live id must not steal it.
	TEST(SceneObjectModelTests, ExplicitIdsAdvanceAllocatorAndDuplicatesGetFreshIds)
	{
		SceneRegistry scene;
		Entity loaded = scene.CreateObject(SceneObjectKind::FreeLabel, 0, "loaded", SceneObjectId{42});
		EXPECT_EQ(loaded.GetComponent<SceneObjectComponent>().id, SceneObjectId{42});
		EXPECT_GT(scene.AllocateObjectId().value, 42u);

		RendererWindowState windowState = BuildAnnotatedWindow();
		RendererWindowState::FreeLabel copy = windowState.freeLabels.front();
		windowState.freeLabels.push_back(copy);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);

		EXPECT_NE(windowState.freeLabels.back().id, windowState.freeLabels.front().id);
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(windowState.freeLabels.front().id));
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(windowState.freeLabels.back().id));
	}

} // namespace DefectStudio::Tests
