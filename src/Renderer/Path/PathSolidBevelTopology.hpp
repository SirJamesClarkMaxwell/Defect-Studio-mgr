#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "Renderer/Path/PathSolidMesher.hpp"

namespace DefectStudio::detail
{
	using ThickFlatBevelEdgeKey = std::pair<std::uint32_t, std::uint32_t>;

	enum class ThickFlatBevelTurn : std::uint8_t
	{
		Convex,
		Reflex,
		Smooth,
		Unsupported,
	};

	struct ThickFlatBevelIncident
	{
		std::size_t face = 0u;
		std::size_t corner = 0u;
	};

	struct ThickFlatBevelEdge
	{
		ThickFlatBevelEdgeKey key{};
		std::vector<ThickFlatBevelIncident> incidents;
		ThickFlatBevelTurn turn = ThickFlatBevelTurn::Unsupported;
		bool bevel = false;
	};

	struct ThickFlatBevelVertex
	{
		std::vector<std::size_t> edges;
		std::vector<std::size_t> orderedEdges;
		ThickFlatBevelTurn turn = ThickFlatBevelTurn::Unsupported;
		double radius = 0.0;
	};

	struct ThickFlatBevelTopology
	{
		std::vector<ThickFlatBevelEdge> edges;
		std::map<ThickFlatBevelEdgeKey, std::size_t> edgeByKey;
		std::map<std::uint32_t, ThickFlatBevelVertex> vertices;
		std::vector<std::vector<ThickFlatBevelTurn>> faceCorners;
	};

	[[nodiscard]] bool BuildThickFlatBevelTopology(const ThickFlatMesh &mesh,
		std::span<const glm::dvec3> faceNormals, double requestedRadius,
		ThickFlatBevelTopology &topology);
}
