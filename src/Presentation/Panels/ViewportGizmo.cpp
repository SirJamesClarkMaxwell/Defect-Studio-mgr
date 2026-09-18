#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportGizmo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

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

		void DrawAxisHandles(const glm::vec2 &pivot, const std::array<AxisProjection, 3> &axes)
		{
			ImDrawList &drawList = *ImGui::GetWindowDrawList();
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axes[axis].valid)
					continue;
				const glm::vec2 direction = axes[axis].direction;
				const glm::vec2 perpendicular(-direction.y, direction.x);
				const glm::vec2 tip = pivot + direction * kPickMaxDistance;
				const glm::vec2 headBase = tip - direction * 16.0f;
				drawList.AddLine(ImVec2(pivot.x, pivot.y), ImVec2(headBase.x, headBase.y), kAxisColors[axis], 3.5f);
				drawList.AddTriangleFilled(
					ImVec2(tip.x, tip.y), ImVec2(headBase.x + perpendicular.x * 6.0f, headBase.y + perpendicular.y * 6.0f),
					ImVec2(headBase.x - perpendicular.x * 6.0f, headBase.y - perpendicular.y * 6.0f), kAxisColors[axis]);
			}
			drawList.AddCircleFilled(ImVec2(pivot.x, pivot.y), 5.0f, IM_COL32(235, 235, 235, 255));
		}

		[[nodiscard]] int HitTestAxis(
			const glm::vec2 &mouse, const glm::vec2 &pivot, const std::array<AxisProjection, 3> &axes)
		{
			const glm::vec2 fromPivot = mouse - pivot;
			const float radial = glm::length(fromPivot);
			if (radial < kPickMinDistance || radial > kPickMaxDistance)
				return -1;
			float bestDistance = 16.0f;
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

	} // namespace

	bool RenderTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered,
		RendererLayer &layer, const WeakRef<CommandRegistry> &commandRegistryRef)
	{
		if (UpdateViewportModalTransform(windowState, imageOrigin, imageSize, layer, commandRegistryRef))
			return true;
		if (windowState.camera == nullptr)
			return false;

		SceneTransformSelectionSnapshot snapshot = CaptureSceneTransformSelection(windowState);
		if (SceneTransformPivotPositions(snapshot).empty())
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

		const ModalTransformOp operation = windowState.gizmoOperation == GizmoOperation::Rotate
			? ModalTransformOp::Rotate
			: windowState.gizmoOperation == GizmoOperation::Scale
				? ModalTransformOp::Scale
				: ModalTransformOp::Translate;
		snapshot = CaptureSceneTransformSelectionForOperation(windowState, operation);
		const std::vector<glm::vec3> positions = SceneTransformPivotPositions(snapshot);
		const std::optional<glm::vec3> cursor = windowState.cursor3DPlaced
			? std::optional<glm::vec3>(windowState.cursor3DPosition)
			: std::nullopt;
		const glm::vec3 pivot = ComputeTransformPivot(windowState.transformPivotMode, positions, cursor);
		const glm::mat4 viewProjection =
			windowState.camera->ProjectionMatrix() * windowState.camera->ViewMatrix();
		const std::optional<glm::vec2> pivotScreen = ProjectAbsolute(viewProjection, imageOrigin, imageSize, pivot);
		if (!pivotScreen.has_value())
			return false;

		const glm::vec2 mouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		if (singleArrowOnly && windowState.gizmoOperation == GizmoOperation::Translate)
		{
			using Target = RendererWindowState::SceneArrowDragTarget;
			const ArrowTransformStart &arrow = snapshot.arrows.front();
			const std::array<std::pair<glm::vec3, Target>, 3> points = {{
				{arrow.points.front(), Target::Start}, {arrow.points.back(), Target::End},
				{(arrow.points.front() + arrow.points.back()) * 0.5f, Target::Both}}};
			for (const auto &[world, target] : points)
			{
				if (target == windowState.sceneArrowGizmoActiveTarget)
					continue;
				const std::optional<glm::vec2> screen = ProjectAbsolute(viewProjection, imageOrigin, imageSize, world);
				if (!screen.has_value())
					continue;
				ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(screen->x, screen->y), 5.0f, IM_COL32(190, 190, 190, 190));
				if (hovered && glm::distance(mouse, *screen) <= 10.0f)
				{
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
						windowState.sceneArrowGizmoActiveTarget = target;
					return true;
				}
			}
		}

		const bool pointerOnGeometry = IsAtomOrBondUnderMouse(windowState, imageOrigin, mouse);
		if (windowState.gizmoOperation == GizmoOperation::Rotate)
		{
			const float radial = glm::length(mouse - *pivotScreen);
			const bool hoveringRing = hovered && !pointerOnGeometry &&
				radial >= kPickMinDistance && radial <= kPickMaxDistance;
			ImGui::GetWindowDrawList()->AddCircle(
				ImVec2(pivotScreen->x, pivotScreen->y), kPickMaxDistance,
				hoveringRing ? IM_COL32(255, 200, 60, 220) : IM_COL32(235, 235, 235, 200), 48, 2.5f);
			if (hoveringRing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				BeginViewportModalTransform(windowState, ModalTransformOp::Rotate, mouse, std::nullopt, true);
				return true;
			}
			return hoveringRing;
		}

		TransformBases bases;
		bases.local = SceneTransformLocalBasis(snapshot);
		bases.lattice = windowState.structure.lattice;
		const OrientationAxes worldAxes = ResolveNormalizedOrientationAxes(windowState.transformOrientation, bases);
		const std::array<AxisProjection, 3> axes =
			ProjectAxes(viewProjection, imageOrigin, imageSize, pivot, *pivotScreen, worldAxes);
		DrawAxisHandles(*pivotScreen, axes);
		const int hoveredAxis = pointerOnGeometry ? -1 : HitTestAxis(mouse, *pivotScreen, axes);
		if (hovered && hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			BeginViewportModalTransform(windowState, operation, mouse, hoveredAxis, true);
			return true;
		}

		windowState.gizmoDragActive = false;
		return hoveredAxis >= 0;
	}
} // namespace DefectStudio
