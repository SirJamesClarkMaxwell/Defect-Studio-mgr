#include "Core/dspch.hpp"

#include "Renderer/Path/PathSolidBevelTopology.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace DefectStudio::detail
{
	namespace
	{
		constexpr double kLengthToleranceSquared = 1.0e-20;
		constexpr double kTurnTolerance = 0.25881904510252074; // sin(15 degrees)

		[[nodiscard]] ThickFlatBevelEdgeKey MakeEdgeKey(const std::uint32_t first,
			const std::uint32_t second)
		{
			return first < second ? ThickFlatBevelEdgeKey{first, second} : ThickFlatBevelEdgeKey{second, first};
		}

		void AddUnique(std::vector<std::size_t> &values, const std::size_t value)
		{
			if (std::find(values.begin(), values.end(), value) == values.end())
				values.push_back(value);
		}

		[[nodiscard]] std::size_t EdgeId(const ThickFlatBevelTopology &topology,
			const ThickFlatBevelEdgeKey key)
		{
			const auto found = topology.edgeByKey.find(key);
			return found == topology.edgeByKey.end() ? std::numeric_limits<std::size_t>::max() : found->second;
		}

		[[nodiscard]] ThickFlatBevelTurn ClassifyTurn(const glm::dvec3 &incoming,
			const glm::dvec3 &outgoing, const glm::dvec3 &axis)
		{
			const double incomingLengthSquared = glm::dot(incoming, incoming);
			const double outgoingLengthSquared = glm::dot(outgoing, outgoing);
			const double axisLengthSquared = glm::dot(axis, axis);
			if (incomingLengthSquared <= kLengthToleranceSquared || outgoingLengthSquared <= kLengthToleranceSquared ||
				axisLengthSquared <= kLengthToleranceSquared)
				return ThickFlatBevelTurn::Unsupported;
			const glm::dvec3 first = incoming / std::sqrt(incomingLengthSquared);
			const glm::dvec3 second = outgoing / std::sqrt(outgoingLengthSquared);
			const glm::dvec3 turnAxis = axis / std::sqrt(axisLengthSquared);
			const double sine = glm::dot(turnAxis, glm::cross(first, second));
			const double cosine = std::clamp(glm::dot(first, second), -1.0, 1.0);
			if (std::abs(sine) <= kTurnTolerance)
				return cosine > 0.0 ? ThickFlatBevelTurn::Smooth : ThickFlatBevelTurn::Unsupported;
			return sine > 0.0 ? ThickFlatBevelTurn::Convex : ThickFlatBevelTurn::Reflex;
		}

		[[nodiscard]] ThickFlatBevelTurn MergeTurn(const ThickFlatBevelTurn current,
			const ThickFlatBevelTurn incoming)
		{
			if (current == ThickFlatBevelTurn::Unsupported || incoming == ThickFlatBevelTurn::Unsupported)
				return ThickFlatBevelTurn::Unsupported;
			if (current == ThickFlatBevelTurn::Reflex || incoming == ThickFlatBevelTurn::Reflex)
				return ThickFlatBevelTurn::Reflex;
			if (current == ThickFlatBevelTurn::Smooth || incoming == ThickFlatBevelTurn::Smooth)
				return ThickFlatBevelTurn::Smooth;
			if (current == ThickFlatBevelTurn::Convex || incoming == ThickFlatBevelTurn::Convex)
				return ThickFlatBevelTurn::Convex;
			return ThickFlatBevelTurn::Unsupported;
		}
	} // namespace

	bool BuildThickFlatBevelTopology(const ThickFlatMesh &mesh,
		const std::span<const glm::dvec3> faceNormals, const double requestedRadius,
		ThickFlatBevelTopology &topology)
	{
		if (faceNormals.size() != mesh.faces.size() || !std::isfinite(requestedRadius) || requestedRadius <= 0.0)
			return false;
		topology = {};
		topology.faceCorners.resize(mesh.faces.size());
		std::vector<std::vector<std::pair<std::size_t, std::size_t>>> transitions(mesh.vertices.size());

		for (std::size_t faceIndex = 0u; faceIndex < mesh.faces.size(); ++faceIndex)
		{
			const ThickFlatMeshFace &face = mesh.faces[faceIndex];
			if (face.vertices.size() < 3u || face.bevelEdges.size() != face.vertices.size() ||
				glm::dot(faceNormals[faceIndex], faceNormals[faceIndex]) <= kLengthToleranceSquared)
				return false;
			topology.faceCorners[faceIndex].resize(face.vertices.size(), ThickFlatBevelTurn::Unsupported);
			for (std::size_t corner = 0u; corner < face.vertices.size(); ++corner)
			{
				const std::uint32_t previous = face.vertices[(corner + face.vertices.size() - 1u) % face.vertices.size()];
				const std::uint32_t vertex = face.vertices[corner];
				const std::uint32_t next = face.vertices[(corner + 1u) % face.vertices.size()];
				if (previous >= mesh.vertices.size() || vertex >= mesh.vertices.size() || next >= mesh.vertices.size() ||
					vertex == next)
					return false;

				const ThickFlatBevelTurn cornerTurn = ClassifyTurn(
					mesh.vertices[vertex].position - mesh.vertices[previous].position,
					mesh.vertices[next].position - mesh.vertices[vertex].position, faceNormals[faceIndex]);
				topology.faceCorners[faceIndex][corner] = cornerTurn;

				const ThickFlatBevelEdgeKey key = MakeEdgeKey(vertex, next);
				auto [found, inserted] = topology.edgeByKey.emplace(key, topology.edges.size());
				if (inserted)
					topology.edges.push_back({key});
				ThickFlatBevelEdge &edge = topology.edges[found->second];
				edge.incidents.push_back({faceIndex, corner});
				edge.bevel = edge.bevel || face.bevelEdges[corner];
				AddUnique(topology.vertices[vertex].edges, found->second);
				AddUnique(topology.vertices[next].edges, found->second);
			}
		}

		for (ThickFlatBevelEdge &edge : topology.edges)
		{
			if (edge.incidents.size() != 2u || edge.incidents[0].face == edge.incidents[1].face)
				return false;
			const ThickFlatBevelIncident &firstIncident = edge.incidents[0];
			const ThickFlatMeshFace &firstFace = mesh.faces[firstIncident.face];
			const std::uint32_t first = firstFace.vertices[firstIncident.corner];
			const std::uint32_t second = firstFace.vertices[(firstIncident.corner + 1u) % firstFace.vertices.size()];
			const glm::dvec3 direction = mesh.vertices[second].position - mesh.vertices[first].position;
			edge.turn = ClassifyTurn(faceNormals[firstIncident.face], faceNormals[edge.incidents[1].face], direction);
		}

		for (std::size_t faceIndex = 0u; faceIndex < mesh.faces.size(); ++faceIndex)
		{
			const auto &vertices = mesh.faces[faceIndex].vertices;
			for (std::size_t corner = 0u; corner < vertices.size(); ++corner)
			{
				const std::uint32_t vertex = vertices[corner];
				const std::size_t previous = EdgeId(topology,
					MakeEdgeKey(vertices[(corner + vertices.size() - 1u) % vertices.size()], vertex));
				const std::size_t next = EdgeId(topology, MakeEdgeKey(vertex, vertices[(corner + 1u) % vertices.size()]));
				if (previous == std::numeric_limits<std::size_t>::max() || next == std::numeric_limits<std::size_t>::max())
					return false;
				transitions[vertex].push_back({previous, next});
			}
		}

		for (auto &[vertex, info] : topology.vertices)
		{
			std::map<std::size_t, std::vector<std::size_t>> neighbours;
			for (const auto &[first, second] : transitions[vertex])
			{
				AddUnique(neighbours[first], second);
				AddUnique(neighbours[second], first);
			}
			if (info.edges.size() < 3u)
				return false;
			for (const std::size_t edge : info.edges)
				if (neighbours[edge].size() != 2u)
					return false;

			const std::size_t start = info.edges.front();
			std::size_t current = start;
			do
			{
				info.orderedEdges.push_back(current);
				// Walk outgoing -> incoming in each outward-wound face. The resulting
				// corner boundary opposes the incident edge strips at every endpoint.
				const auto next = std::find_if(transitions[vertex].begin(), transitions[vertex].end(),
					[&](const auto &transition) { return transition.second == current; });
				if (next == transitions[vertex].end())
					return false;
				current = next->first;
			} while (current != start && info.orderedEdges.size() <= info.edges.size());
			if (info.orderedEdges.size() != info.edges.size())
				return false;

			info.turn = ThickFlatBevelTurn::Convex;
			for (const std::size_t edgeId : info.edges)
				info.turn = MergeTurn(info.turn, topology.edges[edgeId].turn);
			info.radius = requestedRadius;
			for (std::size_t faceIndex = 0u; faceIndex < mesh.faces.size(); ++faceIndex)
				for (std::size_t corner = 0u; corner < mesh.faces[faceIndex].vertices.size(); ++corner)
					if (mesh.faces[faceIndex].vertices[corner] == vertex)
					{
						info.turn = MergeTurn(info.turn, topology.faceCorners[faceIndex][corner]);
						const ThickFlatMeshFace &face = mesh.faces[faceIndex];
						const std::size_t previousCorner = (corner + face.vertices.size() - 1u) % face.vertices.size();
						if (topology.faceCorners[faceIndex][corner] == ThickFlatBevelTurn::Smooth ||
							(!face.bevelEdges[previousCorner] && !face.bevelEdges[corner]))
							continue;
						const glm::dvec3 toPrevious = mesh.vertices[face.vertices[previousCorner]].position -
							mesh.vertices[vertex].position;
						const glm::dvec3 toNext = mesh.vertices[face.vertices[(corner + 1u) % face.vertices.size()]].position -
							mesh.vertices[vertex].position;
						const double previousLength = glm::length(toPrevious);
						const double nextLength = glm::length(toNext);
						if (!(std::isfinite(previousLength) && std::isfinite(nextLength)) ||
							previousLength * previousLength <= kLengthToleranceSquared ||
							nextLength * nextLength <= kLengthToleranceSquared)
						{
							info.turn = ThickFlatBevelTurn::Unsupported;
							info.radius = 0.0;
							continue;
						}
						const double cosine = std::clamp(glm::dot(toPrevious / previousLength,
							toNext / nextLength), -1.0, 1.0);
						const double cornerAngle = std::acos(cosine);
						const double faceRadius = 0.45 * std::min(previousLength, nextLength) *
							std::tan(0.5 * cornerAngle);
						if (!(std::isfinite(faceRadius) && faceRadius > 0.0))
						{
							info.turn = ThickFlatBevelTurn::Unsupported;
							info.radius = 0.0;
							continue;
						}
						info.radius = std::min(info.radius, faceRadius);
					}
		}
		return true;
	}
}
