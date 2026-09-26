#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathEditorWidget.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "Presentation/Panels/ScenePathOperations.hpp"

namespace DefectStudio
{
	namespace
	{
		template <typename T>
		void MarkMixed(bool &mixed, const T &first, const T &value)
		{
			mixed = mixed || !(first == value);
		}

		[[nodiscard]] bool DrawEnumCombo(const char *label, StrokeProfile &value)
		{
			const char *names[] = {"Round", "Flat", "Camera-facing"};
			int index = static_cast<int>(value);
			const bool changed = ImGui::Combo(label, &index, names, 3);
			if (changed)
				value = static_cast<StrokeProfile>(index);
			return changed;
		}

		[[nodiscard]] bool DrawEnumCombo(const char *label, PathDecorationKind &value)
		{
			const char *names[] = {"None", "Arrow", "Stealth", "Open arrow", "Bar", "Circle", "Square", "Diamond"};
			int index = static_cast<int>(value);
			const bool changed = ImGui::Combo(label, &index, names, 8);
			if (changed)
				value = static_cast<PathDecorationKind>(index);
			return changed;
		}

		[[nodiscard]] bool DrawEnumCombo(const char *label, PathDepthMode &value)
		{
			const char *names[] = {"Depth test", "Always on top"};
			int index = static_cast<int>(value);
			const bool changed = ImGui::Combo(label, &index, names, 2);
			if (changed)
				value = static_cast<PathDepthMode>(index);
			return changed;
		}
	}

	ScenePathStyleEditState ResolveScenePathStyleEdit(
		const RendererWindowState &windowState, const std::vector<SceneObjectId> &selection)
	{
		ScenePathStyleEditState state;
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
				state.values.profile = path->style.profile;
				state.values.width = path->style.width;
				state.values.alpha = path->style.alpha;
				state.values.color = path->style.color;
				state.values.startDecoration = path->style.startDecoration.kind;
				state.values.endDecoration = path->style.endDecoration.kind;
				state.values.depthMode = path->style.depthMode;
				state.resolved = 1;
				continue;
			}
			++state.resolved;
			MarkMixed(state.mixedProfile, state.values.profile, path->style.profile);
			MarkMixed(state.mixedWidth, state.values.width, path->style.width);
			MarkMixed(state.mixedAlpha, state.values.alpha, path->style.alpha);
			MarkMixed(state.mixedColor, state.values.color, path->style.color);
			MarkMixed(state.mixedStartDecoration, state.values.startDecoration, path->style.startDecoration.kind);
			MarkMixed(state.mixedEndDecoration, state.values.endDecoration, path->style.endDecoration.kind);
			MarkMixed(state.mixedDepthMode, state.values.depthMode, path->style.depthMode);
		}
		return state;
	}

	std::size_t ApplyScenePathStyleEdit(
		RendererWindowState &windowState, const std::vector<SceneObjectId> &selection,
		const ScenePathStyleEdit &edit)
	{
		if (windowState.paths == nullptr || selection.empty())
			return 0;
		const PathEditReport report = SetScenePathStyle(
			MakeWindowPathEditContext(windowState), selection,
			[&edit](PathStrokeStyle &style) {
				style.profile = edit.profile;
				style.width = edit.width;
				style.alpha = edit.alpha;
				style.color = edit.color;
				style.startDecoration.kind = edit.startDecoration;
				style.endDecoration.kind = edit.endDecoration;
				style.depthMode = edit.depthMode;
			});
		return report.applied.size();
	}

	bool RenameScenePath(RendererWindowState &windowState, const SceneObjectId id, std::string name)
	{
		if (windowState.paths == nullptr || windowState.paths->Store().Find(id) == nullptr)
			return false;
		// PathStore intentionally exposes only revision-bumping mutable access. Keep the edit on the
		// command path so rename still gets the shared undo boundary; the unavoidable style bump is
		// documented by SceneObjectPersistence.
		const PathEditReport report = ApplyPathEdit(
			MakeWindowPathEditContext(windowState), std::span<const SceneObjectId>(&id, 1),
			PathRevisionKind::Style, "Rename path", [&name](ScenePath &path) {
				path.name = name;
				return Result<void>{};
			});
		return report.AnyApplied();
	}

	std::string ScenePathDisplayName(const ScenePath &path, const std::size_t storeIndex)
	{
		return path.name.empty() ? "Path #" + std::to_string(storeIndex) : path.name;
	}

	bool DrawScenePathEditor(RendererWindowState &windowState)
	{
		const std::vector<SceneObjectId> selection = windowState.selectedScenePaths;
		const ScenePathStyleEditState resolved = ResolveScenePathStyleEdit(windowState, selection);
		if (resolved.resolved == 0)
			return false;
		ScenePathStyleEdit edit = resolved.values;
		bool changed = false;
		bool renamed = false;
		ImGui::Text("Paths (%zu selected)", resolved.resolved);
		if (selection.size() == 1 && windowState.paths != nullptr)
		{
			const ScenePath *path = windowState.paths->Store().Find(selection.front());
			if (path != nullptr)
			{
				char name[256]{};
				std::snprintf(name, sizeof(name), "%s", path->name.c_str());
				if (ImGui::InputText("Name", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue))
					renamed = RenameScenePath(windowState, path->id, name);
			}
		}
		changed = DrawEnumCombo("Profile", edit.profile) || changed;
		changed = ImGui::DragFloat("Width", &edit.width, 0.005f, 0.001f, 10.0f, "%.3f") || changed;
		changed = ImGui::SliderFloat("Alpha", &edit.alpha, 0.0f, 1.0f, "%.2f") || changed;
		changed = ImGui::ColorEdit3("Color", &edit.color.x) || changed;
		changed = DrawEnumCombo("Start decoration", edit.startDecoration) || changed;
		changed = DrawEnumCombo("End decoration", edit.endDecoration) || changed;
		changed = DrawEnumCombo("Depth", edit.depthMode) || changed;
		if (changed)
			return ApplyScenePathStyleEdit(windowState, selection, edit) != 0 || renamed;
		return renamed;
	}
} // namespace DefectStudio
