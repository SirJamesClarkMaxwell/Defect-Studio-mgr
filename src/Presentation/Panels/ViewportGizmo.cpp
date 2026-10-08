#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportGizmo.hpp"
#include "Presentation/Panels/ViewportRotateGizmo.hpp"
#include "Presentation/Panels/ViewportTranslateGizmo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

#include <glm/gtc/constants.hpp>

#include "Presentation/Panels/ViewportModalTransform.hpp"
#include "Renderer/Scene/ViewportNavigationMath.hpp"
#include "Renderer/RendererLayer.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/ModalTransform.hpp"
#include "Renderer/Scene/SceneTransform.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kPickMinDistance = 20.0f;
		constexpr float kPickMaxDistance = 100.0f;
		constexpr float kDefaultTransformGizmoSize = 100.0f;
		constexpr float kScaleHandleLength = 100.0f;
		constexpr float kScaleHandleHalfExtent = 7.0f;
		constexpr float kScaleCenterRadius = 12.0f;
		constexpr std::array<ImU32, 3> kAxisColors = {
			IM_COL32(230, 70, 70, 200), IM_COL32(90, 210, 90, 200), IM_COL32(90, 150, 240, 200)};

		struct AxisProjection
		{
			glm::vec2 direction = glm::vec2(0.0f);
			bool valid = false;
		};

		[[nodiscard]] std::optional<glm::vec2> ProjectAbsolute(
			const glm::mat4 &viewProjection, const ImVec2 &imageOrigin, const ImVec2 &imageSize,
			const glm::vec3 &world)
		{
			const std::optional<glm::vec2> relative = SelectionHitTest::ProjectToScreen(
				viewProjection, glm::vec2(imageSize.x, imageSize.y), world);
			return relative.has_value()
				? std::optional<glm::vec2>(glm::vec2(imageOrigin.x, imageOrigin.y) + *relative)
				: std::nullopt;
		}

		[[nodiscard]] std::array<AxisProjection, 3> ProjectAxes(
			const glm::mat4 &viewProjection, const ImVec2 &imageOrigin, const ImVec2 &imageSize,
			const glm::vec3 &pivot, const glm::vec2 &pivotScreen, const OrientationAxes &worldAxes)
		{
			std::array<AxisProjection, 3> axes;
			for (int axis = 0; axis < 3; ++axis)
			{
				const std::optional<glm::vec2> probe =
					ProjectAbsolute(viewProjection, imageOrigin, imageSize, pivot + worldAxes[axis]);
				if (!probe.has_value())
					continue;
				const glm::vec2 projected = *probe - pivotScreen;
				if (glm::dot(projected, projected) > 1.0f)
					axes[axis] = AxisProjection{glm::normalize(projected), true};
			}
			return axes;
		}

		void ScaleRotateGizmoGeometry(
			RotateGizmoGeometry &geometry, const glm::vec2 &pivot, const float gizmoScale)
		{
			geometry.trackballRadius *= gizmoScale;
			for (RotateArcGeometry &arc : geometry.arcs)
				for (glm::vec2 &point : arc.points)
					point = pivot + (point - pivot) * gizmoScale;
		}

		void ScaleTranslatePlaneGeometry(
			std::array<TranslatePlaneGeometry, 3> &planes,
			const glm::vec2 &pivot,
			const float gizmoScale)
		{
			for (TranslatePlaneGeometry &plane : planes)
				if (plane.valid)
					for (glm::vec2 &corner : plane.corners)
						corner = pivot + (corner - pivot) * gizmoScale;
		}

		void DrawAxisHandles(const glm::vec2 &pivot, const std::array<AxisProjection, 3> &axes,
			const ImVec2 &imageOrigin, const ImVec2 &imageSize, const float gizmoScale)
		{
			ImDrawList &drawList = *ImGui::GetWindowDrawList();
			drawList.PushClipRect(
				imageOrigin, ImVec2(imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y), true);
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axes[axis].valid)
					continue;
				const glm::vec2 direction = axes[axis].direction;
				const glm::vec2 perpendicular(-direction.y, direction.x);
				const glm::vec2 tip = pivot + direction * kPickMaxDistance * gizmoScale;
				const glm::vec2 headBase = tip - direction * 16.0f * gizmoScale;
				drawList.AddLine(
					ImVec2(pivot.x, pivot.y), ImVec2(headBase.x, headBase.y), ViewportTransformAxisColor(axis),
					3.5f * gizmoScale);
				drawList.AddTriangleFilled(
					ImVec2(tip.x, tip.y), ImVec2(headBase.x + perpendicular.x * 6.0f * gizmoScale,
						headBase.y + perpendicular.y * 6.0f * gizmoScale),
					ImVec2(headBase.x - perpendicular.x * 6.0f * gizmoScale,
						headBase.y - perpendicular.y * 6.0f * gizmoScale),
					ViewportTransformAxisColor(axis));
			}
			drawList.AddCircleFilled(ImVec2(pivot.x, pivot.y), 5.0f * gizmoScale, IM_COL32(235, 235, 235, 255));
			drawList.PopClipRect();
		}

		[[nodiscard]] int HitTestAxis(
			const glm::vec2 &mouse, const glm::vec2 &pivot, const std::array<AxisProjection, 3> &axes,
			const float gizmoScale)
		{
			const glm::vec2 fromPivot = mouse - pivot;
			const float radial = glm::length(fromPivot);
			// Past the tip by the scale handle's square, so the whole square is grabbable.
			if (radial < kPickMinDistance * gizmoScale || radial > (kPickMaxDistance + 2.0f * kScaleHandleHalfExtent) * gizmoScale)
				return -1;
			float bestDistance = 16.0f * gizmoScale;
			int bestAxis = -1;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axes[axis].valid)
					continue;
				const float along = glm::dot(fromPivot, axes[axis].direction);
				const float distance = glm::length(fromPivot - axes[axis].direction * along);
				if (along > 0.0f && distance < bestDistance)
				{
					bestDistance = distance;
					bestAxis = axis;
				}
			}
			return bestAxis;
		}

		void DrawScaleGizmo(const glm::vec2 &pivot, const std::array<AxisProjection, 3> &axes, int hoveredAxis,
			bool hoveringCenter, const ImVec2 &imageOrigin, const ImVec2 &imageSize, const float gizmoScale)
		{
			ImDrawList &drawList = *ImGui::GetWindowDrawList();
			drawList.PushClipRect(
				imageOrigin, ImVec2(imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y), true);
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axes[axis].valid)
					continue;
				const glm::vec2 direction = axes[axis].direction;
				const glm::vec2 perpendicular(-direction.y, direction.x);
				const glm::vec2 tip = pivot + direction * kScaleHandleLength * gizmoScale;
				const glm::vec2 shaftEnd = tip - direction * (kScaleHandleHalfExtent + 2.0f) * gizmoScale;
				const ImU32 color = axis == hoveredAxis
					? IM_COL32(255, 200, 60, 240)
					: ViewportTransformAxisColor(axis);
				drawList.AddLine(ImVec2(pivot.x, pivot.y), ImVec2(shaftEnd.x, shaftEnd.y), color, 3.5f * gizmoScale);
				const glm::vec2 along = direction * kScaleHandleHalfExtent * gizmoScale;
				const glm::vec2 across = perpendicular * kScaleHandleHalfExtent * gizmoScale;
				const std::array<ImVec2, 4> box = {
					ImVec2((tip - along - across).x, (tip - along - across).y),
					ImVec2((tip + along - across).x, (tip + along - across).y),
					ImVec2((tip + along + across).x, (tip + along + across).y),
					ImVec2((tip - along + across).x, (tip - along + across).y)};
				drawList.AddTriangleFilled(box[0], box[1], box[2], color);
				drawList.AddTriangleFilled(box[0], box[2], box[3], color);
				for (int corner = 0; corner < 4; ++corner)
					drawList.AddLine(box[corner], box[(corner + 1) % 4], IM_COL32(30, 30, 30, 220), gizmoScale);
			}
			drawList.AddCircleFilled(
				ImVec2(pivot.x, pivot.y), kScaleCenterRadius * gizmoScale,
				hoveringCenter ? IM_COL32(255, 200, 60, 240) : IM_COL32(235, 235, 235, 255));
			drawList.PopClipRect();
		}

	} // namespace

	ImU32 ViewportTransformAxisColor(const int axis)
	{
		return axis >= 0 && axis < static_cast<int>(kAxisColors.size()) ? kAxisColors[axis] : 0;
	}

	bool RenderTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistryRef)
	{
		if (UpdateViewportModalTransform(windowState, imageOrigin, imageSize, layer, commandRegistryRef))
			return true;
		if (windowState.camera == nullptr)
			return false;
		const float gizmoScale = std::max(
			layer.GetGlobalSettings().viewport.transformGizmoSize / kDefaultTransformGizmoSize, 0.01f);

		const SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(windowState);
		if (SceneTransformPivotPositions(windowState, snapshot).empty())
		{
			windowState.gizmoDragActive = false;
			return false;
		}

		const glm::vec2 mouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		const ModalTransformOp operation = windowState.gizmoOperation == GizmoOperation::Rotate
			? ModalTransformOp::Rotate
			: windowState.gizmoOperation == GizmoOperation::Scale
				? ModalTransformOp::Scale
				: ModalTransformOp::Translate;
		const std::vector<glm::vec3> anchorPositions = SceneTransformAnchorPositions(windowState, snapshot);
		const std::optional<glm::vec3> cursor = windowState.cursor3DPlaced
			? std::optional<glm::vec3>(windowState.cursor3DPosition)
			: std::nullopt;
		const glm::vec3 gizmoPosition = ComputeTransformPivot(windowState.transformPivotMode, anchorPositions, cursor);
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		const std::optional<glm::vec2> gizmoScreen = ProjectAbsolute(viewProjection, imageOrigin, imageSize, gizmoPosition);
		if (!gizmoScreen.has_value())
			return false;

		// A handle always wins over the atom or bond drawn under it, for scene objects too: an overlap
		// guard here left a hide volume's handles grabbable only in the gaps between atoms of a dense
		// cell (task 91). Path-edit markers already get first refusal in the chain.
		const TransformBases bases = SceneTransformBases(windowState, snapshot);
		const OrientationAxes worldAxes =
			ResolveNormalizedOrientationAxes(SceneTransformOrientation(windowState.transformOrientation, snapshot), bases);
		const std::array<AxisProjection, 3> axes = ProjectAxes(
			viewProjection, imageOrigin, imageSize, gizmoPosition, *gizmoScreen, worldAxes);
		if (windowState.gizmoOperation == GizmoOperation::Rotate)
		{
			RotateGizmoGeometry geometry = BuildRotateGizmoGeometry(
				*windowState.camera, viewProjection, imageOrigin, imageSize, gizmoPosition, *gizmoScreen, worldAxes);
			ScaleRotateGizmoGeometry(geometry, *gizmoScreen, gizmoScale);
			const int hoveredAxis = HitTestRotateArcs(geometry, mouse);
			const float radial = glm::length(mouse - *gizmoScreen);
			const bool hoveringTrackball = hovered &&
				radial >= geometry.trackballRadius - 7.0f * gizmoScale &&
				radial <= geometry.trackballRadius + 7.0f * gizmoScale;
			DrawRotateGizmo(geometry, *gizmoScreen, hoveredAxis, hoveringTrackball, imageOrigin, imageSize);
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				(hoveredAxis >= 0 || hoveringTrackball))
			{
				BeginViewportModalTransform(
					windowState, ModalTransformOp::Rotate, mouse,
					hoveredAxis >= 0 ? std::optional<int>(hoveredAxis) : std::nullopt, true);
				return true;
			}
			windowState.gizmoDragActive = false;
			return hovered && (hoveredAxis >= 0 || hoveringTrackball);
		}
		if (windowState.gizmoOperation == GizmoOperation::Scale)
		{
			const float radial = glm::length(mouse - *gizmoScreen);
			const bool hoveringCenter = hovered && radial <= kScaleCenterRadius * gizmoScale;
			const int hoveredAxis = hoveringCenter
				? -1 : HitTestAxis(mouse, *gizmoScreen, axes, gizmoScale);
			DrawScaleGizmo(*gizmoScreen, axes, hoveredAxis, hoveringCenter, imageOrigin, imageSize, gizmoScale);
			if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				(hoveredAxis >= 0 || hoveringCenter))
			{
				BeginViewportModalTransform(
					windowState, ModalTransformOp::Scale, mouse,
					hoveredAxis >= 0 ? std::optional<int>(hoveredAxis) : std::nullopt, true);
				return true;
			}
			windowState.gizmoDragActive = false;
			return hovered && (hoveredAxis >= 0 || hoveringCenter);
		}

		std::array<glm::vec2, 3> axisDirections{};
		for (int axis = 0; axis < 3; ++axis)
			if (axes[axis].valid)
				axisDirections[axis] = axes[axis].direction;
		auto planes = BuildTranslatePlaneGeometry(*gizmoScreen, axisDirections);
		ScaleTranslatePlaneGeometry(planes, *gizmoScreen, gizmoScale);
		const int hoveredAxis = HitTestAxis(mouse, *gizmoScreen, axes, gizmoScale);
		const int hoveredPlaneAxis = hoveredAxis >= 0
			? -1
			: HitTestTranslatePlaneHandles(mouse, planes);
		DrawTranslatePlaneHandles(planes, hoveredPlaneAxis, imageOrigin, imageSize);
		DrawAxisHandles(*gizmoScreen, axes, imageOrigin, imageSize, gizmoScale);
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredAxis >= 0)
		{
			BeginViewportModalTransform(windowState, operation, mouse, hoveredAxis, true);
			return true;
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredPlaneAxis >= 0)
		{
			BeginViewportModalTransform(windowState, operation, mouse, hoveredPlaneAxis, true, true);
			return true;
		}

		windowState.gizmoDragActive = false;
		return hovered && (hoveredAxis >= 0 || hoveredPlaneAxis >= 0);
	}
} // namespace DefectStudio
