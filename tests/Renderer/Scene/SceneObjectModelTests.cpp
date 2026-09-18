#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneComponents.hpp"
#include "Renderer/Scene/SceneObject.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Three atoms, one bond, one pinned bond-length measurement, one free label, two arrows -
		// one of every SceneObjectKind, with two of the kind the index-shift bug bites hardest.
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
				RendererWindowState::SceneArrow arrow;
				arrow.id = windowState.sceneRegistry.AllocateObjectId();
				arrow.start() = glm::vec3(static_cast<float>(index), 0.0f, 0.0f);
				arrow.end() = glm::vec3(static_cast<float>(index), 1.0f, 0.0f);
				windowState.sceneArrows.push_back(arrow);
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
		const SceneObjectId arrowId = windowState.sceneArrows.front().id;
		const SceneObjectId labelId = windowState.freeLabels.front().id;

		SceneSystem::SyncSceneWithStructure(windowState.sceneRegistry, windowState.structure);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);

		EXPECT_EQ(CollectAtomIds(windowState.sceneRegistry), atomIdsBefore);
		EXPECT_EQ(windowState.sceneArrows.front().id, arrowId);
		EXPECT_EQ(windowState.freeLabels.front().id, labelId);

		Entity resolvedArrow = windowState.sceneRegistry.FindObject(arrowId);
		ASSERT_TRUE(resolvedArrow);
		const SceneObjectComponent &component = resolvedArrow.GetComponent<SceneObjectComponent>();
		EXPECT_EQ(component.kind, SceneObjectKind::SceneArrow);
		EXPECT_EQ(component.sourceIndex, 0u);
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(labelId));
	}

	// Criterion 2 - one enumeration point for every kind.
	TEST(SceneObjectModelTests, RegistryEnumeratesEveryObjectExactlyOnce)
	{
		RendererWindowState windowState = BuildAnnotatedWindow();

		std::unordered_set<SceneObjectId> seen;
		std::vector<std::size_t> countsByKind(5, 0);
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
		EXPECT_EQ(countsByKind[static_cast<std::size_t>(SceneObjectKind::SceneArrow)], 2u);
		EXPECT_EQ(seen.size(), 8u);
	}

	// Criterion 3 - a dead id stays dead, and its number is never handed out again.
	TEST(SceneObjectModelTests, DestroyedIdNeverResolvesAndIsNeverReused)
	{
		RendererWindowState windowState = BuildAnnotatedWindow();
		const SceneObjectId removedId = windowState.sceneArrows.back().id;

		windowState.sceneArrows.pop_back();
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);

		EXPECT_FALSE(windowState.sceneRegistry.IsAlive(removedId));
		EXPECT_FALSE(windowState.sceneRegistry.FindObject(removedId));

		RendererWindowState::SceneArrow replacement;
		replacement.id = windowState.sceneRegistry.AllocateObjectId();
		EXPECT_NE(replacement.id, removedId);
		windowState.sceneArrows.push_back(replacement);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);

		EXPECT_FALSE(windowState.sceneRegistry.FindObject(removedId));
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(replacement.id));
	}

	// Criterion 4 - the index-shift bug. Selecting the second arrow and deleting the first used to
	// leave the selection pointing at whatever slid into the freed slot.
	TEST(SceneObjectModelTests, SelectionSurvivesDeletingADifferentArrow)
	{
		RendererWindowState windowState = BuildAnnotatedWindow();
		const SceneObjectId selectedId = windowState.sceneArrows.back().id;
		const glm::vec3 selectedStart = windowState.sceneArrows.back().start();
		windowState.selectedSceneArrows = {selectedId};

		windowState.sceneArrows.erase(windowState.sceneArrows.begin());
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);

		ASSERT_EQ(windowState.selectedSceneArrows.size(), 1u);
		EXPECT_EQ(windowState.selectedSceneArrows.front(), selectedId);

		const std::vector<std::size_t> indices =
			SceneSystem::ResolveSourceIndices(windowState.sceneRegistry, windowState.selectedSceneArrows);
		ASSERT_EQ(indices.size(), 1u);
		EXPECT_EQ(indices.front(), 0u); // it shifted down, and the id followed it
		EXPECT_EQ(windowState.sceneArrows[indices.front()].start(), selectedStart);
	}

	// Criterion 5 - the shared label/arrow undo stack snapshots whole vectors, so a restore must not
	// renumber anything.
	TEST(SceneObjectModelTests, IdsSurviveAnUndoRedoRoundTrip)
	{
		RendererWindowState windowState = BuildAnnotatedWindow();
		RendererWindowState::LabelUndoSnapshot snapshot{
			windowState.pinnedMeasurements, windowState.freeLabels, windowState.sceneArrows};
		const SceneObjectId arrowId = windowState.sceneArrows.front().id;
		const SceneObjectId pinId = windowState.pinnedMeasurements.front().id;

		windowState.sceneArrows.clear();
		windowState.freeLabels.clear();
		windowState.pinnedMeasurements.clear();
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
		EXPECT_FALSE(windowState.sceneRegistry.FindObject(arrowId));

		windowState.pinnedMeasurements = snapshot.pinnedMeasurements;
		windowState.freeLabels = snapshot.freeLabels;
		windowState.sceneArrows = snapshot.sceneArrows;
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);

		EXPECT_EQ(windowState.sceneArrows.front().id, arrowId);
		EXPECT_EQ(windowState.pinnedMeasurements.front().id, pinId);
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(arrowId));
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
		RendererWindowState::SceneArrow copy = windowState.sceneArrows.front();
		windowState.sceneArrows.push_back(copy);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);

		EXPECT_NE(windowState.sceneArrows.back().id, windowState.sceneArrows.front().id);
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(windowState.sceneArrows.front().id));
		EXPECT_TRUE(windowState.sceneRegistry.FindObject(windowState.sceneArrows.back().id));
	}

	// Stale ids are dropped, not mapped onto a wrong object - the contract the render call sites
	// depend on when they convert back to indices for OpenGlRendererBackend.
	TEST(SceneObjectModelTests, ResolveSourceIndicesDropsDeadIds)
	{
		RendererWindowState windowState = BuildAnnotatedWindow();
		const SceneObjectId liveId = windowState.sceneArrows.front().id;
		const SceneObjectId deadId = windowState.sceneArrows.back().id;

		windowState.sceneArrows.pop_back();
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);

		const std::vector<std::size_t> indices =
			SceneSystem::ResolveSourceIndices(windowState.sceneRegistry, {liveId, deadId});
		ASSERT_EQ(indices.size(), 1u);
		EXPECT_EQ(indices.front(), 0u);
	}
} // namespace DefectStudio::Tests
