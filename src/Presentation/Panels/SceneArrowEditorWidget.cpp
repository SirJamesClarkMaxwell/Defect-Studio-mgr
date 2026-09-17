#include "Core/dspch.hpp"

#include "Presentation/Panels/SceneArrowEditorWidget.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include <imgui.h>

#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	// Fires once per logical edit (drag/type/pick), BEFORE the change reaches the model - see
	// drawUndoableFloat/Vec3 below. The no-undo `DrawSceneArrowEditor(SceneArrow&)` overload passes
	// an empty one; the indexed, windowState-aware overload passes a real snapshot call.
	using ArrowUndoFn = std::function<void()>;

	// Runs `widget` on a local copy so a snapshot fired via IsItemActivated always captures the true
	// pre-edit value (docs/scene_arrow_rework_plan_corrected.md Section 7 Step 3) - SliderFloat in
	// particular can jump straight to the clicked position on its own activation frame, so a
	// snapshot taken after a widget bound directly to the model field would already be too late.
	template <typename WidgetFn>
	static bool drawUndoableFloat(float &modelValue, const ArrowUndoFn &snapshot, WidgetFn &&widget)
	{
		float edited = modelValue;
		const bool changed = widget(edited);
		if (ImGui::IsItemActivated())
			snapshot();
		if (changed)
			modelValue = edited;
		return changed;
	}

	template <typename WidgetFn>
	static bool drawUndoableVec3(glm::vec3 &modelValue, const ArrowUndoFn &snapshot, WidgetFn &&widget)
	{
		glm::vec3 edited = modelValue;
		const bool changed = widget(edited);
		if (ImGui::IsItemActivated())
			snapshot();
		if (changed)
			modelValue = edited;
		return changed;
	}

	// Head width/length now apply to Arrow2D too (its SDF shader grew a real triangular head - see
	// OpenGlRendererBackend::renderSceneArrows), not just Arrow3D's cone; still hidden for Line,
	// which has no head geometry at all.
	// Arrow2D's shaftWidth/headWidth/headLength are screen-space pixels (typically single/low-double
	// digits); Line/Arrow3D's are world-space full diameters (typically a small fraction of a unit
	// cell) - same fields, unrelated numeric ranges, so the drag step/bounds/format branch on kind
	// rather than sharing one range that would be unusable for the other.
	static bool drawArrowGeometrySection(
		RendererWindowState::ArrowStyle &style, RendererWindowState::ArrowKind kind, const ArrowUndoFn &snapshot)
	{
		using ArrowKind = RendererWindowState::ArrowKind;
		const bool isPixelBased = kind == ArrowKind::Arrow2D;
		bool changed = false;

		ImGui::SetNextItemWidth(100.0f);
		if (isPixelBased)
		{
			changed |= drawUndoableFloat(style.shaftWidth, snapshot, [](float &v) {
				return ImGui::DragFloat("Shaft width (px)##ArrowStyleShaftWidth", &v, 0.1f, 0.5f, 64.0f, "%.1f");
			});
		}
		else
		{
			changed |= drawUndoableFloat(style.shaftWidth, snapshot, [](float &v) {
				return ImGui::DragFloat("Shaft width##ArrowStyleShaftWidth", &v, 0.002f, 0.005f, 1.0f, "%.3f");
			});
		}

		if (kind == ArrowKind::Arrow2D || kind == ArrowKind::Arrow3D)
		{
			ImGui::SetNextItemWidth(100.0f);
			if (isPixelBased)
			{
				changed |= drawUndoableFloat(style.headWidth, snapshot, [](float &v) {
					return ImGui::DragFloat("Head width (px)##ArrowStyleHeadWidth", &v, 0.2f, 1.0f, 128.0f, "%.1f");
				});
				ImGui::SameLine();
				ImGui::SetNextItemWidth(100.0f);
				changed |= drawUndoableFloat(style.headLength, snapshot, [](float &v) {
					return ImGui::DragFloat("Head length (px)##ArrowStyleHeadLength", &v, 0.2f, 1.0f, 200.0f, "%.1f");
				});
			}
			else
			{
				changed |= drawUndoableFloat(style.headWidth, snapshot, [](float &v) {
					return ImGui::DragFloat("Head width##ArrowStyleHeadWidth", &v, 0.002f, 0.01f, 1.0f, "%.3f");
				});
				ImGui::SameLine();
				ImGui::SetNextItemWidth(100.0f);
				changed |= drawUndoableFloat(style.headLength, snapshot, [](float &v) {
					return ImGui::DragFloat("Head length##ArrowStyleHeadLength", &v, 0.005f, 0.01f, 2.0f, "%.3f");
				});
			}
		}
		return changed;
	}

	// Outline is Arrow2D-only (no 3D outline pass exists) - hidden rather than shown disabled for
	// Line/Arrow3D, same "doesn't apply to this kind" reasoning the old single-function version used.
	static bool drawArrowAppearanceSection(
		RendererWindowState::ArrowStyle &style, RendererWindowState::ArrowKind kind, const ArrowUndoFn &snapshot)
	{
		bool changed = false;
		ImGui::TextUnformatted("Color");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		changed |= drawUndoableVec3(style.color, snapshot, [](glm::vec3 &v) {
			return ImGui::ColorEdit3("##ArrowStyleColor", &v.x, ImGuiColorEditFlags_NoInputs);
		});

		// The flat colour above stays visible and editable while the gradient is on, so switching
		// the gradient off gets the arrow back rather than leaving whatever it was before lost.
		bool useGradient = style.useGradient;
		if (ImGui::Checkbox("Gradient", &useGradient))
		{
			snapshot();
			style.useGradient = useGradient;
			changed = true;
		}
		if (style.useGradient)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.0f);
			changed |= drawUndoableVec3(style.gradient.start, snapshot, [](glm::vec3 &v) {
				return ImGui::ColorEdit3("##ArrowGradientStart", &v.x, ImGuiColorEditFlags_NoInputs);
			});
			ImGui::SameLine();
			ImGui::TextUnformatted("->");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.0f);
			changed |= drawUndoableVec3(style.gradient.finish, snapshot, [](glm::vec3 &v) {
				return ImGui::ColorEdit3("##ArrowGradientFinish", &v.x, ImGuiColorEditFlags_NoInputs);
			});
			if (kind == RendererWindowState::ArrowKind::Arrow2D)
				ImGui::TextDisabled("Arrow2D rysuje sie plaskim shaderem - gradient dziala na Linii i Strzalce 3D.");
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		changed |= drawUndoableFloat(style.alpha, snapshot, [](float &v) {
			return ImGui::SliderFloat("Opacity##ArrowStyleAlpha", &v, 0.0f, 1.0f, "%.2f");
		});

		if (kind != RendererWindowState::ArrowKind::Arrow2D)
			return changed;

		bool outlineEnabled = style.outlineWidth > 0.0f;
		if (ImGui::Checkbox("##ArrowStyleOutlineEnabled", &outlineEnabled))
		{
			snapshot();
			style.outlineWidth = outlineEnabled ? 1.25f : 0.0f;
			changed = true;
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Outline");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		changed |= drawUndoableVec3(style.outlineColor, snapshot, [](glm::vec3 &v) {
			return ImGui::ColorEdit3("##ArrowStyleOutlineColor", &v.x, ImGuiColorEditFlags_NoInputs);
		});
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!outlineEnabled);
		changed |= drawUndoableFloat(style.outlineWidth, snapshot, [](float &v) {
			return ImGui::DragFloat("Width##ArrowStyleOutlineWidth", &v, 0.05f, 0.1f, 8.0f, "%.2f");
		});
		ImGui::EndDisabled();
		return changed;
	}

	RendererWindowState::SceneArrow MakeDefaultSceneArrow(const glm::vec3 &seedPosition)
	{
		RendererWindowState::SceneArrow arrow;
		arrow.start = seedPosition;
		arrow.end = seedPosition + glm::vec3(0.0f, 0.0f, 1.0f);
		return arrow;
	}

	// Scene-relative length (docs/scene_arrow_rework_plan_corrected.md Section 7 Step 5, Section 8):
	// 20% of the structure's bounding diagonal, clamped to a sane on-screen range, so a fresh arrow
	// already reads as "an arrow" instead of a speck or a mile-long line. +X (not +Z) so a Billboard
	// 2D arrow - the new default kind - reads clearly against the app's default camera framing.
	RendererWindowState::SceneArrow MakeDefaultSceneArrow(
		const RendererWindowState &windowState, const glm::vec3 &seedPosition)
	{
		glm::vec3 minimum(std::numeric_limits<float>::max());
		glm::vec3 maximum(std::numeric_limits<float>::lowest());
		for (const RendererAtomData &atom : windowState.structure.atoms)
		{
			minimum = glm::min(minimum, atom.cartesianPosition);
			maximum = glm::max(maximum, atom.cartesianPosition);
		}
		const float diagonal = windowState.structure.atoms.empty() ? 0.0f : glm::length(maximum - minimum);
		const float length =
			std::isfinite(diagonal) && diagonal > 0.0f ? std::clamp(diagonal * 0.20f, 0.75f, 4.0f) : 1.0f;

		RendererWindowState::SceneArrow arrow;
		arrow.start = seedPosition;
		arrow.end = seedPosition + glm::vec3(length, 0.0f, 0.0f);
		arrow.kind = RendererWindowState::ArrowKind::Arrow2D;
		arrow.orientation2D = RendererWindowState::Arrow2DOrientation::Billboard;
		arrow.fixedPlane = RendererWindowState::WorldPlane::XY;
		arrow.style.color = glm::vec3(0.949f, 0.710f, 0.114f);
		arrow.style.alpha = 1.0f;
		arrow.style.shaftWidth = 8.0f;
		arrow.style.headWidth = 22.0f;
		arrow.style.headLength = 28.0f;
		arrow.style.outlineWidth = 1.25f;
		arrow.style.outlineColor = glm::vec3(0.06f, 0.055f, 0.05f);
		return arrow;
	}

	// Never reinterpret Arrow2D's pixel geometry as Line/Arrow3D's world-space geometry (or back) -
	// switching kind always re-derives geometry from this kind's own defaults, scaled by the arrow's
	// current length where that makes sense. Appearance (color/alpha/outlineColor) and start/end
	// survive untouched; orientation2D/fixedPlane are left as-is so returning to Arrow2D restores
	// whatever plane/orientation was last chosen.
	void ApplySceneArrowKindChange(
		RendererWindowState::SceneArrow &arrow,
		RendererWindowState::ArrowKind newKind,
		const RendererGlobalRenderSettings &globalSettings)
	{
		using ArrowKind = RendererWindowState::ArrowKind;
		if (arrow.kind == newKind)
			return;

		const float length = std::max(glm::length(arrow.end - arrow.start), 0.0001f);

		if (newKind == ArrowKind::Arrow2D)
		{
			arrow.style.shaftWidth = 8.0f;
			arrow.style.headWidth = 22.0f;
			arrow.style.headLength = 28.0f;
			arrow.style.outlineWidth = 1.25f;
		}
		else
		{
			// Line and Arrow3D share world-space full-diameter semantics - coming from the other
			// world-space kind, shaftWidth already means the right thing and is kept as-is. Ratios
			// (not fixed sizes) from Settings > Renderer > Scene arrows, so the new arrow's silhouette
			// stays consistent regardless of its length.
			if (arrow.kind == ArrowKind::Arrow2D)
			{
				arrow.style.shaftWidth =
					std::clamp(globalSettings.arrowDefaultShaftWidthRatio * length, 0.005f, 0.20f);
				arrow.style.outlineWidth = 0.0f;
			}
			if (newKind == ArrowKind::Arrow3D && arrow.kind != ArrowKind::Arrow3D)
			{
				arrow.style.headWidth =
					std::clamp(globalSettings.arrowDefaultHeadWidthRatio * length, 0.01f, 0.50f);
				arrow.style.headLength =
					std::clamp(globalSettings.arrowDefaultHeadLengthRatio * length, 0.02f, 0.70f);
			}
		}

		arrow.kind = newKind;
	}

	void DrawSelectedSceneArrowProperties(
		RendererWindowState &windowState, const RendererGlobalRenderSettings &globalSettings)
	{
		ImGui::Separator();
		ImGui::Text("Selected arrows");
		const std::size_t selectedCount = windowState.selectedSceneArrows.size();
		const std::size_t representativeIndex =
			AnnotationIndex(windowState.sceneArrows, windowState.selectedSceneArrows.front());
		if (representativeIndex >= windowState.sceneArrows.size())
			return;

		if (selectedCount == 1)
		{
			DrawSceneArrowAtomMatchActions(windowState, representativeIndex);
			DrawSceneArrowEditor(windowState, representativeIndex, SceneArrowEditorMode::Full, globalSettings);
			return;
		}

		ImGui::Text("%zu arrow(s) selected - style below applies to all of them", selectedCount);
		RendererWindowState::SceneArrow &representativeArrow = windowState.sceneArrows[representativeIndex];
		const ArrowUndoFn snapshot = [&windowState]() { PushPinnedMeasurementUndoSnapshot(windowState); };
		const bool geometryChanged =
			drawArrowGeometrySection(representativeArrow.style, representativeArrow.kind, snapshot);
		const bool appearanceChanged =
			drawArrowAppearanceSection(representativeArrow.style, representativeArrow.kind, snapshot);
		if (!geometryChanged && !appearanceChanged)
			return;
		for (std::size_t i = 1; i < selectedCount; ++i)
		{
			const std::size_t index = AnnotationIndex(windowState.sceneArrows, windowState.selectedSceneArrows[i]);
			if (index < windowState.sceneArrows.size())
				windowState.sceneArrows[index].style = representativeArrow.style;
		}
	}

	static void drawArrowPlacementSection(RendererWindowState::SceneArrow &arrow, const ArrowUndoFn &snapshot)
	{
		ImGui::TextUnformatted("Start");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200.0f);
		drawUndoableVec3(arrow.start, snapshot, [](glm::vec3 &v) { return ImGui::DragFloat3("##ArrowStart", &v.x, 0.01f, 0.0f, 0.0f, "%.3f"); });

		ImGui::TextUnformatted("End  ");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200.0f);
		drawUndoableVec3(arrow.end, snapshot, [](glm::vec3 &v) { return ImGui::DragFloat3("##ArrowEnd", &v.x, 0.01f, 0.0f, 0.0f, "%.3f"); });

		ImGui::Text("Length    %.3f", glm::length(arrow.end - arrow.start));
	}

	// Shared by Full's own "2D Orientation" section and Compact's "Advanced" section (doc Section 9
	// mockups put the same two combos in different places depending on mode) - one body, two hosts.
	static void drawArrow2DOrientationControls(RendererWindowState::SceneArrow &arrow, const ArrowUndoFn &snapshot)
	{
		const char *orientationItems[] = {"Billboard", "Fixed plane"};
		int orientationIndex = static_cast<int>(arrow.orientation2D);
		ImGui::SetNextItemWidth(140.0f);
		if (ImGui::Combo("##ArrowOrientationCombo", &orientationIndex, orientationItems, 2))
		{
			snapshot();
			arrow.orientation2D = static_cast<RendererWindowState::Arrow2DOrientation>(orientationIndex);
		}

		if (arrow.orientation2D == RendererWindowState::Arrow2DOrientation::FixedPlane)
		{
			const char *planeItems[] = {"XY", "XZ", "YZ"};
			int planeIndex = static_cast<int>(arrow.fixedPlane);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(80.0f);
			if (ImGui::Combo("Plane##ArrowPlaneCombo", &planeIndex, planeItems, 3))
			{
				snapshot();
				arrow.fixedPlane = static_cast<RendererWindowState::WorldPlane>(planeIndex);
			}
		}
	}

	static void drawArrowTypeSelector(
		RendererWindowState::SceneArrow &arrow,
		const ArrowUndoFn &snapshot,
		const RendererGlobalRenderSettings &globalSettings)
	{
		using ArrowKind = RendererWindowState::ArrowKind;
		struct Option
		{
			const char *label;
			ArrowKind kind;
		};
		constexpr Option options[3] = {
			{"Line", ArrowKind::Line}, {"2D Arrow", ArrowKind::Arrow2D}, {"3D Arrow", ArrowKind::Arrow3D}};
		for (int i = 0; i < 3; ++i)
		{
			if (i > 0)
				ImGui::SameLine();
			if (ImGui::RadioButton(options[i].label, arrow.kind == options[i].kind) && arrow.kind != options[i].kind)
			{
				snapshot();
				ApplySceneArrowKindChange(arrow, options[i].kind, globalSettings);
			}
		}
	}

	// Full = properties panel: Type -> Placement -> 2D Orientation -> Geometry -> Appearance, every
	// section a default-open CollapsingHeader. Compact = quick-edit popup: Type, then Geometry/
	// Appearance always visible (the two things worth touching right after placing an arrow),
	// Placement/Advanced tucked into collapsed headers below - see doc Section 9 for both mockups.
	static void drawSceneArrowEditorBody(
		RendererWindowState::SceneArrow &arrow,
		SceneArrowEditorMode mode,
		const ArrowUndoFn &snapshot,
		const RendererGlobalRenderSettings &globalSettings = {})
	{
		using ArrowKind = RendererWindowState::ArrowKind;
		constexpr ImGuiTreeNodeFlags kOpen = ImGuiTreeNodeFlags_DefaultOpen;

		ImGui::TextUnformatted("Type");
		ImGui::SameLine();
		drawArrowTypeSelector(arrow, snapshot, globalSettings);
		ImGui::Spacing();

		if (mode == SceneArrowEditorMode::Full)
		{
			if (ImGui::CollapsingHeader("Placement##ArrowPlacement", kOpen))
			{
				ImGui::Indent();
				drawArrowPlacementSection(arrow, snapshot);
				ImGui::Unindent();
			}
			if (arrow.kind == ArrowKind::Arrow2D && ImGui::CollapsingHeader("2D Orientation##ArrowOrientationHeader", kOpen))
			{
				ImGui::Indent();
				drawArrow2DOrientationControls(arrow, snapshot);
				ImGui::Unindent();
			}
			if (ImGui::CollapsingHeader("Geometry##ArrowGeometryHeader", kOpen))
			{
				ImGui::Indent();
				drawArrowGeometrySection(arrow.style, arrow.kind, snapshot);
				ImGui::Unindent();
			}
			if (ImGui::CollapsingHeader("Appearance##ArrowAppearanceHeader", kOpen))
			{
				ImGui::Indent();
				drawArrowAppearanceSection(arrow.style, arrow.kind, snapshot);
				ImGui::Unindent();
			}
		}
		else
		{
			ImGui::SeparatorText("Geometry");
			drawArrowGeometrySection(arrow.style, arrow.kind, snapshot);
			ImGui::SeparatorText("Appearance");
			drawArrowAppearanceSection(arrow.style, arrow.kind, snapshot);

			if (ImGui::CollapsingHeader("Placement##ArrowPlacement"))
			{
				ImGui::Indent();
				drawArrowPlacementSection(arrow, snapshot);
				ImGui::Unindent();
			}
			if (arrow.kind == ArrowKind::Arrow2D && ImGui::CollapsingHeader("Advanced##ArrowAdvanced"))
			{
				ImGui::Indent();
				drawArrow2DOrientationControls(arrow, snapshot);
				ImGui::Unindent();
			}
		}
	}

	void DrawSceneArrowEditor(RendererWindowState::SceneArrow &arrow)
	{
		static const ArrowUndoFn kNoUndo = []() {};
		drawSceneArrowEditorBody(arrow, SceneArrowEditorMode::Full, kNoUndo);
	}

	void DrawSceneArrowEditor(
		RendererWindowState &windowState,
		std::size_t arrowIndex,
		SceneArrowEditorMode mode,
		const RendererGlobalRenderSettings &globalSettings)
	{
		if (arrowIndex >= windowState.sceneArrows.size())
			return;
		const ArrowUndoFn snapshot = [&windowState]() { PushPinnedMeasurementUndoSnapshot(windowState); };
		drawSceneArrowEditorBody(windowState.sceneArrows[arrowIndex], mode, snapshot, globalSettings);
	}

} // namespace DefectStudio
