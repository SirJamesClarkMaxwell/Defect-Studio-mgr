#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportPathOverlay.hpp"

#include <algorithm>

#include <imgui.h>

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathHandleGeometry.hpp"
#include "Renderer/RendererWindowState.hpp"

namespace DefectStudio
{
	void DrawViewportPathOverlay(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		if (!windowState.pathEdit.IsActive() || windowState.camera == nullptr || windowState.paths == nullptr ||
			imageSize.x <= 0.0f || imageSize.y <= 0.0f)
			return;

		const ScenePath *path = windowState.paths->Store().Find(windowState.pathEdit.Path());
		if (path == nullptr || !path->visible || !path->renderable)
			return;

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const std::vector<PathHandleMarker> markers = BuildPathHandleMarkers(
			*path,
			ResolveNodePositions(*path, BindingContext{}),
			windowState.camera->ProjectionMatrix() * view,
			glm::vec2(imageSize.x, imageSize.y),
			windowState.pathEdit.ActiveElement());

		ImDrawList &drawList = *ImGui::GetWindowDrawList();
		drawList.PushClipRect(
			imageOrigin, ImVec2(imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y), true);

		constexpr ImU32 kTetherColor = IM_COL32(180, 180, 180, 190);
		constexpr ImU32 kNodeColor = IM_COL32(70, 180, 255, 230);
		constexpr ImU32 kHandleColor = IM_COL32(255, 170, 70, 230);
		constexpr ImU32 kSelectedColor = IM_COL32(255, 210, 70, 255);
		constexpr ImU32 kOutlineColor = IM_COL32(25, 25, 25, 240);
		constexpr float kOutlineThickness = 1.5f;

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
				kTetherColor, 1.0f);
		}

		for (const PathHandleMarker &marker : markers)
		{
			const ImVec2 point(imageOrigin.x + marker.screenPosition.x, imageOrigin.y + marker.screenPosition.y);
			const bool selected = windowState.pathEdit.IsSelected(marker.element);
			const ImU32 fill = selected
				? kSelectedColor
				: marker.kind == PathMarkerKind::Node ? kNodeColor : kHandleColor;
			drawList.AddCircleFilled(point, marker.drawRadius, fill);
			drawList.AddCircle(
				point, std::max(0.0f, marker.drawRadius - kOutlineThickness * 0.5f),
				kOutlineColor, 0, kOutlineThickness);
		}

		drawList.PopClipRect();
	}
} // namespace DefectStudio
