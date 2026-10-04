#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Renderer/Path/PathSolidBevelTopology.hpp"

namespace DefectStudio::detail
{
	struct ThickFlatBevelOutput
	{
		std::vector<StrokeTubeVertex> vertices;
		std::vector<std::uint32_t> indices;
	};

	[[nodiscard]] bool BuildThickFlatBevelInsets(const ThickFlatMesh &mesh,
		std::span<const glm::dvec3> normals, ThickFlatBevelTopology &topology,
		std::vector<std::vector<glm::dvec3>> &insets);

	void EmitThickFlatBevelPolygon(ThickFlatBevelOutput &output, const ThickFlatMesh &mesh,
		std::vector<std::uint32_t> sources, std::vector<glm::dvec3> positions,
		glm::dvec3 expectedNormal);

	void EmitThickFlatPinchedBevelPatch(ThickFlatBevelOutput &output, const ThickFlatMesh &mesh,
		std::uint32_t source, const std::vector<glm::dvec3> &boundary, glm::dvec3 expectedNormal);

	void SmoothThickFlatBevelNormals(StrokeGeometry &geometry);
}
