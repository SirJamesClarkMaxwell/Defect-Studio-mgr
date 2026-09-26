#pragma once

#include <vector>

#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	enum class SceneObjectEditKind
	{
		FreeLabel,
		Arrow,
		Orbital,
		Plane,
		Path
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
		RendererWindowState &windowState, SceneObjectEditKind kind, SceneObjectEditAction action);

	[[nodiscard]] std::vector<RendererWindowState::FreeLabel> &GetSceneFreeLabelClipboard();
	void CopySceneFreeLabelsToClipboard(const RendererWindowState &windowState);
	void DuplicateSelectedSceneFreeLabels(RendererWindowState &windowState);
	void PasteSceneFreeLabelsFromClipboard(RendererWindowState &windowState);
	void EraseSceneFreeLabels(RendererWindowState &windowState, const std::vector<SceneObjectId> &ids);
} // namespace DefectStudio
