#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesPanelSections.hpp"

#include <imgui.h>

#include "Presentation/Panels/SceneObjectEditActions.hpp"
#include "Presentation/Panels/ScenePathEditorWidget.hpp"

namespace DefectStudio
{
	void DrawSelectedScenePathSection(RendererWindowState &windowState)
	{
		ImGui::Separator();
		DrawScenePathTransformEditor(windowState);
		DrawScenePathEditor(windowState);
		if (ImGui::Button("Delete##SelectedPath"))
			ExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, SceneObjectEditAction::Delete);
		ImGui::SameLine();
		if (ImGui::Button("Duplicate##SelectedPath"))
			ExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, SceneObjectEditAction::Duplicate);
		ImGui::SameLine();
		if (ImGui::Button("Copy##SelectedPath"))
			ExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, SceneObjectEditAction::Copy);
		ImGui::SameLine();
		if (ImGui::Button("Paste##SelectedPath"))
			ExecuteSceneObjectEditAction(windowState, SceneObjectEditKind::Path, SceneObjectEditAction::Paste);
	}
}
