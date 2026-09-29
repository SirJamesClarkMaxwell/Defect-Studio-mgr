#include "Core/dspch.hpp"

#include "Renderer/Path/PathPolygonTriangulator.hpp"
#include "Renderer/Path/PathSolidBevelTopology.hpp"
#include "Renderer/Path/PathSolidMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
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

		[[nodiscard]] glm::dvec3 InsetCorner(const ThickFlatMesh &mesh, const ThickFlatMeshFace &face,
			const glm::dvec3 normal, const std::size_t corner, const double bevel)
		{
			const std::size_t count = face.vertices.size();
			const glm::dvec3 vertex = mesh.vertices[face.vertices[corner]].position;
			const glm::dvec3 previous = mesh.vertices[face.vertices[(corner + count - 1u) % count]].position;
			const glm::dvec3 next = mesh.vertices[face.vertices[(corner + 1u) % count]].position;
			const glm::dvec3 previousDirection = SafeNormal(vertex - previous, glm::dvec3(0.0));
			const glm::dvec3 nextDirection = SafeNormal(next - vertex, glm::dvec3(0.0));
			const bool previousBevel = face.bevelEdges[(corner + count - 1u) % count];
			const bool nextBevel = face.bevelEdges[corner];
			if (!previousBevel && !nextBevel)
				return vertex;
			const glm::dvec3 previousInward = SafeNormal(glm::cross(normal, previousDirection), glm::dvec3(0.0));
			const glm::dvec3 nextInward = SafeNormal(glm::cross(normal, nextDirection), glm::dvec3(0.0));
			const glm::dvec3 firstLine = vertex + previousInward * (previousBevel ? bevel : 0.0);
			const glm::dvec3 secondLine = vertex + nextInward * (nextBevel ? bevel : 0.0);
			const double denominator = glm::dot(glm::cross(previousDirection, nextDirection), normal);
			if (std::abs(denominator) <= 1.0e-12)
			{
				glm::dvec3 offset(0.0);
				if (previousBevel)
					offset += previousInward;
				if (nextBevel)
					offset += nextInward;
				return vertex + SafeNormal(offset, previousInward) * bevel;
			}
			const double distance = glm::dot(glm::cross(secondLine - firstLine, nextDirection), normal) / denominator;
			const glm::dvec3 result = firstLine + previousDirection * distance;
			return glm::distance(result, vertex) <= bevel * 8.0 ? result :
				vertex + SafeNormal((previousBevel ? previousInward : glm::dvec3(0.0)) +
					(nextBevel ? nextInward : glm::dvec3(0.0)), previousInward) * bevel;
		}

		struct DisjointSet
		{
			explicit DisjointSet(const std::size_t size) : parent(size) { std::iota(parent.begin(), parent.end(), 0u); }
			std::size_t Find(const std::size_t value) { return parent[value] == value ? value : parent[value] = Find(parent[value]); }
			void Join(const std::size_t first, const std::size_t second) { parent[Find(first)] = Find(second); }
			std::vector<std::size_t> parent;
		};

		struct OwnerOutput
		{
			std::vector<StrokeTubeVertex> vertices;
			std::vector<std::uint32_t> indices;
		};

		[[nodiscard]] std::size_t OwnerIndex(const ThickFlatFaceOwner owner)
		{
			return static_cast<std::size_t>(owner);
		}

		void EmitPolygon(OwnerOutput &output, const ThickFlatMesh &mesh,
			std::vector<std::uint32_t> sources, std::vector<glm::dvec3> positions, const glm::dvec3 expectedNormal)
		{
			if (positions.size() < 3u || positions.size() != sources.size())
				return;
			glm::dvec3 polygonNormal(0.0);
			for (std::size_t index = 0u; index < positions.size(); ++index)
				polygonNormal += glm::cross(positions[index], positions[(index + 1u) % positions.size()]);
			if (glm::dot(polygonNormal, expectedNormal) < 0.0)
			{
				std::reverse(positions.begin(), positions.end());
				std::reverse(sources.begin(), sources.end());
				polygonNormal = -polygonNormal;
			}
			const auto emitTriangle = [&](const std::array<std::uint32_t, 3u> &triangleSources,
				const std::array<glm::dvec3, 3u> &trianglePositions) {
				glm::dvec3 normal = glm::cross(trianglePositions[1] - trianglePositions[0],
					trianglePositions[2] - trianglePositions[0]);
				if (glm::dot(normal, normal) <= kToleranceSquared)
					return;
				normal = glm::normalize(normal);
				for (std::size_t corner = 0u; corner < 3u; ++corner)
				{
					const ThickFlatMeshVertex &source = mesh.vertices[triangleSources[corner]];
					output.indices.push_back(static_cast<std::uint32_t>(output.vertices.size()));
					output.vertices.push_back({glm::vec3(trianglePositions[corner]), glm::vec3(normal), source.color,
						source.arcT, source.dashCoord});
				}
			};
			std::vector<std::array<std::size_t, 3u>> triangles;
			if (!TriangulateSimplePolygon(positions, polygonNormal, triangles))
				return;
			for (const auto &triangle : triangles)
				emitTriangle({sources[triangle[0]], sources[triangle[1]], sources[triangle[2]]},
					{positions[triangle[0]], positions[triangle[1]], positions[triangle[2]]});
		}

		void EmitVertexPatch(OwnerOutput &output, const ThickFlatMesh &mesh, const std::uint32_t source,
			const std::vector<glm::dvec3> &boundary, const glm::dvec3 expectedNormal)
		{
			glm::dvec3 centre(0.0);
			for (const glm::dvec3 &position : boundary)
				centre += position;
			centre /= static_cast<double>(boundary.size());
			for (std::size_t index = 0u; index < boundary.size(); ++index)
				EmitPolygon(output, mesh, {source, source, source},
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

		void EmitTrihedralVertexPatch(OwnerOutput &output, const ThickFlatMesh &mesh, const std::uint32_t source,
			const std::array<std::vector<glm::dvec3>, 3u> &arcs,
			const std::array<glm::dvec3, 3u> &faceNormals, const ThickFlatBevelTurn turn,
			const double bevel, const double shape, const glm::dvec3 expectedNormal)
		{
			if (arcs[0].size() < 2u || arcs[1].size() != arcs[0].size() || arcs[2].size() != arcs[0].size())
				return;
			const std::uint32_t segments = static_cast<std::uint32_t>(arcs[0].size() - 1u);
			if (turn == ThickFlatBevelTurn::Reflex)
			{
				const glm::dvec3 sourcePosition = mesh.vertices[source].position;
				for (std::size_t arcIndex = 0u; arcIndex < arcs.size(); ++arcIndex)
				{
					const glm::dvec3 arcNormal = SafeNormal(faceNormals[arcIndex] +
						faceNormals[(arcIndex + 1u) % faceNormals.size()], expectedNormal);
					for (std::uint32_t sample = 0u; sample < segments; ++sample)
						EmitPolygon(output, mesh, {source, source, source},
							{sourcePosition, arcs[arcIndex][sample], arcs[arcIndex][sample + 1u]}, arcNormal);
				}
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
					EmitPolygon(output, mesh, {source, source, source},
						{points[first][second], points[first + 1u][second], points[first][second + 1u]},
						expectedNormal);
					if (second + first + 1u < segments)
						EmitPolygon(output, mesh, {source, source, source},
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

		void AppendOutputs(const std::array<OwnerOutput, 3u> &outputs, StrokeGeometry &geometry)
		{
			geometry.tubeVertices.clear();
			geometry.ribbonVertices.clear();
			geometry.indices.clear();
			const auto append = [&](const ThickFlatFaceOwner owner, StrokeMeshRange &range) {
				const OwnerOutput &output = outputs[OwnerIndex(owner)];
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

		void SmoothCoincidentNormals(StrokeGeometry &geometry)
		{
			using PositionKey = std::array<float, 3u>;
			const auto key = [](const glm::vec3 &position) {
				return PositionKey{position.x, position.y, position.z};
			};
			std::map<PositionKey, glm::dvec3> normalSums;
			for (std::size_t index = 0u; index + 2u < geometry.indices.size(); index += 3u)
			{
				std::array<glm::dvec3, 3u> positions;
				bool valid = true;
				for (std::size_t corner = 0u; corner < positions.size(); ++corner)
				{
					const std::uint32_t vertex = geometry.indices[index + corner];
					if (vertex >= geometry.tubeVertices.size())
					{
						valid = false;
						break;
					}
					positions[corner] = geometry.tubeVertices[vertex].position;
				}
				if (!valid)
					continue;
				const glm::dvec3 cross = glm::cross(positions[1] - positions[0], positions[2] - positions[0]);
				const glm::dvec3 faceNormal = SafeNormal(cross, glm::dvec3(0.0));
				if (glm::dot(faceNormal, faceNormal) <= kToleranceSquared)
					continue;
				for (std::size_t corner = 0u; corner < positions.size(); ++corner)
				{
					const glm::dvec3 first = SafeNormal(positions[(corner + 1u) % 3u] - positions[corner], glm::dvec3(0.0));
					const glm::dvec3 second = SafeNormal(positions[(corner + 2u) % 3u] - positions[corner], glm::dvec3(0.0));
					if (glm::dot(first, first) <= kToleranceSquared || glm::dot(second, second) <= kToleranceSquared)
						continue;
					const double angle = std::acos(std::clamp(glm::dot(first, second), -1.0, 1.0));
					normalSums[key(glm::vec3(positions[corner]))] += faceNormal * angle;
				}
			}
			for (StrokeTubeVertex &vertex : geometry.tubeVertices)
			{
				const auto found = normalSums.find(key(vertex.position));
				if (found != normalSums.end())
					vertex.normal = glm::vec3(SafeNormal(found->second, glm::dvec3(vertex.normal)));
			}
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

		std::vector<std::size_t> cornerOffsets(mesh.faces.size() + 1u, 0u);
		for (std::size_t faceIndex = 0u; faceIndex < mesh.faces.size(); ++faceIndex)
		{
			const ThickFlatMeshFace &face = mesh.faces[faceIndex];
			FaceInfo &info = faces[faceIndex];
			for (std::size_t corner = 0u; corner < face.vertices.size(); ++corner)
				info.inner.push_back(InsetCorner(mesh, face, info.normal, corner,
					VertexRadius(topology, face.vertices[corner])));
			cornerOffsets[faceIndex + 1u] = cornerOffsets[faceIndex] + face.vertices.size();
		}

		DisjointSet cornerGroups(cornerOffsets.back());
		for (const ThickFlatBevelEdge &edge : topology.edges)
			if (!edge.bevel)
				for (const std::uint32_t vertex : {edge.key.first, edge.key.second})
				{
					const std::size_t firstCorner = FindCorner(mesh, edge.incidents[0].face, vertex);
					const std::size_t secondCorner = FindCorner(mesh, edge.incidents[1].face, vertex);
					cornerGroups.Join(cornerOffsets[edge.incidents[0].face] + firstCorner,
						cornerOffsets[edge.incidents[1].face] + secondCorner);
				}
		std::vector<glm::dvec3> sums(cornerOffsets.back(), glm::dvec3(0.0));
		std::vector<std::size_t> counts(cornerOffsets.back(), 0u);
		for (std::size_t faceIndex = 0u; faceIndex < faces.size(); ++faceIndex)
			for (std::size_t corner = 0u; corner < faces[faceIndex].inner.size(); ++corner)
			{
				const std::size_t root = cornerGroups.Find(cornerOffsets[faceIndex] + corner);
				sums[root] += faces[faceIndex].inner[corner];
				++counts[root];
			}
		for (std::size_t faceIndex = 0u; faceIndex < faces.size(); ++faceIndex)
			for (std::size_t corner = 0u; corner < faces[faceIndex].inner.size(); ++corner)
			{
				const std::size_t root = cornerGroups.Find(cornerOffsets[faceIndex] + corner);
				faces[faceIndex].inner[corner] = sums[root] / static_cast<double>(counts[root]);
			}

		std::array<OwnerOutput, 3u> outputs;
		for (std::size_t faceIndex = 0u; faceIndex < mesh.faces.size(); ++faceIndex)
			EmitPolygon(outputs[OwnerIndex(mesh.faces[faceIndex].owner)], mesh, mesh.faces[faceIndex].vertices,
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
				EmitPolygon(outputs[OwnerIndex(owner)], mesh,
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
			SmoothCoincidentNormals(geometry);
	}
}
