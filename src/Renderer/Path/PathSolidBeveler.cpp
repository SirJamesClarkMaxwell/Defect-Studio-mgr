#include "Core/dspch.hpp"

#include "Renderer/Path/PathSolidBevelGeometry.hpp"
#include "Renderer/Path/PathSolidBevelTopology.hpp"
#include "Renderer/Path/PathSolidMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace DefectStudio::detail
{
	namespace
	{
		constexpr double kToleranceSquared = 1.0e-20;

		[[nodiscard]] glm::dvec3 SafeNormal(const glm::dvec3 &value, const glm::dvec3 &fallback)
		{
			return glm::dot(value, value) > kToleranceSquared ? glm::normalize(value) : fallback;
		}

		[[nodiscard]] glm::dvec3 FaceNormal(const ThickFlatMesh &mesh, const ThickFlatMeshFace &face)
		{
			glm::dvec3 normal(0.0);
			for (std::size_t index = 0u; index < face.vertices.size(); ++index)
				normal += glm::cross(mesh.vertices[face.vertices[index]].position,
					mesh.vertices[face.vertices[(index + 1u) % face.vertices.size()]].position);
			return SafeNormal(normal, glm::dvec3(0.0));
		}

		[[nodiscard]] std::size_t FindCorner(const ThickFlatMesh &mesh, const std::size_t face,
			const std::uint32_t vertex)
		{
			const auto &vertices = mesh.faces[face].vertices;
			const auto found = std::find(vertices.begin(), vertices.end(), vertex);
			return found == vertices.end() ? std::numeric_limits<std::size_t>::max() :
				static_cast<std::size_t>(found - vertices.begin());
		}

		[[nodiscard]] std::size_t SharedFace(const ThickFlatBevelEdge &first,
			const ThickFlatBevelEdge &second)
		{
			for (const ThickFlatBevelIncident &a : first.incidents)
				for (const ThickFlatBevelIncident &b : second.incidents)
					if (a.face == b.face)
						return a.face;
			return std::numeric_limits<std::size_t>::max();
		}

		[[nodiscard]] double VertexRadius(const ThickFlatBevelTopology &topology,
			const std::uint32_t vertex)
		{
			const auto found = topology.vertices.find(vertex);
			return found == topology.vertices.end() ? 0.0 : found->second.radius;
		}

		[[nodiscard]] ThickFlatFaceOwner DecorationOwner(const ThickFlatFaceOwner first,
			const ThickFlatFaceOwner second)
		{
			if (first != ThickFlatFaceOwner::Shaft)
				return first;
			return second;
		}

		struct FaceInfo
		{
			glm::dvec3 normal{0.0};
			std::vector<glm::dvec3> inner;
		};

		[[nodiscard]] std::size_t OwnerIndex(const ThickFlatFaceOwner owner)
		{
			return static_cast<std::size_t>(owner);
		}

		void EmitVertexPatch(ThickFlatBevelOutput &output, const ThickFlatMesh &mesh, const std::uint32_t source,
			const std::vector<glm::dvec3> &boundary, const glm::dvec3 expectedNormal)
		{
			glm::dvec3 centre(0.0);
			for (const glm::dvec3 &position : boundary)
				centre += position;
			centre /= static_cast<double>(boundary.size());
			for (std::size_t index = 0u; index < boundary.size(); ++index)
				EmitThickFlatBevelPolygon(output, mesh, {source, source, source},
					{centre, boundary[index], boundary[(index + 1u) % boundary.size()]}, expectedNormal);
		}

		template<std::size_t Size>
		[[nodiscard]] double RadialProfileScale(const std::array<double, Size> &direction, const double shape)
		{
			if (shape >= 1.0)
				return 1.0 / *std::max_element(direction.begin(), direction.end());
			const double exponent = -std::log(2.0) / std::log(std::sqrt(std::max(shape, 1.0e-6)));
			double poweredSum = 0.0;
			for (const double component : direction)
				poweredSum += std::pow(component, exponent);
			return std::pow(poweredSum, -1.0 / exponent);
		}

		void EmitTrihedralVertexPatch(ThickFlatBevelOutput &output, const ThickFlatMesh &mesh, const std::uint32_t source,
			const std::array<std::vector<glm::dvec3>, 3u> &arcs,
			const std::array<glm::dvec3, 3u> &faceNormals, const ThickFlatBevelTurn turn,
			const double bevel, const double shape, const glm::dvec3 expectedNormal)
		{
			if (arcs[0].size() < 2u || arcs[1].size() != arcs[0].size() || arcs[2].size() != arcs[0].size())
				return;
			const std::uint32_t segments = static_cast<std::uint32_t>(arcs[0].size() - 1u);
			if (turn == ThickFlatBevelTurn::Reflex)
			{
				// Reentrant shoulder arcs fold in projection, including at shape zero.
				// Close them with the source-anchored fan before handling convex pinches.
				const glm::dvec3 sourcePosition = mesh.vertices[source].position;
				for (std::size_t arcIndex = 0u; arcIndex < arcs.size(); ++arcIndex)
				{
					const glm::dvec3 arcNormal = SafeNormal(faceNormals[arcIndex] +
						faceNormals[(arcIndex + 1u) % faceNormals.size()], expectedNormal);
					for (std::uint32_t sample = 0u; sample < segments; ++sample)
						EmitThickFlatBevelPolygon(output, mesh, {source, source, source},
							{sourcePosition, arcs[arcIndex][sample], arcs[arcIndex][sample + 1u]}, arcNormal);
				}
				return;
			}
			if (shape == 0.0 && segments > 1u)
			{
				std::vector<glm::dvec3> boundary;
				for (const auto &arc : arcs)
					boundary.insert(boundary.end(), arc.begin(), arc.end() - 1);
				EmitThickFlatPinchedBevelPatch(output, mesh, source, boundary, expectedNormal);
				return;
			}
			const std::array<glm::dvec3, 3u> extremes = {arcs[0].front(), arcs[0].back(), arcs[1].back()};
			glm::dvec3 centre(0.0);
			for (std::size_t index = 0u; index < extremes.size(); ++index)
				centre += extremes[index] - faceNormals[index] * bevel;
			centre /= static_cast<double>(extremes.size());

			std::vector<std::vector<glm::dvec3>> points(static_cast<std::size_t>(segments) + 1u);
			for (std::uint32_t first = 0u; first <= segments; ++first)
				for (std::uint32_t second = 0u; second + first <= segments; ++second)
				{
					const std::uint32_t third = segments - first - second;
					glm::dvec3 position(0.0);
					if (third == 0u)
						position = arcs[0][second];
					else if (first == 0u)
						position = arcs[1][third];
					else if (second == 0u)
						position = arcs[2][first];
					else
					{
						const std::array<double, 3u> fractions = {
							static_cast<double>(first) / segments,
							static_cast<double>(second) / segments,
							static_cast<double>(third) / segments};
						std::array<double, 3u> weights{};
						double weightSum = 0.0;
						for (std::size_t index = 0u; index < weights.size(); ++index)
						{
							const double sine = std::sin(0.5 * std::numbers::pi * fractions[index]);
							weights[index] = sine * sine;
							weightSum += weights[index];
						}
						std::array<double, 3u> direction{};
						for (std::size_t index = 0u; index < direction.size(); ++index)
							direction[index] = std::sqrt(weights[index] / weightSum);
						const double scale = RadialProfileScale(direction, shape);
						position = centre;
						for (std::size_t index = 0u; index < direction.size(); ++index)
							position += faceNormals[index] * bevel * direction[index] * scale;
					}
					points[first].push_back(position);
				}

			for (std::uint32_t first = 0u; first < segments; ++first)
				for (std::uint32_t second = 0u; second + first < segments; ++second)
				{
					EmitThickFlatBevelPolygon(output, mesh, {source, source, source},
						{points[first][second], points[first + 1u][second], points[first][second + 1u]},
						expectedNormal);
					if (second + first + 1u < segments)
						EmitThickFlatBevelPolygon(output, mesh, {source, source, source},
							{points[first + 1u][second], points[first + 1u][second + 1u],
								points[first][second + 1u]}, expectedNormal);
				}
		}

		[[nodiscard]] double Coordinate(const double fraction, const double shape, const bool first)
		{
			if (shape == 0.0)
				return fraction <= 0.5 ? (first ? 1.0 - 2.0 * fraction : 0.0) :
					(first ? 0.0 : 2.0 * fraction - 1.0);
			const double angle = 0.5 * std::numbers::pi * fraction;
			const std::array direction = {std::sin(angle), std::cos(angle)};
			return direction[first ? 1u : 0u] * RadialProfileScale(direction, shape);
		}

		[[nodiscard]] glm::dvec3 ProfilePoint(const glm::dvec3 &anchor, const glm::dvec3 &first,
			const glm::dvec3 &second, const double fraction, const double shape)
		{
			const double firstWeight = 1.0 - Coordinate(fraction, shape, false);
			const double secondWeight = 1.0 - Coordinate(fraction, shape, true);
			return anchor + (first - anchor) * firstWeight + (second - anchor) * secondWeight;
		}

		void AppendOutputs(const std::array<ThickFlatBevelOutput, 3u> &outputs, StrokeGeometry &geometry)
		{
			geometry.tubeVertices.clear();
			geometry.ribbonVertices.clear();
			geometry.indices.clear();
			const auto append = [&](const ThickFlatFaceOwner owner, StrokeMeshRange &range) {
				const ThickFlatBevelOutput &output = outputs[OwnerIndex(owner)];
				range.firstIndex = static_cast<std::uint32_t>(geometry.indices.size());
				const std::uint32_t firstVertex = static_cast<std::uint32_t>(geometry.tubeVertices.size());
				geometry.tubeVertices.insert(geometry.tubeVertices.end(), output.vertices.begin(), output.vertices.end());
				for (const std::uint32_t index : output.indices)
					geometry.indices.push_back(firstVertex + index);
				range.indexCount = static_cast<std::uint32_t>(geometry.indices.size()) - range.firstIndex;
			};
			append(ThickFlatFaceOwner::StartDecoration, geometry.startDecoration);
			append(ThickFlatFaceOwner::EndDecoration, geometry.endDecoration);
			append(ThickFlatFaceOwner::Shaft, geometry.shaft);
		}

	} // namespace

	void FinalizeThickFlatMesh(const ThickFlatMesh &sourceMesh, const PathStrokeStyle &style,
		StrokeGeometry &geometry)
	{
		const double shape = std::clamp(std::isfinite(style.ribbonBevelShape) ?
			static_cast<double>(style.ribbonBevelShape) : 0.5, 0.0, 1.0);
		ThickFlatMesh mesh = sourceMesh;
		MergeCoplanarThickFlatSeams(mesh);
		const double requestedBevel = static_cast<double>(style.ribbonBevel);
		if (!(std::isfinite(requestedBevel) && requestedBevel > 0.0))
		{
			FinalizeSharpThickFlatMesh(sourceMesh, geometry);
			return;
		}
		const double bevel = std::min(requestedBevel,
			0.5 * std::min(static_cast<double>(style.width), static_cast<double>(style.ribbonThickness)));

		std::vector<FaceInfo> faces(mesh.faces.size());
		std::vector<glm::dvec3> faceNormals(mesh.faces.size());
		for (std::size_t faceIndex = 0u; faceIndex < mesh.faces.size(); ++faceIndex)
		{
			faces[faceIndex].normal = FaceNormal(mesh, mesh.faces[faceIndex]);
			faceNormals[faceIndex] = faces[faceIndex].normal;
			if (glm::dot(faces[faceIndex].normal, faces[faceIndex].normal) <= kToleranceSquared)
			{
				FinalizeSharpThickFlatMesh(sourceMesh, geometry);
				return;
			}
		}
		ThickFlatBevelTopology topology;
		if (!BuildThickFlatBevelTopology(mesh, faceNormals, bevel, topology))
		{
			FinalizeSharpThickFlatMesh(sourceMesh, geometry);
			return;
		}

		std::vector<std::vector<glm::dvec3>> insets;
		if (!BuildThickFlatBevelInsets(mesh, faceNormals, topology, insets))
		{
			FinalizeSharpThickFlatMesh(sourceMesh, geometry);
			return;
		}
		for (std::size_t face = 0u; face < faces.size(); ++face)
			faces[face].inner = std::move(insets[face]);
		std::array<ThickFlatBevelOutput, 3u> outputs;
		for (std::size_t faceIndex = 0u; faceIndex < mesh.faces.size(); ++faceIndex)
			EmitThickFlatBevelPolygon(outputs[OwnerIndex(mesh.faces[faceIndex].owner)], mesh, mesh.faces[faceIndex].vertices,
				faces[faceIndex].inner, faces[faceIndex].normal);

		const std::uint32_t segments = std::max(1u, style.ribbonBevelSegments);
		std::vector<std::vector<std::array<glm::dvec3, 2u>>> edgeProfiles(topology.edges.size());
		for (std::size_t edgeId = 0u; edgeId < topology.edges.size(); ++edgeId)
		{
			const ThickFlatBevelEdge &edge = topology.edges[edgeId];
			if (!edge.bevel)
				continue;
			const ThickFlatBevelIncident &firstIncident = edge.incidents[0];
			const ThickFlatBevelIncident &secondIncident = edge.incidents[1];
			const glm::dvec3 expected = SafeNormal(faces[firstIncident.face].normal +
				faces[secondIncident.face].normal, faces[firstIncident.face].normal);
			const ThickFlatFaceOwner owner = DecorationOwner(mesh.faces[firstIncident.face].owner,
				mesh.faces[secondIncident.face].owner);
			auto &profile = edgeProfiles[edgeId];
			profile.reserve(static_cast<std::size_t>(segments) + 1u);
			for (std::uint32_t sample = 0u; sample <= segments; ++sample)
			{
				const double fraction = static_cast<double>(sample) / segments;
				std::array<glm::dvec3, 2u> positions{};
				for (std::size_t endpoint = 0u; endpoint < 2u; ++endpoint)
				{
					const std::uint32_t vertex = endpoint == 0u ? edge.key.first : edge.key.second;
					const std::uint32_t other = endpoint == 0u ? edge.key.second : edge.key.first;
					const std::size_t firstCorner = FindCorner(mesh, firstIncident.face, vertex);
					const std::size_t secondCorner = FindCorner(mesh, secondIncident.face, vertex);
					const glm::dvec3 direction = mesh.vertices[other].position - mesh.vertices[vertex].position;
					const double length = glm::length(direction);
					const double localRadius = VertexRadius(topology, vertex);
					const glm::dvec3 anchor = mesh.vertices[vertex].position +
						SafeNormal(direction, glm::dvec3(0.0)) * std::min(localRadius, length * 0.25);
					positions[endpoint] = ProfilePoint(anchor, faces[firstIncident.face].inner[firstCorner],
						faces[secondIncident.face].inner[secondCorner], fraction, shape);
				}
				profile.push_back(positions);
			}
			for (std::uint32_t sample = 0u; sample < segments; ++sample)
				EmitThickFlatBevelPolygon(outputs[OwnerIndex(owner)], mesh,
					{edge.key.first, edge.key.second, edge.key.second, edge.key.first},
					{profile[sample][0], profile[sample][1], profile[sample + 1u][1],
						profile[sample + 1u][0]}, expected);
		}

		for (const auto &[vertex, info] : topology.vertices)
		{
			const std::size_t valence = info.orderedEdges.size();
			glm::dvec3 normal(0.0);
			ThickFlatFaceOwner owner = ThickFlatFaceOwner::Shaft;
			for (const std::size_t edgeId : info.edges)
				for (const ThickFlatBevelIncident &incident : topology.edges[edgeId].incidents)
				{
					normal += faces[incident.face].normal;
					owner = DecorationOwner(owner, mesh.faces[incident.face].owner);
				}
			const glm::dvec3 expectedNormal = SafeNormal(normal, glm::dvec3(0.0, 0.0, 1.0));

			if (valence == 3u && std::all_of(info.orderedEdges.begin(), info.orderedEdges.end(),
				[&](const std::size_t edgeId) { return topology.edges[edgeId].bevel; }))
			{
				std::array<std::vector<glm::dvec3>, 3u> arcs;
				std::array<std::size_t, 3u> faceIds{};
				bool validPatch = true;
				for (std::size_t index = 0u; index < valence; ++index)
				{
					const std::size_t edgeId = info.orderedEdges[index];
					const ThickFlatBevelEdge &edge = topology.edges[edgeId];
					const ThickFlatBevelEdge &previous = topology.edges[info.orderedEdges[(index + valence - 1u) % valence]];
					const ThickFlatBevelEdge &next = topology.edges[info.orderedEdges[(index + 1u) % valence]];
					const std::size_t fromFace = SharedFace(previous, edge);
					const std::size_t toFace = SharedFace(edge, next);
					if (fromFace >= faces.size() || toFace >= faces.size() || edgeProfiles[edgeId].size() != segments + 1u)
					{
						validPatch = false;
						break;
					}
					faceIds[index] = fromFace;
					const bool forward = edge.incidents[0].face == fromFace && edge.incidents[1].face == toFace;
					const std::size_t endpoint = edge.key.first == vertex ? 0u : 1u;
					for (std::uint32_t sample = 0u; sample <= segments; ++sample)
						arcs[index].push_back(edgeProfiles[edgeId][forward ? sample : segments - sample][endpoint]);
				}
				if (validPatch)
				{
					const std::array<glm::dvec3, 3u> patchFaceNormals = {
						faces[faceIds[0]].normal, faces[faceIds[1]].normal, faces[faceIds[2]].normal};
					EmitTrihedralVertexPatch(outputs[OwnerIndex(owner)], mesh, vertex, arcs, patchFaceNormals, info.turn,
						info.radius, shape, expectedNormal);
					continue;
				}
			}

			std::vector<glm::dvec3> boundary;
			for (std::size_t index = 0u; index < valence; ++index)
			{
				const std::size_t edgeId = info.orderedEdges[index];
				const ThickFlatBevelEdge &edge = topology.edges[edgeId];
				if (!edge.bevel)
					continue;
				const ThickFlatBevelEdge &previous = topology.edges[info.orderedEdges[(index + valence - 1u) % valence]];
				const ThickFlatBevelEdge &next = topology.edges[info.orderedEdges[(index + 1u) % valence]];
				const std::size_t fromFace = SharedFace(previous, edge);
				const std::size_t toFace = SharedFace(edge, next);
				const bool forward = edge.incidents[0].face == fromFace && edge.incidents[1].face == toFace;
				const std::size_t endpoint = edge.key.first == vertex ? 0u : 1u;
				for (std::uint32_t sample = 0u; sample <= segments; ++sample)
				{
					const glm::dvec3 position = edgeProfiles[edgeId][forward ? sample : segments - sample][endpoint];
					if (boundary.empty() || glm::dot(boundary.back() - position, boundary.back() - position) > kToleranceSquared)
						boundary.push_back(position);
				}
			}
			if (boundary.size() > 1u && glm::dot(boundary.front() - boundary.back(),
				boundary.front() - boundary.back()) <= kToleranceSquared)
				boundary.pop_back();
			if (boundary.size() < 3u)
				continue;
			EmitVertexPatch(outputs[OwnerIndex(owner)], mesh, vertex, boundary, expectedNormal);
		}

		AppendOutputs(outputs, geometry);
		if (style.shadeSmooth)
			SmoothThickFlatBevelNormals(geometry);
	}
}
