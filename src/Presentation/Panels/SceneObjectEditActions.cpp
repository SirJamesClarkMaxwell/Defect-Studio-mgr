#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneObjectEditActions.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

#include "Presentation/Panels/SceneArrowEditorWidget.hpp"
#include "Presentation/Panels/SceneOrbitalEditorWidget.hpp"
#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr glm::vec3 kFreeLabelCopyOffset(0.5f, 0.0f, 0.0f);

		[[nodiscard]] std::size_t FindFreeLabelIndex(
			const RendererWindowState &windowState, const SceneObjectId id)
		{
			const auto found = std::find_if(
				windowState.freeLabels.begin(), windowState.freeLabels.end(),
				[id](const RendererWindowState::FreeLabel &label) { return label.id == id; });
			return found == windowState.freeLabels.end()
				? windowState.freeLabels.size()
				: static_cast<std::size_t>(std::distance(windowState.freeLabels.begin(), found));
		}

		[[nodiscard]] const std::vector<SceneObjectId> &SelectionFor(
			const RendererWindowState &windowState, const SceneObjectEditKind kind)
		{
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel: return windowState.selectedFreeLabels;
			case SceneObjectEditKind::Arrow: return windowState.selectedSceneArrows;
			case SceneObjectEditKind::Orbital: return windowState.selectedSceneOrbitals;
			case SceneObjectEditKind::Plane: return windowState.selectedScenePlanes;
			case SceneObjectEditKind::Path: return windowState.selectedScenePaths;
			}
			return windowState.selectedSceneArrows;
		}

		[[nodiscard]] bool ClipboardHasObjects(const SceneObjectEditKind kind)
		{
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel: return !GetSceneFreeLabelClipboard().empty();
			case SceneObjectEditKind::Arrow: return !GetSceneArrowClipboard().empty();
			case SceneObjectEditKind::Orbital: return !GetSceneOrbitalClipboard().empty();
			case SceneObjectEditKind::Plane: return !GetScenePlaneClipboard().empty();
			case SceneObjectEditKind::Path: return !GetScenePathClipboard().empty();
			}
			return false;
		}
	} // namespace

	std::vector<RendererWindowState::FreeLabel> &GetSceneFreeLabelClipboard()
	{
		static std::vector<RendererWindowState::FreeLabel> clipboard;
		return clipboard;
	}

	void CopySceneFreeLabelsToClipboard(const RendererWindowState &windowState)
	{
		std::vector<RendererWindowState::FreeLabel> &clipboard = GetSceneFreeLabelClipboard();
		clipboard.clear();
		for (const SceneObjectId id : windowState.selectedFreeLabels)
		{
			const std::size_t index = FindFreeLabelIndex(windowState, id);
			if (index < windowState.freeLabels.size())
				clipboard.push_back(windowState.freeLabels[index]);
		}
	}

	void DuplicateSelectedSceneFreeLabels(RendererWindowState &windowState)
	{
		if (windowState.selectedFreeLabels.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);

		std::vector<RendererWindowState::FreeLabel> source;
		for (const SceneObjectId id : windowState.selectedFreeLabels)
		{
			const std::size_t index = FindFreeLabelIndex(windowState, id);
			if (index < windowState.freeLabels.size())
				source.push_back(windowState.freeLabels[index]);
		}

		std::vector<SceneObjectId> newIds;
		newIds.reserve(source.size());
		for (RendererWindowState::FreeLabel copy : source)
		{
			copy.id = windowState.sceneRegistry.AllocateObjectId();
			copy.persistKey.clear();
			copy.worldPosition += kFreeLabelCopyOffset;
			newIds.push_back(copy.id);
			windowState.freeLabels.push_back(std::move(copy));
		}
		windowState.selectedFreeLabels = std::move(newIds);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
	}

	void PasteSceneFreeLabelsFromClipboard(RendererWindowState &windowState)
	{
		const std::vector<RendererWindowState::FreeLabel> &clipboard = GetSceneFreeLabelClipboard();
		if (clipboard.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);

		std::vector<SceneObjectId> newIds;
		newIds.reserve(clipboard.size());
		for (RendererWindowState::FreeLabel copy : clipboard)
		{
			copy.id = windowState.sceneRegistry.AllocateObjectId();
			copy.persistKey.clear();
			copy.worldPosition += kFreeLabelCopyOffset;
			newIds.push_back(copy.id);
			windowState.freeLabels.push_back(std::move(copy));
		}
		windowState.selectedFreeLabels = std::move(newIds);
		SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
	}

	void EraseSceneFreeLabels(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids)
	{
		const auto removed = std::remove_if(
			windowState.freeLabels.begin(), windowState.freeLabels.end(),
			[&ids](const RendererWindowState::FreeLabel &label) {
				return std::find(ids.begin(), ids.end(), label.id) != ids.end();
			});
		const bool erasedAny = removed != windowState.freeLabels.end();
		windowState.freeLabels.erase(removed, windowState.freeLabels.end());
		windowState.selectedFreeLabels.clear();
		windowState.freeLabelDragging = false;
		if (erasedAny)
			SceneSystem::SyncLabelEntities(windowState.sceneRegistry, windowState);
	}

	bool CanExecuteSceneObjectEditAction(
		const RendererWindowState &windowState,
		const SceneObjectEditKind kind,
		const SceneObjectEditAction action)
	{
		if (action == SceneObjectEditAction::Paste)
			return ClipboardHasObjects(kind);
		return !SelectionFor(windowState, kind).empty();
	}

	bool ExecuteSceneObjectEditAction(
		RendererWindowState &windowState,
		const SceneObjectEditKind kind,
		const SceneObjectEditAction action)
	{
		if (!CanExecuteSceneObjectEditAction(windowState, kind, action))
			return false;

		switch (action)
		{
		case SceneObjectEditAction::Delete:
			// DeleteScenePaths records the whole batch itself.
			if (kind != SceneObjectEditKind::Path)
				PushPinnedMeasurementUndoSnapshot(windowState);
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel:
				EraseSceneFreeLabels(windowState, windowState.selectedFreeLabels);
				break;
			case SceneObjectEditKind::Arrow:
				EraseSceneArrows(windowState, windowState.selectedSceneArrows);
				break;
			case SceneObjectEditKind::Orbital:
				EraseSceneOrbitals(windowState, windowState.selectedSceneOrbitals);
				break;
			case SceneObjectEditKind::Plane:
				EraseScenePlanes(windowState, windowState.selectedScenePlanes);
				break;
			case SceneObjectEditKind::Path:
				EraseScenePaths(windowState, windowState.selectedScenePaths);
				break;
			}
			break;

		case SceneObjectEditAction::Duplicate:
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel: DuplicateSelectedSceneFreeLabels(windowState); break;
			case SceneObjectEditKind::Arrow: DuplicateSelectedSceneArrows(windowState); break;
			case SceneObjectEditKind::Orbital: DuplicateSelectedSceneOrbitals(windowState); break;
			case SceneObjectEditKind::Plane: DuplicateSelectedScenePlanes(windowState); break;
			case SceneObjectEditKind::Path: DuplicateSelectedScenePaths(windowState); break;
			}
			break;

		case SceneObjectEditAction::Copy:
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel: CopySceneFreeLabelsToClipboard(windowState); break;
			case SceneObjectEditKind::Arrow: CopySceneArrowsToClipboard(windowState); break;
			case SceneObjectEditKind::Orbital: CopySceneOrbitalsToClipboard(windowState); break;
			case SceneObjectEditKind::Plane: CopyScenePlanesToClipboard(windowState); break;
			case SceneObjectEditKind::Path: CopyScenePathsToClipboard(windowState); break;
			}
			break;

		case SceneObjectEditAction::Paste:
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel: PasteSceneFreeLabelsFromClipboard(windowState); break;
			case SceneObjectEditKind::Arrow: PasteSceneArrowsFromClipboard(windowState); break;
			case SceneObjectEditKind::Orbital: PasteSceneOrbitalsFromClipboard(windowState); break;
			case SceneObjectEditKind::Plane: PasteScenePlanesFromClipboard(windowState); break;
			case SceneObjectEditKind::Path: PasteScenePathsFromClipboard(windowState); break;
			}
			break;
		}
		return true;
	}
} // namespace DefectStudio
