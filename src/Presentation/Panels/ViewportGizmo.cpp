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
		constexpr float kSceneArrowHandleDrawRadius = 5.0f;
		constexpr float kActiveSceneArrowHandleDrawRadius = 7.0f;
		// Deliberately much bigger than the drawn dot (see SceneArrowHandleGeometry) - live testing
		// showed most real clicks aimed at a handle landed 30-135px away from it, missing an 11px
		// pick radius entirely and falling through to the plain-shaft-click handler, which forces the
		// target back to Both. 20/26px is comparable to the pre-task-40 tolerance
		// (ViewportSceneArrowInteraction.cpp's old `max(14px, shaft-half+8px)`), which was usable.
		constexpr float kSceneArrowHandlePickRadius = 20.0f;
		constexpr float kActiveSceneArrowHandlePickRadius = 26.0f;
		constexpr std::array<ImU32, 3> kAxisColors = {
			IM_COL32(230, 70, 70, 200), IM_COL32(90, 210, 90, 200), IM_COL32(90, 150, 240, 200)};

		struct Ray
		{
			glm::vec3 origin = glm::vec3(0.0f);
			glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
		};

		struct AxisProjection
		{
			glm::vec2 direction = glm::vec2(0.0f);
			bool valid = false;
		};

		[[nodiscard]] std::optional<Ray> MakeScreenRay(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPosition)
		{
			if (windowState.camera == nullptr || windowState.viewportSize.x <= 0.0f || windowState.viewportSize.y <= 0.0f)
				return std::nullopt;
			const glm::vec2 relative = screenPosition - glm::vec2(imageOrigin.x, imageOrigin.y);
			if (relative.x < 0.0f || relative.y < 0.0f ||
				relative.x >= windowState.viewportSize.x || relative.y >= windowState.viewportSize.y)
				return std::nullopt;

			const glm::vec2 ndc(
				relative.x * 2.0f / windowState.viewportSize.x - 1.0f,
				1.0f - relative.y * 2.0f / windowState.viewportSize.y);
			const glm::mat4 inverseViewProjection =
				glm::inverse(windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix());
			const glm::vec4 nearH = inverseViewProjection * glm::vec4(ndc, -1.0f, 1.0f);
			const glm::vec4 farH = inverseViewProjection * glm::vec4(ndc, 1.0f, 1.0f);
			if (std::abs(nearH.w) <= 1.0e-6f || std::abs(farH.w) <= 1.0e-6f)
				return std::nullopt;
			const glm::vec3 nearPoint = glm::vec3(nearH) / nearH.w;
			const glm::vec3 direction = glm::vec3(farH) / farH.w - nearPoint;
			if (glm::dot(direction, direction) <= 1.0e-8f)
				return std::nullopt;
			return Ray{nearPoint, glm::normalize(direction)};
		}

		[[nodiscard]] bool IsAtomOrBondUnderMouse(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &mouse)
		{
			const std::optional<Ray> ray = MakeScreenRay(windowState, imageOrigin, mouse);
			if (!ray.has_value())
				return false;
			for (const RendererAtomData &atom : windowState.structure.atoms)
			{
				if (!atom.visible)
					continue;
				const glm::vec3 offset = ray->origin - atom.cartesianPosition;
				const float b = 2.0f * glm::dot(offset, ray->direction);
				const float radius = atom.radius * 1.35f;
				const float discriminant = b * b - 4.0f * (glm::dot(offset, offset) - radius * radius);
				if (discriminant >= 0.0f && (-b + std::sqrt(discriminant)) * 0.5f > 0.001f)
					return true;
			}
			for (const RendererBondData &bond : windowState.structure.bonds)
			{
				if (!bond.visible || bond.firstAtomIndex >= windowState.structure.atoms.size() ||
					bond.secondAtomIndex >= windowState.structure.atoms.size())
					continue;
				float rayDistance = 0.0f;
				glm::vec3 closestOnBond(0.0f);
				SelectionHitTest::ClosestPointsRaySegment(
					ray->origin, ray->direction,
					windowState.structure.atoms[bond.firstAtomIndex].cartesianPosition,
					windowState.structure.atoms[bond.secondAtomIndex].cartesianPosition + bond.secondAtomPeriodicOffset,
					rayDistance, closestOnBond);
				const float pickRadius = std::max(bond.radius * 2.5f, 0.12f);
				if (rayDistance > 0.001f &&
					glm::distance(ray->origin + ray->direction * rayDistance, closestOnBond) <= pickRadius)
					return true;
			}
			return false;
		}

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
			if (radial < kPickMinDistance * gizmoScale || radial > kPickMaxDistance * gizmoScale)
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

	std::array<SceneArrowHandleGeometry, 3> BuildSceneArrowHandleGeometry(
		const RendererWindowState::SceneArrow &arrow,
		const RendererViewCamera &camera,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize,
		const GizmoOperation operation,
		const bool showMidpoint,
		const RendererWindowState::SceneArrowDragTarget activeTarget,
		const bool active)
	{
		using Target = RendererWindowState::SceneArrowDragTarget;
		std::array<SceneArrowHandleGeometry, 3> handles = {{
			{Target::Start, glm::vec2(0.0f), kSceneArrowHandleDrawRadius, kSceneArrowHandlePickRadius, false},
			{Target::End, glm::vec2(0.0f), kSceneArrowHandleDrawRadius, kSceneArrowHandlePickRadius, false},
			{Target::Both, glm::vec2(0.0f), kSceneArrowHandleDrawRadius, kSceneArrowHandlePickRadius, false}}};
		const glm::mat4 viewProjection = camera.ProjectionMatrix() * camera.ViewMatrix();
		const std::array<glm::vec3, 3> worldPoints = {
			arrow.start(), arrow.end(), (arrow.start() + arrow.end()) * 0.5f};
		for (std::size_t index = 0; index < handles.size(); ++index)
		{
			SceneArrowHandleGeometry &handle = handles[index];
			handle.visible = index < 2 || (showMidpoint && operation == GizmoOperation::Translate);
			if (!handle.visible)
				continue;
			const std::optional<glm::vec2> screen =
				ProjectAbsolute(viewProjection, imageOrigin, imageSize, worldPoints[index]);
			if (!screen.has_value())
			{
				handle.visible = false;
				continue;
			}
			handle.point = *screen;
			if (active && (activeTarget == Target::Both || activeTarget == handle.target))
			{
				handle.drawRadius = kActiveSceneArrowHandleDrawRadius;
				handle.pickRadius = kActiveSceneArrowHandlePickRadius;
			}
		}
		return handles;
	}

	void DrawSceneArrowHandleMarkers(
		const RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize)
	{
		if (windowState.camera == nullptr || windowState.selectedSceneArrows.empty())
			return;

		ImDrawList &drawList = *ImGui::GetWindowDrawList();
		constexpr float kHandleOutlineThickness = 1.5f;
		const bool singleArrow = windowState.selectedSceneArrows.size() == 1;
		const bool singleDragging = windowState.sceneArrowDragging && singleArrow;
		for (const SceneObjectId id : windowState.selectedSceneArrows)
		{
			const auto arrow = std::find_if(
				windowState.sceneArrows.begin(), windowState.sceneArrows.end(),
				[id](const RendererWindowState::SceneArrow &candidate) { return candidate.id == id; });
			if (arrow == windowState.sceneArrows.end())
				continue;
			const auto handles = BuildSceneArrowHandleGeometry(
				*arrow, *windowState.camera, imageOrigin, imageSize, windowState.gizmoOperation,
				singleArrow, windowState.sceneArrowDragTarget, singleDragging);
			for (const SceneArrowHandleGeometry &handle : handles)
			{
				if (!handle.visible)
					continue;
				const ImVec2 point(handle.point.x, handle.point.y);
				drawList.AddCircleFilled(point, handle.drawRadius, IM_COL32(255, 200, 60, 220));
				drawList.AddCircle(
					point, handle.drawRadius - kHandleOutlineThickness * 0.5f,
					IM_COL32(40, 25, 0, 255), 0, kHandleOutlineThickness);
			}
		}
	}

	bool RenderTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistryRef)
	{
		if (UpdateViewportModalTransform(windowState, imageOrigin, imageSize, layer, commandRegistryRef))
			return true;
		// A plain click on the arrow's shaft (not a handle marker - those start a modal transform,
		// see below) is a screen-plane rigid-move of the whole arrow, continued by
		// HandleSceneArrowInteraction later in the dispatch chain.
		if (windowState.sceneArrowDragging)
			return false;
		if (windowState.camera == nullptr)
			return false;
		const float gizmoScale = std::max(
			layer.GetGlobalSettings().viewport.transformGizmoSize / kDefaultTransformGizmoSize, 0.01f);

		SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(windowState);
		if (SceneTransformPivotPositions(windowState, snapshot).empty())
		{
			windowState.gizmoDragActive = false;
			return false;
		}

		const bool singleArrowOnly = snapshot.atoms.empty() && snapshot.labels.empty() && snapshot.arrows.size() == 1;
		if (singleArrowOnly)
		{
			const std::size_t index = snapshot.arrows.front().index;
			if (windowState.sceneArrowGizmoActiveArrowIndex != index)
			{
				windowState.sceneArrowGizmoActiveArrowIndex = index;
				windowState.sceneArrowGizmoActiveTarget = RendererWindowState::SceneArrowDragTarget::Both;
			}
		}

		const glm::vec2 mouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		if (singleArrowOnly && windowState.gizmoOperation == GizmoOperation::Translate)
		{
			const RendererWindowState::SceneArrow &arrow =
				windowState.sceneArrows[snapshot.arrows.front().index];
			const auto handles = BuildSceneArrowHandleGeometry(
				arrow, *windowState.camera, imageOrigin, imageSize, windowState.gizmoOperation, true,
				windowState.sceneArrowDragTarget, windowState.sceneArrowDragging);
			// UpdateViewportModalTransform already claimed any in-progress axis drag above. A marker
			// only owns a new frame on an actual click; hover must not hide or interrupt the axis gizmo.
			const bool handleClicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			for (const SceneArrowHandleGeometry &handle : handles)
			{
				if (handle.visible && handleClicked && glm::distance(mouse, handle.point) <= handle.pickRadius)
				{
					// Grabbing a marker starts a real modal transform (unconstrained/free, confirmed
					// on mouse-release like every other handle-started drag) instead of the older
					// screen-plane-only free-drag in ViewportSceneArrowInteraction.cpp - that one never
					// engages the X/Y/Z axis constraint keys and computed its delta purely from mouse
					// pixels via camera-right/camera-up, which is why dragging visibly slid along
					// whatever direction happened to roughly match "screen up" for the current camera
					// angle instead of a real 3D axis. This is now the single path a handle drag takes.
					windowState.sceneArrowGizmoActiveTarget = handle.target;
					BeginViewportModalTransform(windowState, ModalTransformOp::Translate, mouse, std::nullopt, true);
					return true;
				}
			}
		}

		const ModalTransformOp operation = windowState.gizmoOperation == GizmoOperation::Rotate
			? ModalTransformOp::Rotate
			: windowState.gizmoOperation == GizmoOperation::Scale
				? ModalTransformOp::Scale
				: ModalTransformOp::Translate;
		snapshot = CaptureSceneTransformSelectionForOperation(windowState, operation);
		const std::vector<glm::vec3> anchorPositions = SceneTransformAnchorPositions(windowState, snapshot);
		const std::optional<glm::vec3> cursor = windowState.cursor3DPlaced
			? std::optional<glm::vec3>(windowState.cursor3DPosition)
			: std::nullopt;
		const glm::vec3 gizmoPosition = ComputeTransformPivot(windowState.transformPivotMode, anchorPositions, cursor);
		const glm::mat4 viewProjection = windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		const std::optional<glm::vec2> gizmoScreen = ProjectAbsolute(viewProjection, imageOrigin, imageSize, gizmoPosition);
		if (!gizmoScreen.has_value())
			return false;

		// Atom picking must not swallow a handle belonging to the selected atom(s). Scene-object
		// gizmos keep the old overlap guard; path-edit markers already get first refusal in the chain.
		const bool pointerOnGeometry = !singleArrowOnly && !HasAtomTransformTargets(snapshot) &&
			IsAtomOrBondUnderMouse(windowState, imageOrigin, mouse);
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
			const int hoveredAxis = pointerOnGeometry ? -1 : HitTestRotateArcs(geometry, mouse);
			const float radial = glm::length(mouse - *gizmoScreen);
			const bool hoveringTrackball = hovered && !pointerOnGeometry &&
				radial >= geometry.trackballRadius - 7.0f * gizmoScale &&
				radial <= geometry.trackballRadius + 7.0f * gizmoScale;
			DrawRotateGizmo(geometry, *gizmoScreen, hoveredAxis, hoveringTrackball, imageOrigin, imageSize);
			if (hovered && !pointerOnGeometry && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				(hoveredAxis >= 0 || hoveringTrackball))
			{
				BeginViewportModalTransform(
					windowState, ModalTransformOp::Rotate, mouse,
					hoveredAxis >= 0 ? std::optional<int>(hoveredAxis) : std::nullopt, true);
				return true;
			}
			windowState.gizmoDragActive = false;
			return hovered && !pointerOnGeometry && (hoveredAxis >= 0 || hoveringTrackball);
		}
		if (windowState.gizmoOperation == GizmoOperation::Scale)
		{
			const float radial = glm::length(mouse - *gizmoScreen);
			const bool hoveringCenter = hovered && !pointerOnGeometry && radial <= kScaleCenterRadius * gizmoScale;
			const int hoveredAxis = pointerOnGeometry || hoveringCenter
				? -1 : HitTestAxis(mouse, *gizmoScreen, axes, gizmoScale);
			DrawScaleGizmo(*gizmoScreen, axes, hoveredAxis, hoveringCenter, imageOrigin, imageSize, gizmoScale);
			if (hovered && !pointerOnGeometry && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
				(hoveredAxis >= 0 || hoveringCenter))
			{
				BeginViewportModalTransform(
					windowState, ModalTransformOp::Scale, mouse,
					hoveredAxis >= 0 ? std::optional<int>(hoveredAxis) : std::nullopt, true);
				return true;
			}
			windowState.gizmoDragActive = false;
			return hovered && !pointerOnGeometry && (hoveredAxis >= 0 || hoveringCenter);
		}

		std::array<glm::vec2, 3> axisDirections{};
		for (int axis = 0; axis < 3; ++axis)
			if (axes[axis].valid)
				axisDirections[axis] = axes[axis].direction;
		auto planes = BuildTranslatePlaneGeometry(*gizmoScreen, axisDirections);
		ScaleTranslatePlaneGeometry(planes, *gizmoScreen, gizmoScale);
		const int hoveredAxis = pointerOnGeometry ? -1 : HitTestAxis(mouse, *gizmoScreen, axes, gizmoScale);
		const int hoveredPlaneAxis = pointerOnGeometry || hoveredAxis >= 0
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
