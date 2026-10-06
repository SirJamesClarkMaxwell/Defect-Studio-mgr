#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportPathOverlay.hpp"

#include <algorithm>

#include <imgui.h>

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathHandleGeometry.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/SceneSystem.hpp"

namespace DefectStudio
{
	void DrawViewportPathOverlay(
		const RendererWindowState &windowState, const RendererGlobalRenderSettings &globalSettings,
		const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		if (!windowState.pathEdit.IsActive() || windowState.camera == nullptr || windowState.paths == nullptr ||
			imageSize.x <= 0.0f || imageSize.y <= 0.0f)
			return;

		const ScenePath *path = windowState.paths->Store().Find(windowState.pathEdit.Path());
		if (path == nullptr || !path->visible || !path->renderable)
			return;

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const RendererViewportSettings &settings = globalSettings.viewport;
		const BindingContext bindings = SceneSystem::MakePathBindingContext(windowState);
		const std::vector<PathHandleMarker> markers = BuildPathHandleMarkers(
			*path,
			ResolveNodePositions(*path, bindings),
			windowState.camera->ProjectionMatrix() * view,
			glm::vec2(imageSize.x, imageSize.y),
			windowState.pathEdit.ActiveElement());

		ImDrawList &drawList = *ImGui::GetWindowDrawList();
		drawList.PushClipRect(
			imageOrigin, ImVec2(imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y), true);

		const ImU32 tetherColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
			settings.pathEditTetherColor.x,
			settings.pathEditTetherColor.y,
			settings.pathEditTetherColor.z,
			settings.pathEditTetherColor.w));
		const ImU32 nodeColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
			settings.pathEditNodeColor.x,
			settings.pathEditNodeColor.y,
			settings.pathEditNodeColor.z,
			settings.pathEditNodeColor.w));
		const ImU32 handleColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
			settings.pathEditHandleColor.x,
			settings.pathEditHandleColor.y,
			settings.pathEditHandleColor.z,
			settings.pathEditHandleColor.w));
		const ImU32 selectedColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
			settings.pathEditSelectedColor.x,
			settings.pathEditSelectedColor.y,
			settings.pathEditSelectedColor.z,
			settings.pathEditSelectedColor.w));
		const ImU32 outlineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
			settings.pathEditOutlineColor.x,
			settings.pathEditOutlineColor.y,
			settings.pathEditOutlineColor.z,
			settings.pathEditOutlineColor.w));

		for (const PathHandleMarker &handle : markers)
		{
			if (handle.kind != PathMarkerKind::BezierHandle)
				continue;
			const auto owner = std::find_if(markers.begin(), markers.end(), [&handle](const PathHandleMarker &marker) {
				return marker.kind == PathMarkerKind::Node && marker.element == handle.owner;
			});
			if (owner == markers.end())
				continue;
			drawList.AddLine(
					ImVec2(imageOrigin.x + handle.screenPosition.x, imageOrigin.y + handle.screenPosition.y),
					ImVec2(imageOrigin.x + owner->screenPosition.x, imageOrigin.y + owner->screenPosition.y),
					tetherColor, settings.pathEditTetherThickness);
		}

		for (const PathHandleMarker &marker : markers)
		{
			const ImVec2 point(imageOrigin.x + marker.screenPosition.x, imageOrigin.y + marker.screenPosition.y);
			const bool selected = windowState.pathEdit.IsSelected(marker.element);
			const ImU32 fill = selected
				? selectedColor
				: marker.kind == PathMarkerKind::Node ? nodeColor : handleColor;
			drawList.AddCircleFilled(point, marker.drawRadius, fill);
			drawList.AddCircle(
				point, std::max(0.0f, marker.drawRadius - settings.pathEditOutlineThickness * 0.5f),
				outlineColor, 0, settings.pathEditOutlineThickness);
		}

		drawList.PopClipRect();
	}
} // namespace DefectStudio
