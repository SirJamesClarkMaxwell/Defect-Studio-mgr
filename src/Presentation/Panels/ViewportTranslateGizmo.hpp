#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>
#include <imgui.h>

namespace DefectStudio
{
	struct TranslatePlaneGeometry
	{
		std::array<glm::vec2, 4> corners{};
		bool valid = false;
	};

	[[nodiscard]] std::array<TranslatePlaneGeometry, 3> BuildTranslatePlaneGeometry(
		const glm::vec2 &pivot, const std::array<glm::vec2, 3> &axisDirections);

	[[nodiscard]] int HitTestTranslatePlaneHandles(
		const glm::vec2 &mouse, const std::array<TranslatePlaneGeometry, 3> &planes);

	void DrawTranslatePlaneHandles(
		const std::array<TranslatePlaneGeometry, 3> &planes,
		int hoveredPlaneAxis,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize);
} // namespace DefectStudio
