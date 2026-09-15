#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportGizmo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ImGuizmo.h>

#include "Presentation/Panels/ViewportModalTransform.hpp"
#include "Presentation/Panels/ViewportOrientationTriad.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/RendererWindowState.hpp"
#include "Renderer/Scene/ModalTransform.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kPickMinDistance = 20.0f;
		constexpr float kPickMaxDistance = 130.0f;
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

		[[nodiscard]] bool IsAtomUnderScreenPosition(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPosition)
		{
			const std::optional<Ray> ray = MakeScreenRay(windowState, imageOrigin, screenPosition);
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
				if (discriminant >= 0.0f && (-b - std::sqrt(discriminant)) * 0.5f > 0.001f)
					return true;
			}
			return false;
		}

		[[nodiscard]] bool IsBondUnderScreenPosition(
			const RendererWindowState &windowState, const ImVec2 &imageOrigin, const glm::vec2 &screenPosition)
		{
			const std::optional<Ray> ray = MakeScreenRay(windowState, imageOrigin, screenPosition);
			if (!ray.has_value())
				return false;
			for (const RendererBondData &bond : windowState.structure.bonds)
			{
				if (!bond.visible || bond.firstAtomIndex >= windowState.structure.atoms.size() ||
					bond.secondAtomIndex >= windowState.structure.atoms.size())
					continue;
				const RendererAtomData &first = windowState.structure.atoms[bond.firstAtomIndex];
				const RendererAtomData &second = windowState.structure.atoms[bond.secondAtomIndex];
				if (!first.visible || !second.visible)
					continue;

				float rayDistance = 0.0f;
				glm::vec3 closestOnBond(0.0f);
				SelectionHitTest::ClosestPointsRaySegment(
					ray->origin, ray->direction, first.cartesianPosition,
					second.cartesianPosition + bond.secondAtomPeriodicOffset, rayDistance, closestOnBond);
				if (rayDistance <= 0.001f)
					continue;
				const float pickRadius = std::max(bond.radius * 2.5f, 0.12f);
				if (glm::distance(ray->origin + ray->direction * rayDistance, closestOnBond) <= pickRadius)
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

		[[nodiscard]] std::optional<glm::vec3> SelectionPivot(const RendererWindowState &windowState)
		{
			std::vector<glm::vec3> positions;
			positions.reserve(windowState.selectedAtomIndices.size());
			for (const std::size_t atomIndex : windowState.selectedAtomIndices)
				if (atomIndex < windowState.structure.atoms.size())
					positions.push_back(windowState.structure.atoms[atomIndex].cartesianPosition);
			if (positions.empty())
				return std::nullopt;
			const std::optional<glm::vec3> cursor = windowState.cursor3DPlaced
				? std::optional<glm::vec3>(windowState.cursor3DPosition)
				: std::nullopt;
			return ComputeTransformPivot(windowState.transformPivotMode, positions, cursor);
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
				if (glm::dot(projected, projected) <= 1.0f)
					continue;
				axes[axis] = AxisProjection{glm::normalize(projected), true};
			}
			return axes;
		}

		void DrawAxisHandles(const glm::vec2 &pivot, const std::array<AxisProjection, 3> &axes)
		{
			ImDrawList &drawList = *ImGui::GetWindowDrawList();
			constexpr float headLength = 16.0f;
			constexpr float headHalfWidth = 6.0f;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axes[axis].valid)
					continue;
				const glm::vec2 direction = axes[axis].direction;
				const glm::vec2 perpendicular(-direction.y, direction.x);
				const glm::vec2 tip = pivot + direction * kPickMaxDistance;
				const glm::vec2 headBase = tip - direction * headLength;
				const glm::vec2 headLeft = headBase + perpendicular * headHalfWidth;
				const glm::vec2 headRight = headBase - perpendicular * headHalfWidth;
				drawList.AddLine(ImVec2(pivot.x, pivot.y), ImVec2(headBase.x, headBase.y), kAxisColors[axis], 3.5f);
				drawList.AddTriangleFilled(
					ImVec2(tip.x, tip.y), ImVec2(headLeft.x, headLeft.y), ImVec2(headRight.x, headRight.y),
					kAxisColors[axis]);
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

			constexpr float pickTolerance = 16.0f;
			float bestDistance = pickTolerance;
			int bestAxis = -1;
			for (int axis = 0; axis < 3; ++axis)
			{
				if (!axes[axis].valid)
					continue;
				const float along = glm::dot(fromPivot, axes[axis].direction);
				if (along <= 0.0f)
					continue;
				const float distance = glm::length(fromPivot - axes[axis].direction * along);
				if (distance < bestDistance)
				{
					bestDistance = distance;
					bestAxis = axis;
				}
			}
			return bestAxis;
		}

		void ApplyTrackballRotation(
			RendererWindowState &windowState, const glm::mat4 &view, const glm::vec3 &pivot,
			const glm::vec2 &mouse)
		{
			const glm::vec3 cameraRight(view[0][0], view[1][0], view[2][0]);
			const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);
			const glm::vec3 cameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
			const glm::vec2 screenDelta = mouse - windowState.fallbackLastMousePos;
			windowState.fallbackLastMousePos = mouse;
			const glm::vec3 dragDirection = cameraRight * screenDelta.x - cameraUp * screenDelta.y;
			if (glm::dot(dragDirection, dragDirection) <= 1.0e-8f)
				return;

			const glm::vec3 rotationAxis = glm::normalize(glm::cross(cameraForward, dragDirection));
			const glm::quat rotation = glm::angleAxis(glm::length(screenDelta) * 0.006f, rotationAxis);
			for (const std::size_t atomIndex : windowState.selectedAtomIndices)
			{
				if (atomIndex < windowState.structure.atoms.size())
				{
					RendererAtomData &atom = windowState.structure.atoms[atomIndex];
					atom.cartesianPosition = pivot + rotation * (atom.cartesianPosition - pivot);
				}
			}
		}
	} // namespace

	bool RenderTransformGizmo(
		RendererWindowState &windowState, const ImVec2 &imageOrigin, const ImVec2 &imageSize, bool hovered,
		const WeakRef<CommandRegistry> &commandRegistryRef)
	{
		if (UpdateAtomModalTransform(windowState, imageOrigin, imageSize, commandRegistryRef))
			return true;

		const std::optional<glm::vec3> pivot = SelectionPivot(windowState);
		if (!pivot.has_value() || windowState.camera == nullptr)
		{
			windowState.gizmoDragActive = false;
			windowState.fallbackGizmoDragging = false;
			windowState.fallbackGizmoAxis = -1;
			return false;
		}

		const glm::mat4 view = windowState.camera->ViewMatrix();
		const glm::mat4 projection = windowState.camera->ProjectionMatrix();
		const glm::mat4 viewProjection = projection * view;
		const std::optional<glm::vec2> pivotScreen =
			ProjectAbsolute(viewProjection, imageOrigin, imageSize, *pivot);
		if (!pivotScreen.has_value())
			return false;

		ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
		ModalTransformOp modalOperation = ModalTransformOp::Translate;
		if (windowState.gizmoOperation == GizmoOperation::Rotate)
		{
			operation = ImGuizmo::ROTATE;
			modalOperation = ModalTransformOp::Rotate;
		}
		else if (windowState.gizmoOperation == GizmoOperation::Scale)
		{
			operation = ImGuizmo::SCALE;
			modalOperation = ModalTransformOp::Scale;
		}

		glm::mat4 gizmoMatrix = glm::translate(glm::mat4(1.0f), *pivot);
		glm::mat4 deltaMatrix(1.0f);
		ImGuizmo::PushID(windowState.windowId.c_str());
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetOrthographic(windowState.camera->Projection() == CameraProjection::Orthographic);
		ImGuizmo::Enable(false);
		ImGuizmo::SetRect(imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y);
		if (operation == ImGuizmo::ROTATE)
		{
			ImGuizmo::Manipulate(
				glm::value_ptr(view), glm::value_ptr(projection), operation, ImGuizmo::WORLD,
				glm::value_ptr(gizmoMatrix), glm::value_ptr(deltaMatrix));
		}
		ImGuizmo::PopID();

		const glm::vec2 mouse(ImGui::GetMousePos().x, ImGui::GetMousePos().y);
		const bool pointerOnGeometry = !windowState.fallbackGizmoDragging &&
			(IsAtomUnderScreenPosition(windowState, imageOrigin, mouse) ||
				IsBondUnderScreenPosition(windowState, imageOrigin, mouse));
		if (operation == ImGuizmo::ROTATE)
		{
			const float radial = glm::length(mouse - *pivotScreen);
			const bool hoveringRing = !pointerOnGeometry && radial >= kPickMinDistance && radial <= kPickMaxDistance;
			if (!windowState.fallbackGizmoDragging && hoveringRing && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				windowState.fallbackGizmoDragging = true;
				windowState.fallbackGizmoAxis = -2;
				windowState.fallbackLastMousePos = mouse;
				windowState.gizmoDragActive = true;
			}
			if (windowState.fallbackGizmoDragging && windowState.fallbackGizmoAxis == -2)
			{
				ImGui::GetIO().WantCaptureKeyboard = true;
				if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					ApplyTrackballRotation(windowState, view, *pivot, mouse);
					windowState.gizmoDragActive = true;
					return true;
				}
				windowState.fallbackGizmoDragging = false;
				windowState.fallbackGizmoAxis = -1;
				windowState.gizmoDragActive = false;
				CommitAtomGizmoPreview(windowState, commandRegistryRef, "Rotate selected atoms");
				return true;
			}
			windowState.gizmoDragActive = false;
			return hoveringRing;
		}

		TransformBases bases;
		bases.lattice = windowState.structure.lattice;
		const OrientationAxes worldAxes = ResolveNormalizedOrientationAxes(windowState.transformOrientation, bases);
		const std::array<AxisProjection, 3> axes =
			ProjectAxes(viewProjection, imageOrigin, imageSize, *pivot, *pivotScreen, worldAxes);
		DrawAxisHandles(*pivotScreen, axes);
		const int hoveredAxis = pointerOnGeometry ? -1 : HitTestAxis(mouse, *pivotScreen, axes);
		if (hovered && hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			BeginAtomModalTransform(windowState, modalOperation, mouse, hoveredAxis, true);
			return true;
		}

		windowState.gizmoDragActive = false;
		return hoveredAxis >= 0;
	}
} // namespace DefectStudio
