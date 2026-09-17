#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneOutlinerVisibilityColumns.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace DefectStudio
{
	namespace
	{
		bool DrawColumn(const char *id, const char *tooltip, bool value, const bool mixed)
		{
			if (mixed)
				ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
			const bool changed = ImGui::Checkbox(id, &value);
			if (mixed)
				ImGui::PopItemFlag();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("%s", tooltip);
			ImGui::SameLine();
			return changed;
		}
	} // namespace

	SceneVisibilityColumnEdit DrawSceneVisibilityColumns(const SceneVisibilityColumnState &state)
	{
		SceneVisibilityColumnEdit edit;
		// A mixed column reads as unchecked, so clicking it turns everything under it on - the same
		// direction Blender's outliner takes from a mixed parent.
		edit.visible = state.visibleMixed ? true : !state.visible;
		edit.renderable = state.renderableMixed ? true : !state.renderable;
		edit.visibleChanged = DrawColumn(
			"##outlinerVisible", "Visible in the viewport (H)", state.visibleMixed ? false : state.visible,
			state.visibleMixed);
		edit.renderableChanged = DrawColumn(
			"##outlinerRenderable", "Included in an exported render", state.renderableMixed ? false : state.renderable,
			state.renderableMixed);
		return edit;
	}

	bool DrawSceneVisibilityColumns(bool &visible, bool &renderable)
	{
		const SceneVisibilityColumnEdit edit = DrawSceneVisibilityColumns(
			SceneVisibilityColumnState{visible, renderable, false, false});
		if (edit.visibleChanged)
			visible = edit.visible;
		if (edit.renderableChanged)
			renderable = edit.renderable;
		return edit.visibleChanged || edit.renderableChanged;
	}
} // namespace DefectStudio
