#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerPanel.hpp"
#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"

#include <algorithm>
#include <variant>

#include <imgui.h>

#include "Presentation/Panels/ScenePathEditorWidget.hpp"

namespace DefectStudio
{
	bool IsSceneOutlinerVacancyBond(const ScenePath &path)
	{
		return std::any_of(path.nodes.begin(), path.nodes.end(), [](const PathNode &node) {
			return std::holds_alternative<PathBinding::CopyVacancy>(node.binding.value);
		});
	}

	void SceneOutlinerPanel::drawPathsGroup(RendererWindowState &windowState, const bool vacancyBonds)
	{
		if (windowState.paths == nullptr)
			return;
		PathStore &store = windowState.paths->Store();
		std::vector<SceneObjectId> ids;
		SceneVisibilityColumnAccumulator flags;
		store.Visit([&](const ScenePath &path) {
			if (IsSceneOutlinerVacancyBond(path) == vacancyBonds)
			{
				ids.push_back(path.id);
				flags.Add(path.visible, path.renderable);
			}
		});
		if (vacancyBonds && ids.empty())
			return;
		ImGui::PushID(vacancyBonds ? "##vacancyBondsGroup" : "##pathsGroup");
		ImGui::SetNextItemAllowOverlap();
		const bool open = ImGui::TreeNodeEx(
			"##paths", ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth,
			vacancyBonds ? "Wiązania wakansów (%zu)" : "Ścieżki (%zu)", ids.size());
		const auto edit = DrawSceneVisibilityColumns(SceneVisibilityStateFrom(flags));
		if (edit.visibleChanged || edit.renderableChanged)
			for (const SceneObjectId id : ids)
				store.MutateStyle(id, [&edit](ScenePath &path) {
					if (edit.visibleChanged) path.visible = edit.visible;
					if (edit.renderableChanged) path.renderable = edit.renderable;
				});
		if (open)
		{
			for (std::size_t index = 0; index < store.Size(); ++index)
			{
				const ScenePath *path = store.At(index);
				if (path == nullptr || IsSceneOutlinerVacancyBond(*path) != vacancyBonds)
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
