#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneVisibility.hpp"

#include <algorithm>

#include "Renderer/Path/PathSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename Objects, typename Ids>
		void SetVisibleWhereSelected(Objects &objects, const Ids &selectedIds, const bool visible)
		{
			for (auto &object : objects)
				if (std::find(selectedIds.begin(), selectedIds.end(), object.id) != selectedIds.end())
					object.visible = visible;
		}

		template <typename Objects>
		void SetVisibleEverywhere(Objects &objects, const bool visible)
		{
			for (auto &object : objects)
				object.visible = visible;
		}

		// Paths live in a PathStore, not a vector, so they get their own three. MutateStyle is the
		// only mutable access there is; it bumps the style revision, which costs one re-mesh per
		// visibility toggle. That is the right trade for a user action that happens by hand - the
		// alternative is a third revision counter for a flag the mesher never reads.
		void ForEachPath(RendererWindowState &window, const std::function<void(ScenePath &)> &mutate)
		{
			if (window.paths == nullptr)
				return;
			PathStore &store = window.paths->Store();
			for (const SceneObjectId id : store.Ids())
				store.MutateStyle(id, mutate);
		}

		template <typename Objects>
		void CollapseOntoRenderable(Objects &objects)
		{
			for (auto &object : objects)
				object.visible = object.renderable;
		}
	} // namespace

	bool AnySceneObjectSelected(const RendererWindowState &windowState)
	{
		return !windowState.selectedAtomIndices.empty() || !windowState.selectedPinnedMeasurements.empty() ||
			!windowState.selectedFreeLabels.empty() || !windowState.selectedSceneArrows.empty() ||
			!windowState.selectedSceneOrbitals.empty() || !windowState.selectedScenePlanes.empty() ||
			!windowState.selectedScenePaths.empty();
	}

	void SetSelectedSceneObjectsVisible(RendererWindowState &windowState, const bool visible)
	{
		SetVisibleWhereSelected(windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements, visible);
		SetVisibleWhereSelected(windowState.freeLabels, windowState.selectedFreeLabels, visible);
		SetVisibleWhereSelected(windowState.sceneArrows, windowState.selectedSceneArrows, visible);
		SetVisibleWhereSelected(windowState.sceneOrbitals, windowState.selectedSceneOrbitals, visible);
		SetVisibleWhereSelected(windowState.scenePlanes, windowState.selectedScenePlanes, visible);
		const std::vector<SceneObjectId> &selectedPaths = windowState.selectedScenePaths;
		ForEachPath(windowState, [&selectedPaths, visible](ScenePath &path) {
			if (std::find(selectedPaths.begin(), selectedPaths.end(), path.id) != selectedPaths.end())
				path.visible = visible;
		});
	}

	void ShowAllSceneObjects(RendererWindowState &windowState)
	{
		SetVisibleEverywhere(windowState.pinnedMeasurements, true);
		SetVisibleEverywhere(windowState.freeLabels, true);
		SetVisibleEverywhere(windowState.sceneArrows, true);
		SetVisibleEverywhere(windowState.sceneOrbitals, true);
		SetVisibleEverywhere(windowState.scenePlanes, true);
		ForEachPath(windowState, [](ScenePath &path) { path.visible = true; });
	}

	void ApplyRenderPassVisibility(RendererWindowState &windowState)
	{
		CollapseOntoRenderable(windowState.structure.atoms);
		CollapseOntoRenderable(windowState.structure.bonds);
		CollapseOntoRenderable(windowState.pinnedMeasurements);
		CollapseOntoRenderable(windowState.freeLabels);
		CollapseOntoRenderable(windowState.sceneArrows);
		CollapseOntoRenderable(windowState.sceneOrbitals);
		CollapseOntoRenderable(windowState.scenePlanes);
		ForEachPath(windowState, [](ScenePath &path) { path.visible = path.renderable; });
	}
} // namespace DefectStudio
