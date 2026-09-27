#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportNavigationGizmo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

#include <cfloat>

#include <glm/geometric.hpp>
#include <imgui.h>

#include "IconsFontAwesome6.h"

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Events/RendererEvents.hpp"
#include "Presentation/Panels/ViewportGizmo.hpp"
#include "Presentation/Panels/ViewportToolbars.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneTransform.hpp"
#include "Renderer/Scene/ViewportNavigationMath.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kDefaultNavigationGizmoSize = 60.0f;

		[[nodiscard]] bool PointInCircle(const glm::vec2 &point, const glm::vec2 &center, float radius)
		{
			return glm::distance(point, center) <= radius;
		}

		void PublishTransition(
			RendererWindowState &windowState, RendererLayer &layer, const RendererViewCamera &camera,
			const char *sourceAction)
		{
			Ref<EventBus> eventBus = layer.GetEventBus();
			if (eventBus == nullptr)
				return;
			RendererEvents::Viewport::ViewTransitionRequested event;
			event.windowId = windowState.windowId;
			event.targetView.target = camera.Target();
			event.targetView.distance = camera.Distance();
			event.targetView.yaw = camera.Yaw();
			event.targetView.pitch = camera.Pitch();
			event.targetView.roll = camera.Roll();
			event.targetView.projection = camera.Projection();
			event.sourceAction = sourceAction;
			eventBus->Publish(event);
		}

		void PublishDragDelta(RendererWindowState &windowState, RendererLayer &layer, const glm::vec2 &delta)
		{
			Ref<EventBus> eventBus = layer.GetEventBus();
			if (eventBus == nullptr)
				return;
			using Mode = RendererWindowState::NavigationGizmoDragMode;
			if (windowState.navigationGizmoDragMode == Mode::Orbit)
			{
				RendererEvents::Viewport::OrbitDelta event;
				event.windowId = windowState.windowId;
				event.dx = delta.x * layer.GetGlobalSettings().orbitSensitivity;
				event.dy = delta.y * layer.GetGlobalSettings().orbitSensitivity;
				eventBus->Publish(event);
			}
			else if (windowState.navigationGizmoDragMode == Mode::Pan)
			{
				RendererEvents::Viewport::PanDelta event;
				event.windowId = windowState.windowId;
				event.dx = delta.x * layer.GetGlobalSettings().panSensitivity;
				event.dy = delta.y * layer.GetGlobalSettings().panSensitivity;
				eventBus->Publish(event);
			}
			else if (windowState.navigationGizmoDragMode == Mode::Zoom)
			{
				RendererEvents::Viewport::ZoomDelta event;
				event.windowId = windowState.windowId;
				event.amount = -delta.y * 0.020f * layer.GetGlobalSettings().zoomSensitivity;
				eventBus->Publish(event);
			}
		}

		enum class RoundButtonIcon
		{
			Projection,
			Pan,
			Zoom,
			Home,
		};
		struct RoundButtonResult
		{
			bool pressed = false;
			bool hovered = false;
		};

		[[nodiscard]] RoundButtonResult RoundButton(
			const char *id,
			const glm::vec2 &center,
			float radius,
			RoundButtonIcon icon,
			const char *tooltip,
			bool interactive,
			bool orthographic = false,
			bool active = false)
		{
			// Drawing below is all absolute-coordinate drawList calls, not cursor-relative - nothing
			// in this function needs the ImGui cursor restored to its pre-button position. Restoring
			// it used to be exactly the bug: a bare SetCursorPos/SetCursorScreenPos with no item
			// submitted after it is flagged by ImGui as an attempt to grow window/parent boundaries,
			// and asserts in the enclosing End()/EndChild() - which, for an inactive/unhovered pane,
			// is the very next call after this function returns.
			RoundButtonResult result;
			if (interactive)
			{
				ImGui::SetCursorScreenPos(ImVec2(center.x - radius, center.y - radius));
				ImGui::InvisibleButton(id, ImVec2(radius * 2.0f, radius * 2.0f));
				result.hovered = ImGui::IsItemHovered();
				result.pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
			}
			ImDrawList *drawList = ImGui::GetWindowDrawList();
			drawList->AddCircleFilled(
				ImVec2(center.x, center.y), radius,
				active ? IM_COL32(70, 120, 200, 230)
					: result.hovered ? IM_COL32(90, 90, 100, 200) : IM_COL32(40, 40, 46, 150));
			// Font Awesome glyphs, sized to ~55% of the button so they read at any UI scale.
			const char *glyph = icon == RoundButtonIcon::Projection
				? (orthographic ? ICON_FA_BORDER_ALL : ICON_FA_CUBE)
				: icon == RoundButtonIcon::Pan ? ICON_FA_HAND
				: icon == RoundButtonIcon::Zoom ? ICON_FA_MAGNIFYING_GLASS
				: ICON_FA_HOUSE;
			const float glyphSize = radius * 1.1f;
			const ImVec2 glyphExtent = ImGui::GetFont()->CalcTextSizeA(glyphSize, FLT_MAX, 0.0f, glyph);
			drawList->AddText(
				ImGui::GetFont(), glyphSize, ImVec2(center.x - glyphExtent.x * 0.5f, center.y - glyphExtent.y * 0.5f),
				IM_COL32(235, 235, 240, 255), glyph);
			if (result.hovered && tooltip != nullptr)
				ImGui::SetTooltip("%s", tooltip);
			return result;
		}
	} // namespace

	bool RenderViewportNavigationGizmo(
		RendererWindowState &windowState,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize,
		bool viewportHovered,
		const float horizontalToolbarOffset,
		RendererLayer &layer)
	{
		if (windowState.camera == nullptr || imageSize.x <= 0.0f || imageSize.y <= 0.0f)
			return false;
		ImGui::PushID(windowState.windowId.c_str());
		const float uiScale = std::max(
			ImGui::GetIO().FontGlobalScale / kViewportToolbarFontScaleBaseline, 0.01f);
		const float sizeScale = std::max(
			layer.GetGlobalSettings().viewport.navigationGizmoSize / kDefaultNavigationGizmoSize, 0.01f);
		const float scale = uiScale * sizeScale;
		const float gizmoRadius = 60.0f * scale;
		const float axisLength = 41.0f * scale;
		const float positiveRadius = 14.0f * scale;
		const float negativeRadius = 10.0f * scale;
		const glm::vec2 center(
			imageOrigin.x + imageSize.x - gizmoRadius - 14.0f * scale,
			imageOrigin.y + horizontalToolbarOffset + gizmoRadius + 14.0f * scale);
		const glm::vec2 mouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);

		TransformBases bases;
		bases.lattice = windowState.structure.lattice;
		bases.local = windowState.modalTransform.has_value()
			? windowState.modalTransform->bases.local
			: SceneTransformLocalBasis(CaptureSceneTransformSelection(windowState));
		const OrientationAxes axes = ResolveNormalizedOrientationAxes(windowState.transformOrientation, bases);
		const auto markers = ProjectNavigationAxisMarkers(windowState.camera->ViewMatrix(), axes, center, axisLength);
		const auto order = SortNavigationMarkersBackToFront(markers);
		const std::optional<std::size_t> hit = viewportHovered
			? HitTestNavigationAxisMarkers(markers, mouse, positiveRadius, negativeRadius)
			: std::nullopt;
		const bool lattice = windowState.transformOrientation == TransformOrientation::Lattice;
		const std::array<const char *, 3> labels = lattice
			? std::array<const char *, 3>{"a", "b", "c"}
			: std::array<const char *, 3>{"X", "Y", "Z"};

		ImDrawList *drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(imageOrigin, ImVec2(imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y), true);
		drawList->AddCircleFilled(ImVec2(center.x, center.y), gizmoRadius, IM_COL32(18, 18, 22, 90));
		for (const std::size_t index : order)
		{
			const NavigationAxisMarker &marker = markers[index];
			const bool front = marker.depth >= 0.0f;
			const bool markerHovered = hit.has_value() && *hit == index;
			const ImU32 axisColor = (ViewportTransformAxisColor(marker.axis) & IM_COL32(255, 255, 255, 0)) |
				IM_COL32(0, 0, 0, 255);
			const ImU32 lineColor = axisColor &
				(front ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 115));
			const float radius = marker.sign > 0 ? positiveRadius : negativeRadius;
			const ImVec2 markerCenter(marker.center.x, marker.center.y);
			// Blender style: positive = solid disc with a dark letter, negative = faded disc, letter
			// only on hover - two labels at the same spot (axis toward the camera) never overlap.
			if (marker.sign > 0 && marker.axis >= 0)
				drawList->AddLine(ImVec2(center.x, center.y), markerCenter, lineColor, 2.0f * scale);
			drawList->AddCircleFilled(
				markerCenter, radius,
				markerHovered ? IM_COL32(255, 255, 255, 255)
					: marker.sign > 0 ? lineColor
					: (lineColor & IM_COL32(255, 255, 255, 0)) | IM_COL32(0, 0, 0, front ? 110 : 60));
			if (marker.sign > 0 || markerHovered)
			{
				const std::string markerLabel = marker.sign < 0
					? std::string("-") + labels[static_cast<std::size_t>(marker.axis)]
					: labels[static_cast<std::size_t>(marker.axis)];
				const float fontSize = positiveRadius * 1.45f;
				const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, markerLabel.c_str());
				drawList->AddText(
					ImGui::GetFont(), fontSize,
					ImVec2(markerCenter.x - textSize.x * 0.5f, markerCenter.y - textSize.y * 0.5f),
					IM_COL32(20, 20, 24, 255), markerLabel.c_str());
			}
		}
		drawList->PopClipRect();

		bool capturing = viewportHovered && PointInCircle(mouse, center, gizmoRadius);
		if (hit.has_value() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			const NavigationAxisMarker &marker = markers[*hit];
			const glm::vec3 clickedSide = axes[static_cast<std::size_t>(marker.axis)] * static_cast<float>(marker.sign);
			const glm::vec3 eyeDirection = windowState.camera->Position() - windowState.camera->Target();
			const glm::vec3 viewSide = ResolveNavigationViewSide(clickedSide, eyeDirection);
			RendererViewCamera target = *windowState.camera;
			const glm::vec3 up = std::abs(glm::dot(viewSide, glm::vec3(0.0f, 0.0f, 1.0f))) > 0.95f
				? glm::vec3(0.0f, 1.0f, 0.0f)
				: glm::vec3(0.0f, 0.0f, 1.0f);
			target.SetAlignToAxis(-viewSide, up);
			PublishTransition(windowState, layer, target, "navigation_gizmo.axis");
		}

		using Mode = RendererWindowState::NavigationGizmoDragMode;
		const auto beginDrag = [&](Mode mode, const char *source, bool fromButton = false)
		{
			if (windowState.transitionActive)
			{
				windowState.transitionActive = false;
				layer.CommitViewInteraction(windowState.windowId);
			}
			windowState.navigationGizmoDragMode = mode;
			windowState.navigationGizmoLastMouse = mouse;
			windowState.navigationGizmoDragStartMouse = mouse;
			windowState.navigationGizmoDragFromButton = fromButton;
			layer.BeginViewInteraction(windowState.windowId, source);
		};
		if (!hit.has_value() && viewportHovered && capturing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			beginDrag(Mode::Orbit, "navigation_gizmo.orbit");

		const float buttonRadius = 21.0f * scale;
		const float buttonGap = 8.0f * scale;
		glm::vec2 buttonCenter(center.x, center.y + gizmoRadius + buttonRadius + buttonGap);
		const RoundButtonResult projectionButton = RoundButton(
			"##NavigationProjection", buttonCenter, buttonRadius, RoundButtonIcon::Projection,
			"Toggle Orthographic / Perspective (right-click for zoom step)", viewportHovered,
			windowState.camera->Projection() == CameraProjection::Orthographic);
		if (projectionButton.pressed)
		{
			if (Ref<EventBus> eventBus = layer.GetEventBus())
			{
				RendererEvents::Viewport::ProjectionToggleRequested event;
				event.windowId = windowState.windowId;
				eventBus->Publish(event);
			}
		}
		if (projectionButton.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			ImGui::OpenPopup("##NavigationProjectionSettings");
		ImGui::SetNextWindowPos(ImVec2(buttonCenter.x + buttonRadius, buttonCenter.y - buttonRadius), ImGuiCond_Appearing);
		if (ImGui::BeginPopup("##NavigationProjectionSettings", ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Zoom step [%]");
			ImGui::SetNextItemWidth(110.0f * uiScale);
			ImGui::InputFloat("##NavigationZoomStep", &windowState.percentStep, 0.0f, 0.0f, "%.0f");
			windowState.percentStep = std::clamp(windowState.percentStep, 0.0f, 180.0f);
			ImGui::EndPopup();
		}
		capturing = capturing || projectionButton.hovered;

		buttonCenter.y += buttonRadius * 2.0f + buttonGap;
		const RoundButtonResult panButton = RoundButton(
			"##NavigationPan", buttonCenter, buttonRadius, RoundButtonIcon::Pan,
			"Pan view: drag, or click to toggle LMB pan (Shift+MMB)", viewportHovered, false,
			windowState.navigationGizmoLatchedMode == Mode::Pan);
		if (panButton.pressed)
			beginDrag(Mode::Pan, "navigation_gizmo.pan", true);
		capturing = capturing || panButton.hovered;

		buttonCenter.y += buttonRadius * 2.0f + buttonGap;
		const RoundButtonResult zoomButton = RoundButton(
			"##NavigationZoom", buttonCenter, buttonRadius, RoundButtonIcon::Zoom,
			"Zoom view: drag, or click to toggle LMB zoom (Ctrl+MMB / wheel)", viewportHovered, false,
			windowState.navigationGizmoLatchedMode == Mode::Zoom);
		if (zoomButton.pressed)
			beginDrag(Mode::Zoom, "navigation_gizmo.zoom", true);
		capturing = capturing || zoomButton.hovered;

		buttonCenter.y += buttonRadius * 2.0f + buttonGap;
		const RoundButtonResult homeButton = RoundButton(
			"##NavigationHome", buttonCenter, buttonRadius, RoundButtonIcon::Home, "Reset view (frame all atoms)",
			viewportHovered);
		if (homeButton.pressed)
			PublishTransition(windowState, layer, ComputeResetViewCamera(windowState), "navigation_gizmo.reset_view");
		capturing = capturing || homeButton.hovered;

		const Mode latched = windowState.navigationGizmoLatchedMode;
		if (latched != Mode::None)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				windowState.navigationGizmoLatchedMode = Mode::None;
			else if (viewportHovered)
			{
				// Latched: the whole viewport is a pan/zoom surface, so selection tools stay out.
				capturing = true;
				if (windowState.navigationGizmoDragMode == Mode::None && !ImGui::IsAnyItemHovered() &&
					ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					beginDrag(latched, latched == Mode::Pan ? "navigation_gizmo.pan" : "navigation_gizmo.zoom");
			}
		}

		if (windowState.navigationGizmoDragMode != Mode::None)
		{
			capturing = true;
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				const glm::vec2 delta = mouse - windowState.navigationGizmoLastMouse;
				windowState.navigationGizmoLastMouse = mouse;
				PublishDragDelta(windowState, layer, delta);
			}
			else
			{
				layer.CommitViewInteraction(windowState.windowId);
				constexpr float kClickSlopPixels = 3.0f;
				const Mode released = windowState.navigationGizmoDragMode;
				if (windowState.navigationGizmoDragFromButton && released != Mode::Orbit &&
					glm::distance(mouse, windowState.navigationGizmoDragStartMouse) <= kClickSlopPixels)
					windowState.navigationGizmoLatchedMode =
						windowState.navigationGizmoLatchedMode == released ? Mode::None : released;
				windowState.navigationGizmoDragMode = Mode::None;
			}
		}

		ImGui::PopID();
		return capturing;
	}
} // namespace DefectStudio
