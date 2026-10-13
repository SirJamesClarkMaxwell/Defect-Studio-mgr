#pragma once

#include <vector>

#include "Renderer/RendererWindowState.hpp"
#include "Presentation/Panels/SceneObjectClipboard.hpp"

namespace DefectStudio
{
	class CommandRegistry;
	class RendererLayer;
	enum class SceneObjectEditKind
	{
		FreeLabel,
		PinnedMeasurement,
		Orbital,
		Plane,
		Path,
		Density,
		HideVolume
	};

	enum class SceneObjectEditAction
	{
		Delete,
		Duplicate,
		Copy,
		Paste
	};

	// One action entry point shared by viewport keyboard shortcuts and Scene Outliner row menus.
	[[nodiscard]] bool CanExecuteSceneObjectEditAction(
		const RendererWindowState &windowState, SceneObjectEditKind kind, SceneObjectEditAction action);
	bool ExecuteSceneObjectEditAction(
		RendererWindowState &windowState, SceneObjectEditKind kind, SceneObjectEditAction action,
		const glm::vec3 &offset = glm::vec3(0.5f, 0.0f, 0.0f));

	// The kind the last Copy put on a clipboard: Ctrl+V with nothing selected pastes that kind.
	[[nodiscard]] SceneObjectEditKind LastCopiedSceneObjectEditKind();
	void RegisterSceneObjectShortcutCommand(CommandRegistry &registry, RendererLayer &layer);
	// Returns true when scene selection/clipboard owns the shortcut, even if its clipboard is empty.
	[[nodiscard]] bool HandleSceneObjectEditShortcut(RendererWindowState &windowState, SceneObjectEditAction action,
		const glm::vec3 &offset = glm::vec3(0.5f, 0.0f, 0.0f));
	// Any scene object (not an atom or bond) selected - what enables Copy/Duplicate in menus.
	[[nodiscard]] bool HasSceneObjectSelection(const RendererWindowState &windowState);

	[[nodiscard]] std::vector<RendererWindowState::FreeLabel> &GetSceneFreeLabelClipboard();
	void CopySceneFreeLabelsToClipboard(const RendererWindowState &windowState);
	void DuplicateSelectedSceneFreeLabels(RendererWindowState &windowState,
		const glm::vec3 &offset = glm::vec3(0.5f, 0.0f, 0.0f));
	void PasteSceneFreeLabelsFromClipboard(RendererWindowState &windowState);
	void EraseSceneFreeLabels(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);
	[[nodiscard]] SceneObjectClipboard &GetPersistedSceneFreeLabelClipboard();
	void CopyPersistedSceneFreeLabelsToClipboard(const RendererWindowState &windowState);
	void PastePersistedSceneFreeLabelsFromClipboard(RendererWindowState &windowState);
	[[nodiscard]] SceneObjectClipboard &GetScenePinnedMeasurementClipboard();
	void CopyScenePinnedMeasurementsToClipboard(const RendererWindowState &windowState);
	void PasteScenePinnedMeasurementsFromClipboard(RendererWindowState &windowState);
	void DuplicateSelectedScenePinnedMeasurements(RendererWindowState &windowState,
		const glm::vec3 &offset = glm::vec3(0.5f, 0.0f, 0.0f));
	void EraseScenePinnedMeasurements(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);
} // namespace DefectStudio
