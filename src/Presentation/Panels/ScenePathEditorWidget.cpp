#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathEditorWidget.hpp"

#include <algorithm>
#include <cstdio>

#include <imgui.h>

#include "Presentation/Panels/ScenePathOperations.hpp"
#include "Renderer/Commands/SceneObjectsSnapshotCommand.hpp"
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

		void MarkDecorationMixed(bool &mixed, const PathEndpointDecoration &first,
			const PathEndpointDecoration &value)
		{
			mixed = mixed || first.kind != value.kind || first.lengthScale != value.lengthScale ||
				first.widthScale != value.widthScale || first.filled != value.filled;
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
			const char *names[] = {"None", "Arrow", "Stealth", "Latex", "Bar", "Circle", "Square", "Diamond", "Kite"};
			int index = static_cast<int>(value);
			const bool changed = ImGui::Combo(label, &index, names, 9);
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
				state.values.ribbonNormal = path->style.ribbonNormal;
				state.values.ribbonThickness = path->style.ribbonThickness;
				state.anyFlatProfile = path->style.profile == StrokeProfile::Flat;
				state.values.width = path->style.width;
				state.values.alpha = path->style.alpha;
				state.values.color = path->style.color;
				state.values.startDecoration = path->style.startDecoration;
				state.values.endDecoration = path->style.endDecoration;
				state.values.depthMode = path->style.depthMode;
				state.resolved = 1;
				continue;
			}
			++state.resolved;
			state.anyFlatProfile = state.anyFlatProfile || path->style.profile == StrokeProfile::Flat;
			MarkMixed(state.mixedProfile, state.values.profile, path->style.profile);
			MarkMixed(state.mixedRibbonNormal, state.values.ribbonNormal, path->style.ribbonNormal);
			MarkMixed(state.mixedRibbonThickness, state.values.ribbonThickness, path->style.ribbonThickness);
			MarkMixed(state.mixedWidth, state.values.width, path->style.width);
			MarkMixed(state.mixedAlpha, state.values.alpha, path->style.alpha);
			MarkMixed(state.mixedColor, state.values.color, path->style.color);
			MarkDecorationMixed(state.mixedStartDecoration, state.values.startDecoration, path->style.startDecoration);
			MarkDecorationMixed(state.mixedEndDecoration, state.values.endDecoration, path->style.endDecoration);
			MarkMixed(state.mixedDepthMode, state.values.depthMode, path->style.depthMode);
		}
		return state;
	}

	std::size_t ApplyScenePathStyleEdit(
		RendererWindowState &windowState, const std::vector<SceneObjectId> &selection,
		const ScenePathStyleEdit &edit, const bool recordUndo)
	{
		if (windowState.paths == nullptr || selection.empty())
			return 0;
		const PathEditContext context = recordUndo
			? MakeWindowPathEditContext(windowState)
			: MakeSilentPathEditContext(windowState);
		const PathEditReport report = SetScenePathStyle(
			context, selection,
			[&edit](PathStrokeStyle &style) {
				style.profile = edit.profile;
				style.ribbonNormal = edit.ribbonNormal;
				style.ribbonThickness = edit.ribbonThickness;
				style.width = edit.width;
				style.alpha = edit.alpha;
				style.color = edit.color;
				style.startDecoration = edit.startDecoration;
				style.endDecoration = edit.endDecoration;
				style.depthMode = edit.depthMode;
			});
		return report.applied.size();
	}

	void BeginScenePathStyleDrag(RendererWindowState &windowState)
	{
		if (windowState.scenePathStyleEditBefore.has_value())
			return;
		windowState.scenePathStyleEditBefore = CaptureSceneObjectsSnapshot(windowState);
	}

	bool CommitScenePathStyleDrag(RendererWindowState &windowState)
	{
		if (!windowState.scenePathStyleEditBefore.has_value())
			return false;

		RendererWindowState::LabelUndoSnapshot before =
			std::move(*windowState.scenePathStyleEditBefore);
		windowState.scenePathStyleEditBefore.reset();

		const PathStore *liveStore = windowState.paths != nullptr ? &windowState.paths->Store() : nullptr;
		const std::vector<SceneObjectId> beforeIds = before.paths.Ids();
		const bool changed = liveStore == nullptr
			? !beforeIds.empty()
			: beforeIds != liveStore->Ids() || std::any_of(
				beforeIds.begin(), beforeIds.end(), [&](const SceneObjectId id) {
					return before.paths.RevisionsFor(id) != liveStore->RevisionsFor(id);
				});
		if (!changed)
			return false;

		PushSceneObjectsUndoSnapshot(windowState, std::move(before));
		return true;
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
		const auto applyImmediate = [&](const bool controlChanged) {
			if (!controlChanged)
				return;
			changed = ApplyScenePathStyleEdit(windowState, selection, edit, true) != 0 || changed;
		};
		const auto applyDrag = [&](const bool controlChanged) {
			if (ImGui::IsItemActivated())
				BeginScenePathStyleDrag(windowState);
			if (controlChanged)
			{
				const bool recordUndo = !windowState.scenePathStyleEditBefore.has_value();
				changed = ApplyScenePathStyleEdit(windowState, selection, edit, recordUndo) != 0 || changed;
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
				CommitScenePathStyleDrag(windowState);
		};
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
		applyImmediate(DrawEnumCombo("Profile", edit.profile));
		if (resolved.values.profile == StrokeProfile::Flat || (resolved.mixedProfile && resolved.anyFlatProfile))
		{
			applyDrag(ImGui::DragFloat3("Ribbon normal", &edit.ribbonNormal.x, 0.01f));
			applyDrag(ImGui::DragFloat("Ribbon thickness", &edit.ribbonThickness, 0.005f, 0.0f, 10.0f, "%.3f"));
		}
		applyDrag(ImGui::DragFloat("Width", &edit.width, 0.005f, 0.001f, 10.0f, "%.3f"));
		applyDrag(ImGui::SliderFloat("Alpha", &edit.alpha, 0.0f, 1.0f, "%.2f"));
		applyDrag(ImGui::ColorEdit3("Color", &edit.color.x));
		const auto drawDecoration = [&](const char *prefix, PathEndpointDecoration &decoration) {
			const std::string kindLabel = std::string(prefix) + " kind";
			const std::string lengthLabel = std::string(prefix) + " length scale";
			const std::string widthLabel = std::string(prefix) + " width scale";
			const std::string filledLabel = std::string(prefix) + " filled";
			applyImmediate(DrawEnumCombo(kindLabel.c_str(), decoration.kind));
			applyDrag(ImGui::DragFloat(lengthLabel.c_str(), &decoration.lengthScale, 0.01f, 0.01f, 10.0f, "%.2f"));
			applyDrag(ImGui::DragFloat(widthLabel.c_str(), &decoration.widthScale, 0.01f, 0.01f, 10.0f, "%.2f"));
			applyImmediate(ImGui::Checkbox(filledLabel.c_str(), &decoration.filled));
		};
		drawDecoration("Start decoration", edit.startDecoration);
		drawDecoration("End decoration", edit.endDecoration);
		applyImmediate(DrawEnumCombo("Depth", edit.depthMode));
		return changed || renamed;
	}
} // namespace DefectStudio
