#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportRotateGizmo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/gtc/constants.hpp>

#include "Presentation/Panels/ViewportGizmo.hpp"
#include "Renderer/RendererViewCamera.hpp"
#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kRotateAxisRadius = 72.0f;
		constexpr float kRotateAxisHitDistance = 10.0f;
		constexpr float kTrackballRadius = 100.0f;

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

		[[nodiscard]] glm::vec3 CircleBasis(const glm::vec3 &axis, const glm::vec3 &reference)
		{
			glm::vec3 basis = glm::cross(axis, reference);
			if (glm::dot(basis, basis) <= 1.0e-8f)
				basis = glm::cross(axis, glm::vec3(0.0f, 1.0f, 0.0f));
			return glm::normalize(basis);
		}

		[[nodiscard]] float DistanceToSegment(
			const glm::vec2 &point, const glm::vec2 &start, const glm::vec2 &end)
		{
			const glm::vec2 segment = end - start;
			const float lengthSquared = glm::dot(segment, segment);
			if (lengthSquared <= 1.0e-8f)
				return glm::distance(point, start);
			const float t = std::clamp(glm::dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
			return glm::distance(point, start + segment * t);
		}
	} // namespace

	RotateGizmoGeometry BuildRotateGizmoGeometry(
		const RendererViewCamera &camera,
		const glm::mat4 &viewProjection,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize,
		const glm::vec3 &pivotWorld,
		const glm::vec2 &pivotScreen,
		const std::array<glm::vec3, 3> &worldAxes)
	{
		RotateGizmoGeometry geometry;
		geometry.trackballRadius = kTrackballRadius;
		const glm::mat4 view = camera.ViewMatrix();
		const glm::vec3 cameraRight = glm::normalize(glm::vec3(view[0][0], view[1][0], view[2][0]));
		const std::optional<glm::vec2> probe = ProjectAbsolute(
			viewProjection, imageOrigin, imageSize, pivotWorld + cameraRight);
		if (!probe.has_value())
			return geometry;

		const float pixelsPerWorld = glm::distance(*probe, pivotScreen);
		if (pixelsPerWorld <= 1.0e-5f)
			return geometry;
		const float worldRadius = kRotateAxisRadius / pixelsPerWorld;
		const glm::vec3 cameraFromPivot = camera.Position() - pivotWorld;

		for (int axis = 0; axis < 3; ++axis)
		{
			RotateArcGeometry &arc = geometry.arcs[axis];
			const glm::vec3 &normal = worldAxes[axis];
			if (glm::dot(normal, normal) <= 1.0e-8f || glm::dot(cameraFromPivot, cameraFromPivot) <= 1.0e-8f)
				continue;

			const glm::vec3 first = CircleBasis(normal, std::abs(normal.z) < 0.9f
				? glm::vec3(0.0f, 0.0f, 1.0f)
				: glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::vec3 second = glm::normalize(glm::cross(normal, first));
			bool projected = true;
			for (std::size_t point = 0; point < RotateArcGeometry::PointCount; ++point)
			{
				const float angle = glm::two_pi<float>() * static_cast<float>(point) /
					static_cast<float>(RotateArcGeometry::PointCount - 1);
				const glm::vec3 worldPoint = pivotWorld + worldRadius *
					(first * std::cos(angle) + second * std::sin(angle));
				const std::optional<glm::vec2> screen =
					ProjectAbsolute(viewProjection, imageOrigin, imageSize, worldPoint);
				if (!screen.has_value())
				{
					projected = false;
					break;
				}
				arc.points[point] = *screen;
			}
			if (!projected)
				continue;

			arc.valid = true;
			for (std::size_t point = 0; point + 1 < RotateArcGeometry::PointCount; ++point)
			{
				const float firstAngle = glm::two_pi<float>() * static_cast<float>(point) /
					static_cast<float>(RotateArcGeometry::PointCount - 1);
				const float secondAngle = glm::two_pi<float>() * static_cast<float>(point + 1) /
					static_cast<float>(RotateArcGeometry::PointCount - 1);
				const glm::vec3 firstOffset = worldRadius *
					(first * std::cos(firstAngle) + second * std::sin(firstAngle));
				const glm::vec3 secondOffset = worldRadius *
					(first * std::cos(secondAngle) + second * std::sin(secondAngle));
				arc.visibleSegments[point] = glm::dot(firstOffset, cameraFromPivot) >= 0.0f &&
					glm::dot(secondOffset, cameraFromPivot) >= 0.0f;
			}
		}
		return geometry;
	}

	int HitTestRotateArcs(const RotateGizmoGeometry &geometry, const glm::vec2 &mouse)
	{
		int bestAxis = -1;
		float bestDistance = std::numeric_limits<float>::max();
		for (int axis = 0; axis < 3; ++axis)
		{
			const RotateArcGeometry &arc = geometry.arcs[axis];
			if (!arc.valid)
				continue;
			for (std::size_t point = 0; point + 1 < RotateArcGeometry::PointCount; ++point)
			{
				if (!arc.visibleSegments[point])
					continue;
				const float distance = DistanceToSegment(mouse, arc.points[point], arc.points[point + 1]);
				if (distance < kRotateAxisHitDistance && distance < bestDistance)
					bestDistance = distance, bestAxis = axis;
			}
		}
		return bestAxis;
	}

	void DrawRotateGizmo(
		const RotateGizmoGeometry &geometry,
		const glm::vec2 &pivot,
		const int hoveredAxis,
		const bool hoveringTrackball,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize)
	{
		ImDrawList &drawList = *ImGui::GetWindowDrawList();
		drawList.PushClipRect(
			imageOrigin, ImVec2(imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y), true);
		drawList.AddCircle(
			ImVec2(pivot.x, pivot.y), geometry.trackballRadius,
			hoveringTrackball ? IM_COL32(255, 200, 60, 220) : IM_COL32(235, 235, 235, 220), 64, 2.5f);
		for (int axis = 0; axis < 3; ++axis)
		{
			const RotateArcGeometry &arc = geometry.arcs[axis];
			if (!arc.valid)
				continue;
			const ImU32 color = axis == hoveredAxis ? IM_COL32(255, 200, 60, 240) : ViewportTransformAxisColor(axis);
			for (std::size_t point = 0; point + 1 < RotateArcGeometry::PointCount; ++point)
			{
				if (arc.visibleSegments[point])
					drawList.AddLine(
						ImVec2(arc.points[point].x, arc.points[point].y),
						ImVec2(arc.points[point + 1].x, arc.points[point + 1].y), color, 3.0f);
			}
		}
		drawList.AddCircleFilled(ImVec2(pivot.x, pivot.y), 5.0f, IM_COL32(235, 235, 235, 255));
		drawList.PopClipRect();
	}
} // namespace DefectStudio
