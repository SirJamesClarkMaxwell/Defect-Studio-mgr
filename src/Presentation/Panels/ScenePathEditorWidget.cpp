#include "Core/dspch.hpp"

#include "Presentation/Panels/ScenePathEditorWidget.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "Presentation/Panels/ScenePathGradientRamp.hpp"
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

		void ClampGradientStops(PathGradient &gradient)
		{
			float previous = 0.0f;
			for (PathGradientStop &stop : gradient.stops)
			{
				if (!std::isfinite(stop.position))
					stop.position = previous;
				stop.position = std::clamp(stop.position, previous, 1.0f);
				previous = stop.position;
			}
		}

		[[nodiscard]] bool SameGradient(const PathGradient &first, const PathGradient &value)
		{
			if (first.enabled != value.enabled || first.stops.size() != value.stops.size())
				return false;
			for (std::size_t index = 0; index < first.stops.size(); ++index)
			{
				const PathGradientStop &a = first.stops[index];
				const PathGradientStop &b = value.stops[index];
				if (a.position != b.position || a.color != b.color || a.alpha != b.alpha)
					return false;
			}
			return true;
		}

		void MarkDashMixed(bool &mixed, const PathDashStyle &first, const PathDashStyle &value)
		{
			mixed = mixed || first.enabled != value.enabled || first.dashLength != value.dashLength ||
				first.gapLength != value.gapLength || first.phase != value.phase;
		}

		[[nodiscard]] bool SameRatio(const float left, const float right, const float ratio)
		{
			if (!std::isfinite(left) || !std::isfinite(right) || left <= 0.0f || right <= 0.0f)
				return false;
			const float expected = right * ratio;
			const float tolerance = 1.0e-5f * std::max(1.0f, std::max(std::abs(left), std::abs(expected)));
			return std::abs(left - expected) <= tolerance;
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

		[[nodiscard]] bool DrawEnumCombo(const char *label, ScenePathLineStyle &value)
		{
			const char *names[] = {"Solid", "Dashed", "Dotted", "Custom"};
			int index = static_cast<int>(value);
			const bool changed = ImGui::Combo(label, &index, names, 4);
			if (changed)
				value = static_cast<ScenePathLineStyle>(index);
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
				state.values.ribbonBevel = path->style.ribbonBevel;
				state.values.ribbonBevelSegments = path->style.ribbonBevelSegments;
				state.values.ribbonBevelShape = path->style.ribbonBevelShape;
				state.anyFlatProfile = path->style.profile == StrokeProfile::Flat;
				state.values.width = path->style.width;
				state.values.alpha = path->style.alpha;
				state.values.color = path->style.color;
				state.values.dash = path->style.dash;
				state.values.gradient = path->style.gradient;
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
			MarkMixed(state.mixedRibbonBevel, state.values.ribbonBevel, path->style.ribbonBevel);
			MarkMixed(state.mixedRibbonBevelSegments, state.values.ribbonBevelSegments, path->style.ribbonBevelSegments);
			MarkMixed(state.mixedRibbonBevelShape, state.values.ribbonBevelShape, path->style.ribbonBevelShape);
			MarkMixed(state.mixedWidth, state.values.width, path->style.width);
			MarkMixed(state.mixedAlpha, state.values.alpha, path->style.alpha);
			MarkMixed(state.mixedColor, state.values.color, path->style.color);
			MarkDashMixed(state.mixedDash, state.values.dash, path->style.dash);
			state.mixedGradient = state.mixedGradient || !SameGradient(state.values.gradient, path->style.gradient);
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
				style.ribbonBevel = edit.ribbonBevel;
				style.ribbonBevelSegments = std::max(1u, edit.ribbonBevelSegments);
				style.ribbonBevelShape = std::clamp(edit.ribbonBevelShape, 0.0f, 1.0f);
				style.width = edit.width;
				style.alpha = edit.alpha;
				style.color = edit.color;
				style.dash = edit.dash;
				PathGradient gradient = edit.gradient;
				ClampGradientStops(gradient);
				if (gradient.stops.empty())
					gradient.enabled = false;
				style.gradient = std::move(gradient);
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

	ScenePathLineStyle ResolveScenePathLineStyle(const PathDashStyle &dash)
	{
		if (!dash.enabled)
			return ScenePathLineStyle::Solid;
		if (SameRatio(dash.dashLength, dash.gapLength, 2.0f))
			return ScenePathLineStyle::Dashed;
		if (SameRatio(dash.dashLength, dash.gapLength, 1.0f))
			return ScenePathLineStyle::Dotted;
		return ScenePathLineStyle::Custom;
	}

	void ApplyScenePathLineStyle(PathDashStyle &dash, const ScenePathLineStyle style, const float strokeWidth)
	{
		if (style == ScenePathLineStyle::Custom)
			return;
		if (style == ScenePathLineStyle::Solid)
		{
			dash.enabled = false;
			return;
		}
		const float width = std::max(strokeWidth, 0.001f);
		dash.enabled = true;
		if (style == ScenePathLineStyle::Dashed)
		{
			dash.dashLength = width * 4.0f;
			dash.gapLength = width * 2.0f;
		}
		else
		{
			dash.dashLength = width;
			dash.gapLength = width;
		}
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
		static int selectedGradientStop = -1;
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
			if (resolved.values.ribbonThickness > 0.0f || resolved.mixedRibbonThickness)
			{
				applyDrag(ImGui::DragFloat("Ribbon bevel", &edit.ribbonBevel, 0.005f, 0.0f, 10.0f, "%.3f"));
				ImGui::BeginDisabled(edit.ribbonBevel <= 0.0f);
				int bevelSegments = static_cast<int>(std::min(edit.ribbonBevelSegments, 256u));
				const bool segmentsChanged = ImGui::DragInt("Ribbon bevel segments", &bevelSegments, 1.0f, 1, 256);
				if (segmentsChanged)
					edit.ribbonBevelSegments = static_cast<std::uint32_t>(std::clamp(bevelSegments, 1, 256));
				applyDrag(segmentsChanged);
				edit.ribbonBevelShape = std::clamp(edit.ribbonBevelShape, 0.0f, 1.0f);
				applyDrag(ImGui::DragFloat("Ribbon bevel shape", &edit.ribbonBevelShape, 0.01f, 0.0f, 1.0f, "%.2f"));
				ImGui::EndDisabled();
			}
		}
		applyDrag(ImGui::DragFloat("Width", &edit.width, 0.005f, 0.001f, 10.0f, "%.3f"));
		applyDrag(ImGui::SliderFloat("Alpha", &edit.alpha, 0.0f, 1.0f, "%.2f"));
		applyDrag(ImGui::ColorEdit3("Color", &edit.color.x));
		ImGui::Separator();
		ImGui::Text("Line style");
		if (resolved.mixedDash)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(mixed)");
		}
		ScenePathLineStyle lineStyle = ResolveScenePathLineStyle(edit.dash);
		if (DrawEnumCombo("Line style", lineStyle))
		{
			ApplyScenePathLineStyle(edit.dash, lineStyle, edit.width);
			applyImmediate(true);
		}
		applyDrag(ImGui::DragFloat("Dash length", &edit.dash.dashLength, 0.01f, 0.001f, 10.0f, "%.3f"));
		applyDrag(ImGui::DragFloat("Gap length", &edit.dash.gapLength, 0.01f, 0.001f, 10.0f, "%.3f"));
		applyDrag(ImGui::DragFloat("Dash phase", &edit.dash.phase, 0.01f, -10.0f, 10.0f, "%.3f"));

		ImGui::Separator();
		ImGui::Text("Gradient");
		if (resolved.mixedGradient)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(mixed)");
		}
		bool gradientEnabled = edit.gradient.enabled;
		if (ImGui::Checkbox("Enabled##PathGradient", &gradientEnabled))
		{
			edit.gradient.enabled = gradientEnabled && !edit.gradient.stops.empty();
			applyImmediate(true);
		}
		const GradientRampResult ramp = DrawGradientRamp("PathGradientRamp", edit.gradient, selectedGradientStop);
		if (ramp.dragStarted)
			BeginScenePathStyleDrag(windowState);
		if (ramp.changed)
		{
			const bool recordUndo = !windowState.scenePathStyleEditBefore.has_value();
			changed = ApplyScenePathStyleEdit(windowState, selection, edit, recordUndo) != 0 || changed;
		}
		if (ramp.dragEnded)
			CommitScenePathStyleDrag(windowState);
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
