#include "Core/dspch.hpp"

#include "Presentation/Panels/ObjectPropertiesPanel.hpp"

#include "Presentation/Panels/SceneArrowEditorWidget.hpp"

#include <cmath>
#include <cstdio>
#include <limits>

#include <imgui.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Domain/Crystal/CrystalStructure.hpp"
#include "Domain/DomainLayer.hpp"
#include "Domain/ProjectWorkspace.hpp"
#include "Renderer/Commands/RendererAtomEditCommands.hpp"
#include "Renderer/RendererLayer.hpp"

namespace DefectStudio
{
	// Shared by every label kind (free labels, pinned bond/angle labels) - one editor for
	// RendererWindowState::LabelStyle instead of a separate control block per label kind. Outline/
	// background rows read "0 = off" like the shader they feed (labels.frag/label_background.frag).
	static void drawLabelStyleEditor(RendererWindowState::LabelStyle &style)
	{
		ImGui::TextUnformatted("Text");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleTextColor", &style.textColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::SliderFloat("Alpha##StyleTextAlpha", &style.textAlpha, 0.0f, 1.0f, "%.2f");

		// Checkbox is a thin view over backgroundAlpha/outlineWidth themselves (0 = off, same meaning
		// the shader already gives that value) rather than a separate enabled flag - one source of
		// truth. Toggling on restores a sensible default rather than 0, since the slider/drag below
		// would otherwise show "on" at a still-invisible value.
		bool backgroundEnabled = style.backgroundAlpha > 0.0f;
		if (ImGui::Checkbox("##StyleBackgroundEnabled", &backgroundEnabled))
			style.backgroundAlpha = backgroundEnabled ? 0.85f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Background");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::BeginDisabled(!backgroundEnabled);
		ImGui::ColorEdit3("##StyleBackgroundColor", &style.backgroundColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::SliderFloat("Alpha##StyleBackgroundAlpha", &style.backgroundAlpha, 0.01f, 1.0f, "%.2f");
		ImGui::EndDisabled();

		bool borderEnabled = style.outlineWidth > 0.0f;
		if (ImGui::Checkbox("##StyleBorderEnabled", &borderEnabled))
			style.outlineWidth = borderEnabled ? 0.02f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Border");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleOutlineColor", &style.outlineColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!borderEnabled);
		ImGui::DragFloat("Width##StyleOutlineWidth", &style.outlineWidth, 0.002f, 0.001f, 0.2f, "%.3f");
		ImGui::EndDisabled();

		// Glyph stroke (labels.frag), independent of the Border row above which only frames the
		// background quad. Screen pixels, not world units and not normalized SDF units - stays a
		// constant on-screen thickness regardless of zoom, unlike Border's 0.001-0.2 world-unit range.
		bool strokeEnabled = style.strokeWidth > 0.0f;
		if (ImGui::Checkbox("##StyleStrokeEnabled", &strokeEnabled))
			style.strokeWidth = strokeEnabled ? 2.0f : 0.0f;
		ImGui::SameLine();
		ImGui::TextUnformatted("Stroke");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		ImGui::ColorEdit3("##StyleStrokeColor", &style.strokeColor.x, ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!strokeEnabled);
		ImGui::DragFloat("Width (px)##StyleStrokeWidth", &style.strokeWidth, 0.05f, 0.1f, 8.0f, "%.2f");
		ImGui::EndDisabled();

		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("Corner radius##StyleCornerRadius", &style.cornerRadius, 0.005f, 0.0f, 0.3f, "%.3f");

		ImGui::SetNextItemWidth(140.0f);
		ImGui::DragFloat2("Padding X/Y##StylePadding", &style.padding.x, 0.005f, 0.0f, 1.0f, "%.3f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::DragFloat("Scale##StyleScale", &style.scale, 0.02f, 0.1f, 8.0f, "%.2f");
	}

	// Fires once per logical edit (drag/type/pick), BEFORE the change reaches the model - see
	// drawUndoableFloat/Vec3 below. The no-undo `DrawSceneArrowEditor(SceneArrow&)` overload passes
	// an empty one; the indexed, windowState-aware overload passes a real snapshot call.
	using ArrowUndoFn = std::function<void()>;

	// Runs `widget` on a local copy so a snapshot fired via IsItemActivated always captures the true
	// pre-edit value (docs/scene_arrow_rework_plan_corrected.md Section 7 Step 3) - SliderFloat in
	// particular can jump straight to the clicked position on its own activation frame, so a
	// snapshot taken after a widget bound directly to the model field would already be too late.
	template <typename WidgetFn>
	static void drawUndoableFloat(float &modelValue, const ArrowUndoFn &snapshot, WidgetFn &&widget)
	{
		float edited = modelValue;
		const bool changed = widget(edited);
		if (ImGui::IsItemActivated())
			snapshot();
		if (changed)
			modelValue = edited;
	}

	template <typename WidgetFn>
	static void drawUndoableVec3(glm::vec3 &modelValue, const ArrowUndoFn &snapshot, WidgetFn &&widget)
	{
		glm::vec3 edited = modelValue;
		const bool changed = widget(edited);
		if (ImGui::IsItemActivated())
			snapshot();
		if (changed)
			modelValue = edited;
	}

	// Head width/length now apply to Arrow2D too (its SDF shader grew a real triangular head - see
	// OpenGlRendererBackend::renderSceneArrows), not just Arrow3D's cone; still hidden for Line,
	// which has no head geometry at all.
	// Arrow2D's shaftWidth/headWidth/headLength are screen-space pixels (typically single/low-double
	// digits); Line/Arrow3D's are world-space full diameters (typically a small fraction of a unit
	// cell) - same fields, unrelated numeric ranges, so the drag step/bounds/format branch on kind
	// rather than sharing one range that would be unusable for the other.
	static void drawArrowGeometrySection(
		RendererWindowState::ArrowStyle &style, RendererWindowState::ArrowKind kind, const ArrowUndoFn &snapshot)
	{
		using ArrowKind = RendererWindowState::ArrowKind;
		const bool isPixelBased = kind == ArrowKind::Arrow2D;

		ImGui::SetNextItemWidth(100.0f);
		if (isPixelBased)
		{
			drawUndoableFloat(style.shaftWidth, snapshot, [](float &v) {
				return ImGui::DragFloat("Shaft width (px)##ArrowStyleShaftWidth", &v, 0.1f, 0.5f, 64.0f, "%.1f");
			});
		}
		else
		{
			drawUndoableFloat(style.shaftWidth, snapshot, [](float &v) {
				return ImGui::DragFloat("Shaft width##ArrowStyleShaftWidth", &v, 0.002f, 0.005f, 1.0f, "%.3f");
			});
		}

		if (kind == ArrowKind::Arrow2D || kind == ArrowKind::Arrow3D)
		{
			ImGui::SetNextItemWidth(100.0f);
			if (isPixelBased)
			{
				drawUndoableFloat(style.headWidth, snapshot, [](float &v) {
					return ImGui::DragFloat("Head width (px)##ArrowStyleHeadWidth", &v, 0.2f, 1.0f, 128.0f, "%.1f");
				});
				ImGui::SameLine();
				ImGui::SetNextItemWidth(100.0f);
				drawUndoableFloat(style.headLength, snapshot, [](float &v) {
					return ImGui::DragFloat("Head length (px)##ArrowStyleHeadLength", &v, 0.2f, 1.0f, 200.0f, "%.1f");
				});
			}
			else
			{
				drawUndoableFloat(style.headWidth, snapshot, [](float &v) {
					return ImGui::DragFloat("Head width##ArrowStyleHeadWidth", &v, 0.002f, 0.01f, 1.0f, "%.3f");
				});
				ImGui::SameLine();
				ImGui::SetNextItemWidth(100.0f);
				drawUndoableFloat(style.headLength, snapshot, [](float &v) {
					return ImGui::DragFloat("Head length##ArrowStyleHeadLength", &v, 0.005f, 0.01f, 2.0f, "%.3f");
				});
			}
		}
	}

	// Outline is Arrow2D-only (no 3D outline pass exists) - hidden rather than shown disabled for
	// Line/Arrow3D, same "doesn't apply to this kind" reasoning the old single-function version used.
	static void drawArrowAppearanceSection(
		RendererWindowState::ArrowStyle &style, RendererWindowState::ArrowKind kind, const ArrowUndoFn &snapshot)
	{
		ImGui::TextUnformatted("Color");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		drawUndoableVec3(style.color, snapshot, [](glm::vec3 &v) {
			return ImGui::ColorEdit3("##ArrowStyleColor", &v.x, ImGuiColorEditFlags_NoInputs);
		});
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		drawUndoableFloat(style.alpha, snapshot, [](float &v) {
			return ImGui::SliderFloat("Opacity##ArrowStyleAlpha", &v, 0.0f, 1.0f, "%.2f");
		});

		if (kind != RendererWindowState::ArrowKind::Arrow2D)
			return;

		bool outlineEnabled = style.outlineWidth > 0.0f;
		if (ImGui::Checkbox("##ArrowStyleOutlineEnabled", &outlineEnabled))
		{
			snapshot();
			style.outlineWidth = outlineEnabled ? 1.25f : 0.0f;
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("Outline");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(140.0f);
		drawUndoableVec3(style.outlineColor, snapshot, [](glm::vec3 &v) {
			return ImGui::ColorEdit3("##ArrowStyleOutlineColor", &v.x, ImGuiColorEditFlags_NoInputs);
		});
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::BeginDisabled(!outlineEnabled);
		drawUndoableFloat(style.outlineWidth, snapshot, [](float &v) {
			return ImGui::DragFloat("Width##ArrowStyleOutlineWidth", &v, 0.05f, 0.1f, 8.0f, "%.2f");
		});
		ImGui::EndDisabled();
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

	void EraseSceneArrows(RendererWindowState &windowState, std::vector<std::size_t> indices)
	{
		std::sort(indices.begin(), indices.end(), std::greater<>());
		indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

		bool erasedAny = false;
		for (const std::size_t index : indices)
		{
			if (index >= windowState.sceneArrows.size())
				continue;
			windowState.sceneArrows.erase(windowState.sceneArrows.begin() + static_cast<std::ptrdiff_t>(index));
			erasedAny = true;
		}

		windowState.selectedSceneArrows.clear();
		windowState.sceneArrowDragging = false;
		if (erasedAny)
			windowState.sceneArrowQuickEditActive = false;
	}

	std::vector<RendererWindowState::SceneArrow> &GetSceneArrowClipboard()
	{
		static std::vector<RendererWindowState::SceneArrow> clipboard;
		return clipboard;
	}

	void CopySceneArrowsToClipboard(const RendererWindowState &windowState)
	{
		std::vector<RendererWindowState::SceneArrow> &clipboard = GetSceneArrowClipboard();
		clipboard.clear();
		for (const std::size_t index : windowState.selectedSceneArrows)
			if (index < windowState.sceneArrows.size())
				clipboard.push_back(windowState.sceneArrows[index]);
	}

	// Flat world-space offset so the copy doesn't land exactly on top of the original - same constant
	// (and same non-cleverness) as RendererAtomEditCommands.cpp's kDuplicateOffset/kPasteOffset.
	static constexpr glm::vec3 kArrowDuplicateOffset(0.5f, 0.0f, 0.0f);

	void DuplicateSelectedSceneArrows(RendererWindowState &windowState)
	{
		if (windowState.selectedSceneArrows.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);

		std::vector<std::size_t> newIndices;
		newIndices.reserve(windowState.selectedSceneArrows.size());
		for (const std::size_t index : windowState.selectedSceneArrows)
		{
			if (index >= windowState.sceneArrows.size())
				continue;
			RendererWindowState::SceneArrow copy = windowState.sceneArrows[index];
			copy.start += kArrowDuplicateOffset;
			copy.end += kArrowDuplicateOffset;
			windowState.sceneArrows.push_back(std::move(copy));
			newIndices.push_back(windowState.sceneArrows.size() - 1);
		}
		windowState.selectedSceneArrows = std::move(newIndices);
	}

	void PasteSceneArrowsFromClipboard(RendererWindowState &windowState)
	{
		const std::vector<RendererWindowState::SceneArrow> &clipboard = GetSceneArrowClipboard();
		if (clipboard.empty())
			return;
		PushPinnedMeasurementUndoSnapshot(windowState);

		std::vector<std::size_t> newIndices;
		newIndices.reserve(clipboard.size());
		for (const RendererWindowState::SceneArrow &arrow : clipboard)
		{
			RendererWindowState::SceneArrow copy = arrow;
			copy.start += kArrowDuplicateOffset;
			copy.end += kArrowDuplicateOffset;
			windowState.sceneArrows.push_back(std::move(copy));
			newIndices.push_back(windowState.sceneArrows.size() - 1);
		}
		windowState.selectedSceneArrows = std::move(newIndices);
	}

	std::optional<RendererWindowState::ArrowStyle> &GetArrowGeometryClipboard()
	{
		static std::optional<RendererWindowState::ArrowStyle> clipboard;
		return clipboard;
	}

	std::optional<RendererWindowState::ArrowStyle> &GetArrowStyleClipboard()
	{
		static std::optional<RendererWindowState::ArrowStyle> clipboard;
		return clipboard;
	}

	void CopyArrowGeometry(const RendererWindowState::ArrowStyle &style)
	{
		GetArrowGeometryClipboard() = style;
	}

	void CopyArrowStyle(const RendererWindowState::ArrowStyle &style)
	{
		GetArrowStyleClipboard() = style;
	}

	bool PasteArrowGeometry(RendererWindowState &windowState, const std::vector<std::size_t> &targets)
	{
		const std::optional<RendererWindowState::ArrowStyle> &clipboard = GetArrowGeometryClipboard();
		if (!clipboard.has_value() || targets.empty())
			return false;
		for (const std::size_t index : targets)
		{
			if (index >= windowState.sceneArrows.size())
				continue;
			RendererWindowState::ArrowStyle &style = windowState.sceneArrows[index].style;
			style.shaftWidth = clipboard->shaftWidth;
			style.headWidth = clipboard->headWidth;
			style.headLength = clipboard->headLength;
			style.outlineWidth = clipboard->outlineWidth;
		}
		return true;
	}

	bool PasteArrowStyle(RendererWindowState &windowState, const std::vector<std::size_t> &targets)
	{
		const std::optional<RendererWindowState::ArrowStyle> &clipboard = GetArrowStyleClipboard();
		if (!clipboard.has_value() || targets.empty())
			return false;
		for (const std::size_t index : targets)
		{
			if (index >= windowState.sceneArrows.size())
				continue;
			RendererWindowState::ArrowStyle &style = windowState.sceneArrows[index].style;
			style.color = clipboard->color;
			style.alpha = clipboard->alpha;
			style.outlineColor = clipboard->outlineColor;
		}
		return true;
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

	ObjectPropertiesPanel::ObjectPropertiesPanel(
		RendererLayer &layer,
		WeakRef<CommandRegistry> commandRegistry,
		WeakRef<DomainLayer> domainLayer,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Layer(layer),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_DomainLayer(std::move(domainLayer))
	{
	}

	Ref<IPanel> ObjectPropertiesPanel::Clone() const
	{
		return CreateRef<ObjectPropertiesPanel>(*this);
	}

	void ObjectPropertiesPanel::Render()
	{
		if (!IsVisible())
			return;

		bool windowOpen = true;
		if (!ImGui::Begin(GetTitle().c_str(), &windowOpen))
		{
			ImGui::End();
			SetVisible(windowOpen);
			return;
		}

		// GetLastFocusedViewportWindowId, not GetFocusedViewportWindowId - the latter clears the
		// instant ImGui focus leaves the viewport (it's meant for camera-input gating), which is
		// exactly what happens the moment this panel's own fields are clicked to edit them. Using it
		// here made every field un-editable: clicking into any InputFloat/InputText immediately
		// dropped the "no viewport focused" message before the click could even register on the
		// widget. Same fix ElectronicStructureSession.cpp already applies for the same reason.
		const std::string &focusedWindowId = m_Layer.GetLastFocusedViewportWindowId();
		RendererWindowState *windowState = nullptr;
		if (!focusedWindowId.empty())
		{
			for (RendererWindowState &candidate : m_Layer.GetWindows())
			{
				if (candidate.windowId == focusedWindowId)
				{
					windowState = &candidate;
					break;
				}
			}
		}

		if (windowState == nullptr)
		{
			ImGui::TextDisabled("No renderer viewport focused.");
		}
		else if (windowState->selectedAtomIndices.size() != 1)
		{
			ImGui::TextDisabled("Select exactly 1 atom to edit its properties.");
		}
		else
		{
			const std::size_t atomIndex = windowState->selectedAtomIndices.front();
			if (atomIndex >= windowState->structure.atoms.size())
			{
				ImGui::TextDisabled("Selected atom is no longer valid.");
			}
			else
			{
				RendererAtomData &atom = windowState->structure.atoms[atomIndex];
				Ref<CommandRegistry> commandRegistry = m_CommandRegistry.lock();
				Ref<DomainLayer> domainLayer = m_DomainLayer.lock();

				// Domain-only fields (label/charge/magnetization/occupancy/selective dynamics) have no
				// renderer-side representation at all - resolved straight from the live domain
				// structure rather than RendererAtomData, same lookup every other atom-edit command
				// uses to find it. Position/element stay renderer-first since they're already mirrored
				// there and their commit commands (gizmo transform / change type) expect that.
				Ref<StructureRecord> domainRecord;
				if (domainLayer != nullptr)
				{
					Result<AtomEditTarget> target = ResolveAtomEditTarget(m_Layer, *domainLayer, windowState->windowId);
					if (target)
						domainRecord = target->record;
				}
				const AtomSite *domainAtom = (domainRecord != nullptr && atomIndex < domainRecord->structure.atoms.size())
					? &domainRecord->structure.atoms[atomIndex]
					: nullptr;

				ImGui::Text("Atom #%zu", atomIndex);
				ImGui::Separator();
				ImGui::PushItemWidth(140.0f);

				// Element - reuses "renderer.selection.change_type", the same command the viewport
				// context menu drives, so this stays a single undo step regardless of entry point.
				ImGui::TextUnformatted("Element");
				ImGui::SameLine();
				char speciesBuffer[16];
				std::snprintf(speciesBuffer, sizeof(speciesBuffer), "%s", atom.element.c_str());
				ImGui::InputText("##ElementText", speciesBuffer, sizeof(speciesBuffer));
				if (ImGui::IsItemDeactivatedAfterEdit() && commandRegistry != nullptr && speciesBuffer[0] != '\0')
				{
					ChangeAtomTypePayload payload;
					payload.windowId = windowState->windowId;
					payload.species = speciesBuffer;
					CommandContext context;
					context.Set<ChangeAtomTypePayload>("atom_edit.change_type_payload", std::move(payload));
					Result<CommandOutcome> result =
						commandRegistry->Execute(CommandID{"renderer.selection.change_type"}, std::move(context));
					if (!result)
						DS_LOG_WARN("Set atom element failed: {}", result.Error().technicalDetails);
				}
				ImGui::SameLine();
				if (ImGui::Button("Choose..."))
				{
					// Seeds the shared Periodic Table window with this atom's current element and asks
					// it to apply the pick back to the selection (rather than just close) once
					// confirmed - see drawPeriodicTableWindow's GetPeriodicTableApplyOnConfirm comment.
					m_Layer.GetSelectedPeriodicElement() = atom.element;
					m_Layer.GetShowPeriodicTableWindow() = true;
					m_Layer.GetPeriodicTableApplyOnConfirm() = true;
				}

				ImGui::Separator();
				ImGui::Text("Position");

				glm::vec3 cartesian = atom.cartesianPosition;
				bool cartesianCommitted = false;
				ImGui::PushItemWidth(110.0f);
				ImGui::BeginGroup();
				ImGui::Text("Cartesian (A)");
				ImGui::InputFloat("X##cart", &cartesian.x, 0.0f, 0.0f, "%.4f");
				cartesianCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::InputFloat("Y##cart", &cartesian.y, 0.0f, 0.0f, "%.4f");
				cartesianCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::InputFloat("Z##cart", &cartesian.z, 0.0f, 0.0f, "%.4f");
				cartesianCommitted |= ImGui::IsItemDeactivatedAfterEdit();
				ImGui::EndGroup();

				bool fractionalCommitted = false;
				glm::vec3 fractional(0.0f);
				if (domainRecord != nullptr)
				{
					fractional = domainRecord->structure.CartesianToFractional(atom.cartesianPosition);
					ImGui::SameLine();
					ImGui::BeginGroup();
					ImGui::Text("Fractional");
					ImGui::InputFloat("X##frac", &fractional.x, 0.0f, 0.0f, "%.4f");
					fractionalCommitted |= ImGui::IsItemDeactivatedAfterEdit();
					ImGui::InputFloat("Y##frac", &fractional.y, 0.0f, 0.0f, "%.4f");
					fractionalCommitted |= ImGui::IsItemDeactivatedAfterEdit();
					ImGui::InputFloat("Z##frac", &fractional.z, 0.0f, 0.0f, "%.4f");
					fractionalCommitted |= ImGui::IsItemDeactivatedAfterEdit();
					ImGui::EndGroup();
				}
				ImGui::PopItemWidth();

				if ((cartesianCommitted || fractionalCommitted) && commandRegistry != nullptr)
				{
					const glm::vec3 newPosition = fractionalCommitted && domainRecord != nullptr
						? domainRecord->structure.FractionalToCartesian(fractional)
						: cartesian;

					GizmoTransformPayload payload;
					payload.windowId = windowState->windowId;
					payload.atomIndices = {atomIndex};
					payload.afterPositions = {newPosition};
					payload.description = "Set atom position";

					CommandContext context;
					context.Set<GizmoTransformPayload>("gizmo.transform_payload", std::move(payload));
					Result<CommandOutcome> result =
						commandRegistry->Execute(CommandID{"renderer.gizmo.commit_transform"}, std::move(context));
					if (!result)
						DS_LOG_WARN("Set atom position failed: {}", result.Error().technicalDetails);
				}

				if (domainAtom != nullptr)
				{
					ImGui::Separator();
					ImGui::Text("Other properties");

					// Every commit below sends the *whole* current set of these fields, not a partial
					// patch - AtomPropertiesPayload has no "which fields changed" flag, so any field
					// left at its struct default would silently blast away the others' live values.
					auto commitProperties = [&](const AtomSite &edited)
					{
						if (commandRegistry == nullptr)
							return;
						AtomPropertiesPayload payload;
						payload.windowId = windowState->windowId;
						payload.atomIndex = atomIndex;
						payload.label = edited.label;
						payload.charge = edited.charge;
						payload.magnetization = edited.magnetization;
						payload.occupancy = edited.occupancy;
						payload.hasSelectiveDynamics = edited.hasSelectiveDynamics;
						payload.selectiveDynamics = edited.selectiveDynamics;

						CommandContext context;
						context.Set<AtomPropertiesPayload>("atom_edit.set_properties_payload", std::move(payload));
						Result<CommandOutcome> result = commandRegistry->Execute(
							CommandID{"renderer.selection.set_atom_properties"}, std::move(context));
						if (!result)
							DS_LOG_WARN("Set atom properties failed: {}", result.Error().technicalDetails);
					};

					AtomSite edited = *domainAtom;

					char labelBuffer[64];
					std::snprintf(labelBuffer, sizeof(labelBuffer), "%s", domainAtom->label.c_str());
					ImGui::InputText("Label", labelBuffer, sizeof(labelBuffer));
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						edited.label = labelBuffer;
						commitProperties(edited);
					}

					float occupancy = domainAtom->occupancy;
					ImGui::DragFloat("Occupancy", &occupancy, 0.01f, 0.0f, 1.0f, "%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						edited.occupancy = occupancy;
						commitProperties(edited);
					}

					float charge = domainAtom->charge;
					ImGui::InputFloat("Charge", &charge, 0.0f, 0.0f, "%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						edited.charge = charge;
						commitProperties(edited);
					}

					float magnetization = domainAtom->magnetization;
					ImGui::InputFloat("Magnetization", &magnetization, 0.0f, 0.0f, "%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						edited.magnetization = magnetization;
						commitProperties(edited);
					}

					bool hasSelectiveDynamics = domainAtom->hasSelectiveDynamics;
					if (ImGui::Checkbox("Selective dynamics", &hasSelectiveDynamics))
					{
						edited.hasSelectiveDynamics = hasSelectiveDynamics;
						commitProperties(edited);
					}
					if (domainAtom->hasSelectiveDynamics)
					{
						std::array<bool, 3> selectiveDynamics = domainAtom->selectiveDynamics;
						bool selectiveDynamicsChanged = false;
						selectiveDynamicsChanged |= ImGui::Checkbox("X##selDyn", &selectiveDynamics[0]);
						ImGui::SameLine();
						selectiveDynamicsChanged |= ImGui::Checkbox("Y##selDyn", &selectiveDynamics[1]);
						ImGui::SameLine();
						selectiveDynamicsChanged |= ImGui::Checkbox("Z##selDyn", &selectiveDynamics[2]);
						if (selectiveDynamicsChanged)
						{
							edited.selectiveDynamics = selectiveDynamics;
							commitProperties(edited);
						}
					}
				}

				ImGui::PopItemWidth();
			}
		}

		// Independent of the atom-selection branch above (0/1/many atoms selected doesn't matter
		// here) - free labels are scene-wide annotations, not a per-atom property. Reposition via
		// typed X/Y/Z here, or click-drag in the viewport (RendererPanel::handleFreeLabelInteraction).
		if (windowState != nullptr)
		{
			ImGui::Separator();
			ImGui::Text("Free labels");
			if (ImGui::Button("+ Add label"))
			{
				PushPinnedMeasurementUndoSnapshot(*windowState);
				RendererWindowState::FreeLabel label;
				label.worldPosition = windowState->cursor3DPlaced ? windowState->cursor3DPosition : glm::vec3(0.0f);
				windowState->freeLabels.push_back(std::move(label));
			}
			int labelToRemove = -1;
			for (int labelIndex = 0; labelIndex < static_cast<int>(windowState->freeLabels.size()); ++labelIndex)
			{
				RendererWindowState::FreeLabel &label = windowState->freeLabels[labelIndex];
				ImGui::PushID(labelIndex);

				char textBuffer[128];
				std::snprintf(textBuffer, sizeof(textBuffer), "%s", label.text.c_str());
				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::InputText("##LabelText", textBuffer, sizeof(textBuffer)))
					label.text = textBuffer;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(200.0f);
				ImGui::InputFloat3("##LabelPos", &label.worldPosition.x, "%.3f");
				ImGui::SameLine();
				if (ImGui::Button("X##RemoveLabel"))
					labelToRemove = labelIndex;

				if (ImGui::TreeNode("Style##FreeLabelStyle"))
				{
					drawLabelStyleEditor(label.style);
					ImGui::TreePop();
				}

				ImGui::PopID();
			}
			if (labelToRemove >= 0)
			{
				PushPinnedMeasurementUndoSnapshot(*windowState);
				windowState->freeLabels.erase(windowState->freeLabels.begin() + labelToRemove);
			}

			// Pinned bond/angle labels and free labels are the same kind of object for style purposes
			// (RendererWindowState::LabelStyle) even though they live in separate vectors - selecting
			// several of either/both here edits their style together, same "click, or box/circle-
			// select, then edit" flow RendererPanel::handlePinnedMeasurementInteraction/
			// handleFreeLabelInteraction/applyLabelRegionSelection build the selection with.
			ImGui::Separator();
			ImGui::Text("Selected labels");
			const std::size_t pinCount = windowState->selectedPinnedMeasurements.size();
			const std::size_t freeCount = windowState->selectedFreeLabels.size();
			if (pinCount == 0 && freeCount == 0)
			{
				ImGui::TextDisabled("Click, or box/circle-select, a label in the viewport to select it.");
			}
			else
			{
				// Per-pin controls only make sense for exactly one selected pin and nothing else.
				if (pinCount == 1 && freeCount == 0)
				{
					RendererWindowState::PinnedMeasurement &pin =
						windowState->pinnedMeasurements[windowState->selectedPinnedMeasurements[0]];
					ImGui::TextUnformatted(pin.atomIndices.size() == 2 ? "Bond length" : "Angle");
					if (pin.atomIndices.size() == 2)
					{
						ImGui::SameLine();
						ImGui::Checkbox("Align to bond##PinAlign", &pin.alignToBondDirection);
					}
				}
				else
				{
					ImGui::Text("%zu label(s) selected - style below applies to all of them", pinCount + freeCount);
				}

				// Bulk style edit: edit one representative item's style, then broadcast that same
				// value to every other selected item every frame - simplest correct way to edit N
				// independent LabelStyle structs together in immediate-mode UI without per-widget
				// delta tracking. Snaps the whole selection to the representative's starting style
				// immediately (before any edit), same "pick one, it becomes the shared value" behavior
				// as most bulk-editors, rather than showing a "mixed" state.
				const bool usedPinAsRepresentative = pinCount > 0;
				RendererWindowState::LabelStyle &representative = usedPinAsRepresentative
					? windowState->pinnedMeasurements[windowState->selectedPinnedMeasurements[0]].style
					: windowState->freeLabels[windowState->selectedFreeLabels[0]].style;
				drawLabelStyleEditor(representative);
				for (std::size_t i = usedPinAsRepresentative ? 1 : 0; i < pinCount; ++i)
					windowState->pinnedMeasurements[windowState->selectedPinnedMeasurements[i]].style = representative;
				for (std::size_t i = usedPinAsRepresentative ? 0 : 1; i < freeCount; ++i)
					windowState->freeLabels[windowState->selectedFreeLabels[i]].style = representative;
			}

			ImGui::Separator();
			ImGui::Text("Arrows");
			if (ImGui::Button("+ Add arrow"))
			{
				PushPinnedMeasurementUndoSnapshot(*windowState);
				const glm::vec3 seed = windowState->cursor3DPlaced ? windowState->cursor3DPosition : glm::vec3(0.0f);
				windowState->sceneArrows.push_back(MakeDefaultSceneArrow(*windowState, seed));
				const std::size_t newIndex = windowState->sceneArrows.size() - 1;
				windowState->selectedSceneArrows = {newIndex};
				windowState->sceneArrowQuickEditActive = true;
				windowState->sceneArrowQuickEditIndex = newIndex;
			}
			int arrowToRemove = -1;
			for (int arrowIndex = 0; arrowIndex < static_cast<int>(windowState->sceneArrows.size()); ++arrowIndex)
			{
				RendererWindowState::SceneArrow &arrow = windowState->sceneArrows[arrowIndex];
				ImGui::PushID(arrowIndex);

				// Selectable row syncs both ways with viewport selection (RendererPanel::
				// handleSceneArrowInteraction) - same clear+select/Ctrl-toggle semantics as a plain
				// viewport click, just triggered from the list instead.
				const std::size_t rowIndex = static_cast<std::size_t>(arrowIndex);
				const bool isSelected = std::find(
					windowState->selectedSceneArrows.begin(), windowState->selectedSceneArrows.end(),
					rowIndex) != windowState->selectedSceneArrows.end();
				const char *kindLabel = arrow.kind == RendererWindowState::ArrowKind::Line ? "Line"
					: arrow.kind == RendererWindowState::ArrowKind::Arrow2D ? "Arrow 2D" : "Arrow 3D";
				char rowLabel[32];
				std::snprintf(rowLabel, sizeof(rowLabel), "%s #%d", kindLabel, arrowIndex);
				// AllowOverlap - without it, this Selectable (spanning the full row width) claims mouse
				// hover for the whole row and the "X" button drawn on top of it via SameLine() below
				// never receives a click at all (ImGui's hover-claim is first-come, not topmost-wins) -
				// confirmed as the reason the remove button silently did nothing.
				if (ImGui::Selectable(rowLabel, isSelected, ImGuiSelectableFlags_AllowOverlap))
				{
					std::vector<std::size_t> &selection = windowState->selectedSceneArrows;
					if (ImGui::GetIO().KeyCtrl)
					{
						const auto existing = std::find(selection.begin(), selection.end(), rowIndex);
						if (existing != selection.end())
							selection.erase(existing);
						else
							selection.push_back(rowIndex);
					}
					else
					{
						selection = {rowIndex};
					}
				}
				ImGui::SameLine();
				if (ImGui::Button("X##RemoveArrow"))
					arrowToRemove = arrowIndex;

				DrawSceneArrowEditor(*windowState, rowIndex, SceneArrowEditorMode::Full, m_Layer.GetGlobalSettings());

				ImGui::PopID();
			}
			if (arrowToRemove >= 0)
			{
				PushPinnedMeasurementUndoSnapshot(*windowState);
				EraseSceneArrows(*windowState, {static_cast<std::size_t>(arrowToRemove)});
			}

			// Mirrors "Selected labels" above - own section since ArrowStyle isn't LabelStyle, so it
			// can't share that bulk editor.
			ImGui::Separator();
			ImGui::Text("Selected arrows");
			const std::size_t arrowSelectedCount = windowState->selectedSceneArrows.size();
			if (arrowSelectedCount == 0)
			{
				ImGui::TextDisabled("Click, or box/circle-select, an arrow in the viewport to select it.");
			}
			else
			{
				if (arrowSelectedCount > 1)
					ImGui::Text("%zu arrow(s) selected - style below applies to all of them", arrowSelectedCount);
				RendererWindowState::SceneArrow &representativeArrow =
					windowState->sceneArrows[windowState->selectedSceneArrows[0]];
				const ArrowUndoFn snapshot = [windowState]() { PushPinnedMeasurementUndoSnapshot(*windowState); };
				drawArrowGeometrySection(representativeArrow.style, representativeArrow.kind, snapshot);
				drawArrowAppearanceSection(representativeArrow.style, representativeArrow.kind, snapshot);
				for (std::size_t i = 1; i < arrowSelectedCount; ++i)
					windowState->sceneArrows[windowState->selectedSceneArrows[i]].style = representativeArrow.style;
			}
		}

		ImGui::End();
		SetVisible(windowOpen);
	}
} // namespace DefectStudio
