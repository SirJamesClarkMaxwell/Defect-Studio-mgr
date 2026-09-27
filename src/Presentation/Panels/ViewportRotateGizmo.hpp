#pragma once

#include <array>
#include <cstddef>

#include <glm/glm.hpp>
#include <imgui.h>

namespace DefectStudio
{
	class RendererViewCamera;

	struct RotateArcGeometry
	{
		static constexpr std::size_t PointCount = 65;

		std::array<glm::vec2, PointCount> points{};
		std::array<bool, PointCount - 1> visibleSegments{};
		bool valid = false;
	};

	struct RotateGizmoGeometry
	{
		std::array<RotateArcGeometry, 3> arcs{};
		float trackballRadius = 100.0f;
	};

	[[nodiscard]] RotateGizmoGeometry BuildRotateGizmoGeometry(
		const RendererViewCamera &camera,
		const glm::mat4 &viewProjection,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize,
		const glm::vec3 &pivotWorld,
		const glm::vec2 &pivotScreen,
		const std::array<glm::vec3, 3> &worldAxes);

	[[nodiscard]] int HitTestRotateArcs(const RotateGizmoGeometry &geometry, const glm::vec2 &mouse);

	void DrawRotateGizmo(
		const RotateGizmoGeometry &geometry,
		const glm::vec2 &pivot,
		int hoveredAxis,
		bool hoveringTrackball,
		const ImVec2 &imageOrigin,
		const ImVec2 &imageSize);
} // namespace DefectStudio
