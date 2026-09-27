#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathEditorWidget.hpp"

#include <algorithm>
#include <glm/gtc/quaternion.hpp>

#include <imgui.h>

#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename T>
		void MarkMixed(bool &mixed, const T &first, const T &value)
		{
			mixed = mixed || !(first == value);
		}
		[[nodiscard]] std::string EllipsizeLabel(const char *label, const float width)
		{
			if (ImGui::CalcTextSize(label).x <= width)
				return label;
			const std::string ellipsis = "...";
			std::string result = label;
			while (!result.empty())
			{
				result.pop_back();
				const std::string candidate = result + ellipsis;
				if (ImGui::CalcTextSize(candidate.c_str()).x <= width)
					return candidate;
			}
			return ellipsis;
		}

		enum TransformFields : unsigned
		{
			PositionField = 1u,
			RotationField = 2u,
			ScaleField = 4u,
			AllTransformFields = PositionField | RotationField | ScaleField,
		};

		std::size_t ApplyScenePathTransformFields(
			RendererWindowState &windowState, const std::vector<SceneObjectId> &selection,
			const ScenePathTransformEdit &edit, const unsigned fields, const bool recordUndo)
		{
			if (windowState.paths == nullptr || selection.empty())
				return 0;
			const PathEditContext context = recordUndo
				? MakeWindowPathEditContext(windowState)
				: MakeSilentPathEditContext(windowState);
			const PathEditReport report = ApplyPathEdit(context, selection, PathRevisionKind::Geometry,
				"Edit path transform", [&edit, fields](ScenePath &path) {
					if ((fields & PositionField) != 0u)
						path.transform.position = edit.position;
					if ((fields & RotationField) != 0u)
						path.transform.rotation = glm::normalize(glm::quat(glm::radians(edit.rotationDegrees)));
					if ((fields & ScaleField) != 0u)
						path.transform.scale = edit.scale;
					return Result<void>{};
				});
			return report.applied.size();
		}
	}

	ScenePathEditorLayout MeasureScenePathEditorLayout(
		const float availableWidth, const char *const *labels, const std::size_t labelCount)
	{
		const ImGuiStyle &style = ImGui::GetStyle();
		float widestLabel = 0.0f;
		for (std::size_t index = 0; index < labelCount; ++index)
			widestLabel = std::max(widestLabel, ImGui::CalcTextSize(labels[index]).x);
		const float mixedWidth = ImGui::CalcTextSize("(mixed)").x;
		const float labelWidth = widestLabel + style.ItemSpacing.x + mixedWidth + style.CellPadding.x * 2.0f;
		const float labelMinimumWidth = ImGui::CalcTextSize("...").x + style.CellPadding.x * 2.0f;
		const float numericWidth = ImGui::CalcTextSize("-0.000").x + style.FramePadding.x * 2.0f;
		const float componentWidth = numericWidth + style.CellPadding.x * 2.0f;
		const float enumWidth = std::max({
			ImGui::CalcTextSize("Camera-facing").x,
			ImGui::CalcTextSize("Always on top").x,
			ImGui::CalcTextSize("Path #000").x}) + style.FramePadding.x * 2.0f + style.CellPadding.x * 2.0f;
		const float fieldMinimumWidth = std::max(
			enumWidth, numericWidth * 3.0f + style.ItemInnerSpacing.x * 2.0f + style.CellPadding.x * 2.0f);
		const float labelCap = availableWidth * 0.5f;
		const float labelBudget = availableWidth - fieldMinimumWidth;
		return {
			std::max(labelMinimumWidth, std::min(labelWidth, std::min(labelCap, labelBudget))),
			fieldMinimumWidth,
			componentWidth};
	}

	void DrawScenePathEditorLabel(const char *label, const bool mixed)
	{
		const ImGuiStyle &style = ImGui::GetStyle();
		const float mixedWidth = ImGui::CalcTextSize("(mixed)").x;
		const float availableWidth = ImGui::GetContentRegionAvail().x;
		const float labelWidth = mixed ? std::max(ImGui::CalcTextSize("...").x,
			availableWidth - style.ItemSpacing.x - mixedWidth) : availableWidth;
		const std::string visibleLabel = EllipsizeLabel(label, labelWidth);
		ImGui::TextUnformatted(visibleLabel.c_str());
		if (mixed)
		{
			ImGui::SameLine(0.0f, style.ItemSpacing.x);
			ImGui::TextDisabled("(mixed)");
		}
	}

	ScenePathTransformEditState ResolveScenePathTransformEdit(
		const RendererWindowState &windowState, const std::vector<SceneObjectId> &selection)
	{
		ScenePathTransformEditState state;
		if (windowState.paths == nullptr)
			return state;
		const ScenePath *first = nullptr;
		for (const SceneObjectId id : selection)
		{
			const ScenePath *path = windowState.paths->Store().Find(id);
			if (path == nullptr)
				continue;
			if (first == nullptr)
			{
				first = path;
				state.values.position = path->transform.position;
				state.values.rotationDegrees = glm::degrees(glm::eulerAngles(glm::normalize(path->transform.rotation)));
				state.values.scale = path->transform.scale;
				state.resolved = 1;
				continue;
			}
			++state.resolved;
			MarkMixed(state.mixedPosition, first->transform.position, path->transform.position);
			MarkMixed(state.mixedRotation, first->transform.rotation, path->transform.rotation);
			MarkMixed(state.mixedScale, first->transform.scale, path->transform.scale);
		}
		return state;
	}

	std::size_t ApplyScenePathTransformEdit(
		RendererWindowState &windowState, const std::vector<SceneObjectId> &selection,
		const ScenePathTransformEdit &edit)
	{
		return ApplyScenePathTransformFields(windowState, selection, edit, AllTransformFields, true);
	}

	bool DrawScenePathTransformEditor(RendererWindowState &windowState)
	{
		const std::vector<SceneObjectId> selection = windowState.selectedScenePaths;
		const ScenePathTransformEditState resolved = ResolveScenePathTransformEdit(windowState, selection);
		if (resolved.resolved == 0)
			return false;

		ScenePathTransformEdit edit = resolved.values;
		bool changed = false;
		const char *transformLabels[] = {"Location", "Rotation", "Scale"};
		const ScenePathEditorLayout layout = MeasureScenePathEditorLayout(
			ImGui::GetContentRegionAvail().x, transformLabels, 3);
		const char *axisLabels[] = {"X", "Y", "Z"};

		ImGui::Separator();
		ImGui::Text("Transform");
		if (!ImGui::BeginTable("##ScenePathTransformEditor", 4,
			ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings))
			return false;
		ImGui::TableSetupColumn(
			"##TransformLabel", ImGuiTableColumnFlags_WidthFixed, layout.labelColumnWidth);
		for (const char *axisLabel : axisLabels)
			ImGui::TableSetupColumn(
				axisLabel, ImGuiTableColumnFlags_WidthFixed, layout.componentColumnWidth);
		ImGui::TableNextRow();
		for (int axis = 0; axis < 3; ++axis)
		{
			ImGui::TableSetColumnIndex(axis + 1);
			ImGui::TextUnformatted(axisLabels[axis]);
		}

		const auto drawField = [&](const char *label, glm::vec3 &value, const bool mixed,
			const float speed, const unsigned field) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			if (mixed)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(mixed)");
			}
			ImGui::PushID(label);
			for (int axis = 0; axis < 3; ++axis)
			{
				ImGui::TableSetColumnIndex(axis + 1);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::PushID(axis);
				const bool controlChanged = ImGui::DragFloat("##value", &value[axis], speed, 0.0f, 0.0f, "%.3f");
				if (ImGui::IsItemActivated())
					BeginScenePathStyleDrag(windowState);
				if (controlChanged)
				{
					const bool recordUndo = !windowState.scenePathStyleEditBefore.has_value();
					changed = ApplyScenePathTransformFields(windowState, selection, edit, field, recordUndo) != 0 || changed;
				}
				if (ImGui::IsItemDeactivatedAfterEdit())
					CommitScenePathStyleDrag(windowState);
				ImGui::PopID();
			}
			ImGui::PopID();
		};
		drawField("Location", edit.position, resolved.mixedPosition, 0.01f, PositionField);
		drawField("Rotation", edit.rotationDegrees, resolved.mixedRotation, 1.0f, RotationField);
		drawField("Scale", edit.scale, resolved.mixedScale, 0.01f, ScaleField);
		ImGui::EndTable();
		return changed;
	}
} // namespace DefectStudio
