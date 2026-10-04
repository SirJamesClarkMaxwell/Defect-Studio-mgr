#include "Core/dspch.hpp"

#include "Renderer/Path/PathSolidBevelGeometry.hpp"
#include "Renderer/Path/PathPolygonTriangulator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>

namespace DefectStudio::detail
{
	namespace
	{
		constexpr double kToleranceSquared = 1.0e-20;

		[[nodiscard]] glm::dvec3 SafeNormal(const glm::dvec3 &value, const glm::dvec3 &fallback)
		{
			return glm::dot(value, value) > kToleranceSquared ? glm::normalize(value) : fallback;
		}
		[[nodiscard]] std::size_t FindCorner(const ThickFlatMesh &mesh, const std::size_t face,
			const std::uint32_t vertex)
		{
			const auto &vertices = mesh.faces[face].vertices;
			const auto found = std::find(vertices.begin(), vertices.end(), vertex);
			return found == vertices.end() ? std::numeric_limits<std::size_t>::max() :
				static_cast<std::size_t>(found - vertices.begin());
		}

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

	} // namespace

	bool BuildThickFlatBevelInsets(const ThickFlatMesh &mesh,
		const std::span<const glm::dvec3> normals, ThickFlatBevelTopology &topology,
		std::vector<std::vector<glm::dvec3>> &insets)
	{
		std::vector<std::size_t> offsets(mesh.faces.size() + 1u, 0u);
		for (std::size_t face = 0u; face < mesh.faces.size(); ++face)
			offsets[face + 1u] = offsets[face] + mesh.faces[face].vertices.size();
		DisjointSet groups(offsets.back());
		for (const ThickFlatBevelEdge &edge : topology.edges)
			if (!edge.bevel)
				for (const std::uint32_t vertex : {edge.key.first, edge.key.second})
					groups.Join(offsets[edge.incidents[0].face] + FindCorner(mesh, edge.incidents[0].face, vertex),
						offsets[edge.incidents[1].face] + FindCorner(mesh, edge.incidents[1].face, vertex));

		// The rigid handoff can leave very short, oblique edges. A corner's isolated radius
		// bound does not prevent the combined inset face from crossing itself after seam welding.
		for (std::size_t attempt = 0u; attempt < 16u; ++attempt)
		{
			insets.assign(mesh.faces.size(), {});
			std::vector<glm::dvec3> sums(offsets.back(), glm::dvec3(0.0));
			std::vector<std::size_t> counts(offsets.back(), 0u);
			for (std::size_t face = 0u; face < mesh.faces.size(); ++face)
				for (std::size_t corner = 0u; corner < mesh.faces[face].vertices.size(); ++corner)
				{
					const std::uint32_t vertex = mesh.faces[face].vertices[corner];
					const auto local = topology.vertices.find(vertex);
					if (local == topology.vertices.end())
						return false;
					const glm::dvec3 inset = InsetCorner(mesh, mesh.faces[face], normals[face], corner,
						local->second.radius);
					insets[face].push_back(inset);
					const std::size_t root = groups.Find(offsets[face] + corner);
					sums[root] += inset;
					++counts[root];
				}
			for (std::size_t face = 0u; face < mesh.faces.size(); ++face)
				for (std::size_t corner = 0u; corner < insets[face].size(); ++corner)
				{
					const std::size_t root = groups.Find(offsets[face] + corner);
					insets[face][corner] = sums[root] / static_cast<double>(counts[root]);
				}

			std::vector<bool> shrink(mesh.vertices.size(), false);
			bool crossing = false;
			for (std::size_t face = 0u; face < insets.size(); ++face)
			{
				const auto &positions = insets[face];
				for (std::size_t first = 0u; first < positions.size(); ++first)
					for (std::size_t second = first + 2u; second < positions.size(); ++second)
					{
						if (first == 0u && second + 1u == positions.size())
							continue;
						const std::size_t nextFirst = (first + 1u) % positions.size();
						const std::size_t nextSecond = (second + 1u) % positions.size();
						const glm::dvec3 a = positions[nextFirst] - positions[first];
						const glm::dvec3 b = positions[nextSecond] - positions[second];
						const glm::dvec3 offset = positions[second] - positions[first];
						const double denominator = glm::dot(glm::cross(a, b), normals[face]);
						const double tolerance = std::max(kToleranceSquared,
							1.0e-12 * glm::length(a) * glm::length(b));
						if (std::abs(denominator) <= tolerance)
							continue;
						const double t = glm::dot(glm::cross(offset, b), normals[face]) / denominator;
						const double u = glm::dot(glm::cross(offset, a), normals[face]) / denominator;
						if (t <= 1.0e-10 || t >= 1.0 - 1.0e-10 || u <= 1.0e-10 || u >= 1.0 - 1.0e-10)
							continue;
						crossing = true;
						for (const std::size_t corner : {first, nextFirst, second, nextSecond})
							shrink[mesh.faces[face].vertices[corner]] = true;
					}
			}
			if (!crossing)
				return true;
			for (auto &[vertex, info] : topology.vertices)
				if (shrink[vertex])
					info.radius *= 0.5;
		}
		return false;
	}

	void EmitThickFlatPinchedBevelPatch(ThickFlatBevelOutput &output, const ThickFlatMesh &mesh,
		const std::uint32_t source, const std::vector<glm::dvec3> &boundary,
		const std::uint64_t smoothingGroup)
	{
		if (boundary.size() < 3u)
			return;
		// Shape zero and coplanar contour seams can make two profile arcs share a span. Split the closed walk at
		// its repeated points: the shared span already has its two incident edge strips,
		// and a zero-area loop there must not acquire an additional pair of patch faces.
		for (std::size_t second = 1u; second < boundary.size(); ++second)
			for (std::size_t first = 0u; first < second; ++first)
				if (glm::dot(boundary[first] - boundary[second], boundary[first] - boundary[second]) <= kToleranceSquared)
				{
					const auto a = boundary.begin() + static_cast<std::ptrdiff_t>(first);
					const auto b = boundary.begin() + static_cast<std::ptrdiff_t>(second);
					const std::vector<glm::dvec3> loop(a, b);
					EmitThickFlatPinchedBevelPatch(output, mesh, source, loop, smoothingGroup);
					std::vector<glm::dvec3> remaining(boundary.begin(), a);
					remaining.insert(remaining.end(), b, boundary.end());
					EmitThickFlatPinchedBevelPatch(output, mesh, source, remaining, smoothingGroup);
					return;
				}
		EmitThickFlatBevelPolygon(output, mesh, std::vector<std::uint32_t>(boundary.size(), source),
			boundary, smoothingGroup);
	}

	void EmitThickFlatBevelPolygon(ThickFlatBevelOutput &output, const ThickFlatMesh &mesh,
		std::vector<std::uint32_t> sources, std::vector<glm::dvec3> positions,
		const std::uint64_t smoothingGroup)
	{
		if (positions.size() < 3u || positions.size() != sources.size())
			return;
		// Insets follow the sampled side faces. At a sharply turning terminal ring their
		// miter/profile can cross the cap plane and form a backwards sliver. Constrain the
		// shared polygon emitter, including corner patches, rather than just the cap face.
		for (std::size_t index = 0; index < positions.size(); ++index)
		{
			const auto &source = mesh.vertices[sources[index]];
			if (source.capOutwardNormal)
				positions[index] -= *source.capOutwardNormal * std::max(0.0,
					glm::dot(positions[index] - source.position, *source.capOutwardNormal));
		}
		// A strip tapered to zero radius is a triangle with a repeated corner, not
		// an invalid quad. Keep its boundary while discarding only zero-length edges.
		std::size_t kept = 0u;
		for (std::size_t index = 0u; index < positions.size(); ++index)
			if (kept == 0u || glm::dot(positions[index] - positions[kept - 1u],
				positions[index] - positions[kept - 1u]) > kToleranceSquared)
			{
				positions[kept] = positions[index];
				sources[kept++] = sources[index];
			}
		if (kept > 1u && glm::dot(positions[0] - positions[kept - 1u],
			positions[0] - positions[kept - 1u]) <= kToleranceSquared)
			--kept;
		positions.resize(kept);
		sources.resize(kept);
		if (kept < 3u)
			return;
		glm::dvec3 polygonNormal(0.0);
		for (std::size_t index = 0u; index < positions.size(); ++index)
			polygonNormal += glm::cross(positions[index], positions[(index + 1u) % positions.size()]);
		const auto emitTriangle = [&](const std::array<std::uint32_t, 3u> &triangleSources,
			const std::array<glm::dvec3, 3u> &trianglePositions) {
			glm::dvec3 normal = glm::cross(trianglePositions[1] - trianglePositions[0],
				trianglePositions[2] - trianglePositions[0]);
			if (glm::dot(normal, normal) <= kToleranceSquared)
				return;
			normal = glm::normalize(normal);
			const bool cap = std::all_of(triangleSources.begin(), triangleSources.end(), [&](const auto source) {
				const auto &outward = mesh.vertices[source].capOutwardNormal;
				return outward && glm::dot(normal, *outward) > 1.0 - 1.0e-10;
			});
			for (std::size_t corner = 0u; corner < 3u; ++corner)
			{
				const ThickFlatMeshVertex &source = mesh.vertices[triangleSources[corner]];
				output.indices.push_back(static_cast<std::uint32_t>(output.vertices.size()));
				output.vertices.push_back({glm::vec3(trianglePositions[corner]), glm::vec3(normal), source.color,
					source.arcT, source.dashCoord, cap ? 0u : smoothingGroup});
			}
		};
		std::vector<std::array<std::size_t, 3u>> triangles;
		if (!TriangulateSimplePolygon(positions, SafeNormal(polygonNormal, glm::dvec3(0.0)), triangles))
			return;
		for (const auto &triangle : triangles)
			emitTriangle({sources[triangle[0]], sources[triangle[1]], sources[triangle[2]]},
				{positions[triangle[0]], positions[triangle[1]], positions[triangle[2]]});
	}

	void SmoothThickFlatBevelNormals(StrokeGeometry &geometry)
	{
		using PositionKey = std::pair<std::array<float, 3u>, std::uint64_t>;
		const auto key = [](const StrokeTubeVertex &vertex) {
			return PositionKey{{vertex.position.x, vertex.position.y, vertex.position.z}, vertex.smoothingGroup};
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
				const auto &vertex = geometry.tubeVertices[geometry.indices[index + corner]];
				if (vertex.smoothingGroup == 0) continue;
				const auto entry = normalSums.try_emplace(key(vertex), glm::dvec3(0.0)).first;
				entry->second += faceNormal * angle;
			}
		}
		for (StrokeTubeVertex &vertex : geometry.tubeVertices)
		{
			const auto found = normalSums.find(key(vertex));
			if (found != normalSums.end())
				vertex.normal = glm::vec3(SafeNormal(found->second, glm::dvec3(vertex.normal)));
		}
	}
}
