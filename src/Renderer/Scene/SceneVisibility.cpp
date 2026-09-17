#include "Core/dspch.hpp"

#include "Renderer/Scene/SceneVisibility.hpp"

#include <algorithm>

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
			!windowState.selectedSceneOrbitals.empty() || !windowState.selectedScenePlanes.empty();
	}

	void SetSelectedSceneObjectsVisible(RendererWindowState &windowState, const bool visible)
	{
		SetVisibleWhereSelected(windowState.pinnedMeasurements, windowState.selectedPinnedMeasurements, visible);
		SetVisibleWhereSelected(windowState.freeLabels, windowState.selectedFreeLabels, visible);
		SetVisibleWhereSelected(windowState.sceneArrows, windowState.selectedSceneArrows, visible);
		SetVisibleWhereSelected(windowState.sceneOrbitals, windowState.selectedSceneOrbitals, visible);
		SetVisibleWhereSelected(windowState.scenePlanes, windowState.selectedScenePlanes, visible);
	}

	void ShowAllSceneObjects(RendererWindowState &windowState)
	{
		SetVisibleEverywhere(windowState.pinnedMeasurements, true);
		SetVisibleEverywhere(windowState.freeLabels, true);
		SetVisibleEverywhere(windowState.sceneArrows, true);
		SetVisibleEverywhere(windowState.sceneOrbitals, true);
		SetVisibleEverywhere(windowState.scenePlanes, true);
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
	}
} // namespace DefectStudio
