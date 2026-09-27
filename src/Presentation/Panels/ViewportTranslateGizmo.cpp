#include "Core/dspch.hpp"

#include "Presentation/Panels/ViewportTranslateGizmo.hpp"

#include <cmath>

#include "Presentation/Panels/ViewportGizmo.hpp"

namespace DefectStudio
{
	namespace
	{
		constexpr float kPlaneOffset = 34.0f;
		constexpr float kPlaneHalfExtent = 7.0f;

		[[nodiscard]] float Cross2(const glm::vec2 &first, const glm::vec2 &second)
		{
			return first.x * second.y - first.y * second.x;
		}
	} // namespace

	std::array<TranslatePlaneGeometry, 3> BuildTranslatePlaneGeometry(
		const glm::vec2 &pivot, const std::array<glm::vec2, 3> &axisDirections)
	{
		std::array<TranslatePlaneGeometry, 3> planes;
		for (int normalAxis = 0; normalAxis < 3; ++normalAxis)
		{
			const int firstAxis = (normalAxis + 1) % 3;
			const int secondAxis = (normalAxis + 2) % 3;
			if (glm::dot(axisDirections[firstAxis], axisDirections[firstAxis]) <= 1.0e-8f ||
				glm::dot(axisDirections[secondAxis], axisDirections[secondAxis]) <= 1.0e-8f)
				continue;
			const glm::vec2 bisector = axisDirections[firstAxis] + axisDirections[secondAxis];
			if (glm::dot(bisector, bisector) <= 1.0e-8f)
				continue;
			const glm::vec2 center = pivot + glm::normalize(bisector) * kPlaneOffset;
			const glm::vec2 first = axisDirections[firstAxis] * kPlaneHalfExtent;
			const glm::vec2 second = axisDirections[secondAxis] * kPlaneHalfExtent;
			planes[normalAxis].corners = {
				center - first - second, center + first - second,
				center + first + second, center - first + second};
			planes[normalAxis].valid = true;
		}
		return planes;
	}

	int HitTestTranslatePlaneHandles(
		const glm::vec2 &mouse, const std::array<TranslatePlaneGeometry, 3> &planes)
	{
		for (int normalAxis = 0; normalAxis < 3; ++normalAxis)
		{
			if (!planes[normalAxis].valid)
				continue;
			const auto &corners = planes[normalAxis].corners;
			const glm::vec2 first = corners[1] - corners[0];
			const glm::vec2 second = corners[3] - corners[0];
			const float determinant = Cross2(first, second);
			if (std::abs(determinant) <= 1.0e-8f)
				continue;
			const glm::vec2 offset = mouse - corners[0];
			const float firstCoordinate = Cross2(offset, second) / determinant;
			const float secondCoordinate = Cross2(first, offset) / determinant;
			if (firstCoordinate >= 0.0f && firstCoordinate <= 1.0f &&
				secondCoordinate >= 0.0f && secondCoordinate <= 1.0f)
				return normalAxis;
		}
		return -1;
	}

	void DrawTranslatePlaneHandles(
		const std::array<TranslatePlaneGeometry, 3> &planes,
		const int hoveredPlaneAxis,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize)
	{
		ImDrawList &drawList = *ImGui::GetWindowDrawList();
		drawList.PushClipRect(
			imageOrigin, ImVec2(imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y), true);
		for (int normalAxis = 0; normalAxis < 3; ++normalAxis)
		{
			if (!planes[normalAxis].valid)
				continue;
			const ImU32 color = normalAxis == hoveredPlaneAxis
				? IM_COL32(255, 200, 60, 220)
				: ViewportTransformAxisColor(normalAxis);
			const auto &corners = planes[normalAxis].corners;
			drawList.AddQuadFilled(
				ImVec2(corners[0].x, corners[0].y), ImVec2(corners[1].x, corners[1].y),
				ImVec2(corners[2].x, corners[2].y), ImVec2(corners[3].x, corners[3].y), color);
			drawList.AddQuad(
				ImVec2(corners[0].x, corners[0].y), ImVec2(corners[1].x, corners[1].y),
				ImVec2(corners[2].x, corners[2].y), ImVec2(corners[3].x, corners[3].y),
				IM_COL32(30, 30, 30, 220), 1.5f);
		}
		drawList.PopClipRect();
	}
} // namespace DefectStudio
