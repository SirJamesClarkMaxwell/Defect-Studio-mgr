#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneObjectEditActions.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <utility>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Presentation/Panels/SceneDensityEditor.hpp"
#include "Presentation/Panels/SceneHideVolumeEditor.hpp"
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
			case SceneObjectEditKind::PinnedMeasurement: return windowState.selectedPinnedMeasurements;
			case SceneObjectEditKind::Orbital: return windowState.selectedSceneOrbitals;
			case SceneObjectEditKind::Plane: return windowState.selectedScenePlanes;
			case SceneObjectEditKind::Path: return windowState.selectedScenePaths;
			case SceneObjectEditKind::Density: return windowState.selectedSceneDensities;
			case SceneObjectEditKind::HideVolume: return windowState.selectedSceneHideVolumes;
			}
			return windowState.selectedScenePaths;
		}

		[[nodiscard]] bool ClipboardHasObjects(const SceneObjectEditKind kind)
		{
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel: return !GetPersistedSceneFreeLabelClipboard().empty();
			case SceneObjectEditKind::PinnedMeasurement: return !GetScenePinnedMeasurementClipboard().empty();
			case SceneObjectEditKind::Orbital: return !GetSceneOrbitalClipboard().empty();
			case SceneObjectEditKind::Plane: return !GetScenePlaneClipboard().empty();
			case SceneObjectEditKind::Path: return !GetScenePathClipboard().empty();
			case SceneObjectEditKind::Density: return !GetSceneDensityClipboard().empty();
			case SceneObjectEditKind::HideVolume: return !GetSceneHideVolumeClipboard().empty();
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

	namespace
	{
		SceneObjectEditKind &LastCopiedKind()
		{
			static SceneObjectEditKind kind = SceneObjectEditKind::Path;
			return kind;
		}
		bool &SceneClipboardActive()
		{
			static bool active = false;
			return active;
		}
	} // namespace

	SceneObjectEditKind LastCopiedSceneObjectEditKind()
	{
		return LastCopiedKind();
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
			// Paths and hide volumes record the whole batch themselves.
			if (kind != SceneObjectEditKind::Path && kind != SceneObjectEditKind::HideVolume)
				PushPinnedMeasurementUndoSnapshot(windowState);
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel:
				EraseSceneFreeLabels(windowState, windowState.selectedFreeLabels);
				break;
			case SceneObjectEditKind::PinnedMeasurement:
				EraseScenePinnedMeasurements(windowState, windowState.selectedPinnedMeasurements);
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
			case SceneObjectEditKind::Density:
				EraseSceneDensities(windowState, windowState.selectedSceneDensities);
				break;
			case SceneObjectEditKind::HideVolume:
				EraseSceneHideVolumes(windowState, windowState.selectedSceneHideVolumes);
				break;
			}
			break;

		case SceneObjectEditAction::Duplicate:
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel: DuplicateSelectedSceneFreeLabels(windowState); break;
			case SceneObjectEditKind::PinnedMeasurement: DuplicateSelectedScenePinnedMeasurements(windowState); break;
			case SceneObjectEditKind::Orbital: DuplicateSelectedSceneOrbitals(windowState); break;
			case SceneObjectEditKind::Plane: DuplicateSelectedScenePlanes(windowState); break;
			case SceneObjectEditKind::Path: DuplicateSelectedScenePaths(windowState); break;
			case SceneObjectEditKind::Density: DuplicateSelectedSceneDensities(windowState); break;
			case SceneObjectEditKind::HideVolume: DuplicateSelectedSceneHideVolumes(windowState); break;
			}
			break;

		case SceneObjectEditAction::Copy:
			LastCopiedKind() = kind;
			SceneClipboardActive() = true;
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel: CopyPersistedSceneFreeLabelsToClipboard(windowState); break;
			case SceneObjectEditKind::PinnedMeasurement: CopyScenePinnedMeasurementsToClipboard(windowState); break;
			case SceneObjectEditKind::Orbital: CopySceneOrbitalsToClipboard(windowState); break;
			case SceneObjectEditKind::Plane: CopyScenePlanesToClipboard(windowState); break;
			case SceneObjectEditKind::Path: CopyScenePathsToClipboard(windowState); break;
			case SceneObjectEditKind::Density: CopySceneDensitiesToClipboard(windowState); break;
			case SceneObjectEditKind::HideVolume: CopySceneHideVolumesToClipboard(windowState); break;
			}
			break;

		case SceneObjectEditAction::Paste:
			switch (kind)
			{
			case SceneObjectEditKind::FreeLabel: PastePersistedSceneFreeLabelsFromClipboard(windowState); break;
			case SceneObjectEditKind::PinnedMeasurement: PasteScenePinnedMeasurementsFromClipboard(windowState); break;
			case SceneObjectEditKind::Orbital: PasteSceneOrbitalsFromClipboard(windowState); break;
			case SceneObjectEditKind::Plane: PasteScenePlanesFromClipboard(windowState); break;
			case SceneObjectEditKind::Path: PasteScenePathsFromClipboard(windowState); break;
			case SceneObjectEditKind::Density: PasteSceneDensitiesFromClipboard(windowState); break;
			case SceneObjectEditKind::HideVolume: PasteSceneHideVolumesFromClipboard(windowState); break;
			}
			break;
		}
		return true;
	}

	bool HandleSceneObjectEditShortcut(RendererWindowState &window, SceneObjectEditAction action)
	{
		if (!window.selectedAtomIndices.empty() || !window.selectedBondIndices.empty())
		{
			if (action == SceneObjectEditAction::Copy) SceneClipboardActive() = false;
			// Paste follows the most recent Copy: a copied scene object (e.g. a hide volume, which
			// then anchors to the selected atoms) still wins over the atom clipboard.
			if (action == SceneObjectEditAction::Paste && SceneClipboardActive())
			{
				(void)ExecuteSceneObjectEditAction(window, LastCopiedKind(), action);
				return true;
			}
			return false;
		}
		if (window.pathEdit.IsActive() || window.modalTransform.has_value()) return true;
		constexpr SceneObjectEditKind kinds[] = {SceneObjectEditKind::Orbital, SceneObjectEditKind::Plane,
			SceneObjectEditKind::Path, SceneObjectEditKind::FreeLabel, SceneObjectEditKind::Density,
			SceneObjectEditKind::HideVolume, SceneObjectEditKind::PinnedMeasurement};
		bool selected = false;
		for (const auto kind : kinds)
		{
			if (SelectionFor(window, kind).empty()) continue;
			selected = true;
			(void)ExecuteSceneObjectEditAction(window, kind, action);
			if (action != SceneObjectEditAction::Delete) return true;
		}
		if (selected || !window.selectedVacancies.empty()) return true;
		if (action == SceneObjectEditAction::Paste && SceneClipboardActive())
		{
			(void)ExecuteSceneObjectEditAction(window, LastCopiedKind(), action);
			return true;
		}
		return false;
	}

	bool HasSceneObjectSelection(const RendererWindowState &window)
	{
		for (const auto kind : {SceneObjectEditKind::Orbital, SceneObjectEditKind::Plane, SceneObjectEditKind::Path,
			SceneObjectEditKind::FreeLabel, SceneObjectEditKind::Density, SceneObjectEditKind::HideVolume,
			SceneObjectEditKind::PinnedMeasurement})
			if (!SelectionFor(window, kind).empty()) return true;
		return false;
	}

	namespace
	{
		class SceneObjectShortcutCommand final : public ICommand
		{
		public:
			explicit SceneObjectShortcutCommand(RendererLayer &layer) : m_Layer(layer) {}
			Result<void> Execute(CommandContext &context) override
			{
				const auto requested = context.GetCopy<std::string>("scene_objects.shortcut_action");
				const auto handled = context.GetCopy<Ref<bool>>("scene_objects.shortcut_handled");
				if (!requested) return requested.Error();
				if (!handled) return handled.Error();
				if (!handled.Value()) return {};
				SceneObjectEditAction action;
				if (requested.Value() == "renderer.selection.delete") action = SceneObjectEditAction::Delete;
				else if (requested.Value() == "renderer.selection.copy") action = SceneObjectEditAction::Copy;
				else if (requested.Value() == "renderer.selection.paste") action = SceneObjectEditAction::Paste;
				else if (requested.Value() == "renderer.selection.duplicate") action = SceneObjectEditAction::Duplicate;
				else return {};
				auto &layer = m_Layer.get();
				const auto &focusedId = layer.GetFocusedViewportWindowId();
				for (auto &window : layer.GetWindows())
					if (window.windowId == focusedId || (focusedId.empty() && layer.GetWindows().size() == 1))
					{
						*handled.Value() = HandleSceneObjectEditShortcut(window, action);
						break;
					}
				return {};
			}
			std::string Description() const override { return "Edycja obiektów sceny"; }
		private:
			std::reference_wrapper<RendererLayer> m_Layer;
		};
	}

	void RegisterSceneObjectShortcutCommand(CommandRegistry &registry, RendererLayer &layer)
	{
		const CommandID id{"renderer.scene_objects.shortcut"};
		if (registry.HasCommand(id)) return;
		const auto result = registry.Register({id, "Edycja obiektów sceny", "Renderer", "Edycja obiektów sceny", {},
			CommandFlags::HiddenFromPalette}, [renderer = std::ref(layer)](CommandContext &) -> Unique<ICommand> {
				return CreateUnique<SceneObjectShortcutCommand>(renderer.get());
			});
		if (!result) DS_LOG_WARN("Scene object shortcut registration failed: {}", result.Error().technicalDetails);
	}
} // namespace DefectStudio
