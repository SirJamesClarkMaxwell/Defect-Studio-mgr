#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerPanel.hpp"
#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "Presentation/Panels/ScenePathEditorWidget.hpp"

namespace DefectStudio
{
	void SceneOutlinerPanel::drawPathsGroup(RendererWindowState &windowState)
	{
		if (windowState.paths == nullptr)
			return;
		PathStore &store = windowState.paths->Store();
		ImGui::PushID("##pathsGroup");
		char groupLabel[32];
		std::snprintf(groupLabel, sizeof(groupLabel), "Paths (%zu)", store.Size());
		ImGui::SetNextItemAllowOverlap();
		const bool open = ImGui::TreeNodeEx(
			"##paths", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", groupLabel);
		ApplySceneVisibilityColumnEdit(store, DrawSceneVisibilityColumns(SceneVisibilityStateFor(store)));
		if (open)
		{
			const std::vector<SceneObjectId> ids = store.Ids();
			for (std::size_t index = 0; index < store.Size(); ++index)
			{
				const ScenePath *path = store.At(index);
				if (path == nullptr)
					continue;
				ImGui::PushID(static_cast<int>(index));
				auto &selection = windowState.selectedScenePaths;
				const bool selected = std::find(selection.begin(), selection.end(), path->id) != selection.end();
				if (selected)
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.85f, 0.42f, 0.05f, 0.85f));
				ImGui::SetNextItemAllowOverlap();
				const bool rowActivated = ImGui::Selectable(ScenePathDisplayName(*path, index).c_str(), selected);
				const bool contextRequested = ImGui::IsItemClicked(ImGuiMouseButton_Right);
				if (selected)
					ImGui::PopStyleColor();
				bool visible = path->visible;
				bool renderable = path->renderable;
				if (DrawSceneVisibilityColumns(visible, renderable))
					store.MutateStyle(path->id, [visible, renderable](ScenePath &edited) {
						edited.visible = visible;
						edited.renderable = renderable;
					});
				if (rowActivated)
					applyAnnotationRowSelection(windowState, SelectionRowKind::Path, ids, path->id, selection);
				const bool sceneMutated = drawSceneObjectContextMenu(
					windowState, SelectionRowKind::Path, path->id, selection, contextRequested);
				ImGui::PopID();
				if (sceneMutated)
					break;
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}
