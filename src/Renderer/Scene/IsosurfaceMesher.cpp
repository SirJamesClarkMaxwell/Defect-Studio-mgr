#include "Core/dspch.hpp"

#include "Renderer/Scene/IsosurfaceMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>

namespace DefectStudio
{
	namespace
	{
		constexpr std::array<glm::ivec3, 8> kCubeCornerOffsets = {
			glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(1, 1, 0), glm::ivec3(0, 1, 0),
			glm::ivec3(0, 0, 1), glm::ivec3(1, 0, 1), glm::ivec3(1, 1, 1), glm::ivec3(0, 1, 1)};

		// 6-tetrahedra decomposition of a cube sharing the main diagonal 0-6 - a standard,
		// crack-free decomposition (adjacent cubes agree on shared-face triangulation because
		// the diagonal split of every shared face is determined by this same corner numbering,
		// not chosen independently per cube).
		constexpr std::array<std::array<int, 4>, 6> kCubeTetrahedra = {{
			{0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6},
			{0, 7, 4, 6}, {0, 4, 5, 6}, {0, 5, 1, 6}}};

		struct GridSample
		{
			glm::vec3 position;
			float value; // already sign-adjusted by the caller (see GenerateLobeMesh)
			glm::vec3 normal;
			std::size_t id = 0;
		};

		[[nodiscard]] std::size_t GridIndex(const glm::ivec3 &dims, int i, int j, int k)
		{
			return static_cast<std::size_t>(i) * static_cast<std::size_t>(dims.y) * static_cast<std::size_t>(dims.z) +
				static_cast<std::size_t>(j) * static_cast<std::size_t>(dims.z) + static_cast<std::size_t>(k);
		}

		struct LobeMesher
		{
			IndexedIsosurfaceMesh &mesh;
			const IsosurfaceMeshOptions &options;
			float sign;
			float gradientStep;
			std::map<std::pair<std::size_t, std::size_t>, std::uint32_t> edges;

			std::uint32_t EdgeCrossing(float iso, const GridSample &a, const GridSample &b)
			{
				const auto key = a.value == iso ? std::make_pair(a.id, a.id) :
					b.value == iso ? std::make_pair(b.id, b.id) :
					std::make_pair(std::min(a.id, b.id), std::max(a.id, b.id));
				if (const auto found = edges.find(key); found != edges.end())
					return found->second;
				const float denominator = b.value - a.value;
				float t = std::abs(denominator) > 1e-12f ? (iso - a.value) / denominator : 0.5f;
				t = glm::clamp(t, 0.0f, 1.0f);
				if (options.field && a.value != iso && b.value != iso)
				{
					float low = 0.0f, high = 1.0f;
					for (int iteration = 0; iteration < 12; ++iteration)
					{
						const float value = sign * options.field(glm::mix(a.position, b.position, t));
						if ((value >= iso) == (a.value >= iso)) low = t;
						else high = t;
						t = (low + high) * 0.5f;
					}
				}
				const glm::vec3 position = glm::mix(a.position, b.position, t);
				glm::vec3 normal = glm::mix(a.normal, b.normal, t);
				if (options.field)
				{
					for (int axis = 0; axis < 3; ++axis)
					{
						glm::vec3 offset(0.0f);
						offset[axis] = gradientStep;
						normal[axis] = -sign * (options.field(position + offset) - options.field(position - offset));
					}
				}
				if (glm::dot(normal, normal) > 0.0f) normal = glm::normalize(normal);
				const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
				mesh.vertices.push_back({position, normal, sign});
				edges.emplace(key, index);
				return index;
			}

			void EmitTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c)
			{
				const auto &p = mesh.vertices[a];
				const auto &q = mesh.vertices[b];
				const auto &r = mesh.vertices[c];
				const glm::vec3 face = glm::cross(q.position - p.position, r.position - p.position);
				if (glm::dot(face, face) == 0.0f) return;
				// Corner-number order alone does not orient all six tetrahedra consistently.
				if (glm::dot(face, p.normal + q.normal + r.normal) < 0.0f) std::swap(b, c);
				mesh.indices.insert(mesh.indices.end(), {a, b, c});
			}
		};
		// Handles all 16 in/out configurations of one tetrahedron's 4 corners against `iso`.
		// popCount 0/4: fully inside/outside, no surface. popCount 1/3: one corner on its own -
		// one triangle from the 3 edges touching it. popCount 2: two-and-two split - four edges
		// cross, forming a quad split into two triangles.
		void PolygoniseTetrahedron(
			LobeMesher &out, float iso, const std::array<GridSample, 4> &corners)
		{
			int inMask = 0;
			for (int vertex = 0; vertex < 4; ++vertex)
				if (corners[vertex].value >= iso)
					inMask |= (1 << vertex);

			if (inMask == 0 || inMask == 0xF)
				return;

			const int popCount =
				((inMask & 1) != 0) + ((inMask & 2) != 0) + ((inMask & 4) != 0) + ((inMask & 8) != 0);

			if (popCount == 1 || popCount == 3)
			{
				const bool singletonIsIn = popCount == 1;
				const int singletonMask = singletonIsIn ? inMask : (~inMask & 0xF);
				int singleton = 0;
				while (((singletonMask >> singleton) & 1) == 0)
					++singleton;

				std::array<int, 3> others{};
				int cursor = 0;
				for (int vertex = 0; vertex < 4; ++vertex)
					if (vertex != singleton)
						others[cursor++] = vertex;

				const auto p0 = out.EdgeCrossing(iso, corners[singleton], corners[others[0]]);
				const auto p1 = out.EdgeCrossing(iso, corners[singleton], corners[others[1]]);
				const auto p2 = out.EdgeCrossing(iso, corners[singleton], corners[others[2]]);

				if (singletonIsIn)
					out.EmitTriangle(p0, p1, p2);
				else
					out.EmitTriangle(p0, p2, p1);
			}
			else // popCount == 2
			{
				std::array<int, 2> insideVertices{};
				std::array<int, 2> outsideVertices{};
				int insideCursor = 0;
				int outsideCursor = 0;
				for (int vertex = 0; vertex < 4; ++vertex)
				{
					if ((inMask >> vertex) & 1)
						insideVertices[insideCursor++] = vertex;
					else
						outsideVertices[outsideCursor++] = vertex;
				}

				const int a = insideVertices[0];
				const int b = insideVertices[1];
				const int c = outsideVertices[0];
				const int d = outsideVertices[1];

				const auto pac = out.EdgeCrossing(iso, corners[a], corners[c]);
				const auto pad = out.EdgeCrossing(iso, corners[a], corners[d]);
				const auto pbd = out.EdgeCrossing(iso, corners[b], corners[d]);
				const auto pbc = out.EdgeCrossing(iso, corners[b], corners[c]);

				out.EmitTriangle(pac, pad, pbd);
				out.EmitTriangle(pac, pbd, pbc);
			}
		}

		void GenerateLobeMesh(
			IndexedIsosurfaceMesh &mesh, const OrbitalGridData &grid, float isoValue, float sign,
			const IsosurfaceMeshOptions &options)
		{
			const glm::ivec3 &dims = grid.dimensions;
			const glm::ivec3 intervals = dims - glm::ivec3(options.endpointInclusive ? 1 : 0);
			const float step = std::min({glm::length(grid.cell[0]) / intervals.x,
				glm::length(grid.cell[1]) / intervals.y, glm::length(grid.cell[2]) / intervals.z}) * 0.01f;
			LobeMesher out{mesh, options, sign, step, {}};
			const glm::mat3 normalMatrix = glm::transpose(glm::inverse(grid.cell));
			auto normalAt = [&](const glm::ivec3 &sample) {
				glm::vec3 gradient(0.0f);
				for (int axis = 0; axis < 3; ++axis)
				{
					glm::ivec3 low = sample, high = sample;
					low[axis] = std::max(0, sample[axis] - 1);
					high[axis] = std::min(dims[axis] - 1, sample[axis] + 1);
					gradient[axis] = (grid.values[GridIndex(dims, high.x, high.y, high.z)] -
						grid.values[GridIndex(dims, low.x, low.y, low.z)]) *
						static_cast<float>(intervals[axis]) / static_cast<float>(high[axis] - low[axis]);
				}
				// Outward from each signed lobe. Interpolate before normalizing at the edge crossing.
				return -sign * (normalMatrix * gradient);
			};
			for (int i = 0; i + 1 < dims.x; ++i)
			{
				for (int j = 0; j + 1 < dims.y; ++j)
				{
					for (int k = 0; k + 1 < dims.z; ++k)
					{
						std::array<GridSample, 8> cubeCorners{};
						for (int corner = 0; corner < 8; ++corner)
						{
							const glm::ivec3 offset = kCubeCornerOffsets[corner] + glm::ivec3(i, j, k);
							const glm::vec3 fractional(
								static_cast<float>(offset.x) / static_cast<float>(intervals.x),
								static_cast<float>(offset.y) / static_cast<float>(intervals.y),
								static_cast<float>(offset.z) / static_cast<float>(intervals.z));
							const glm::vec3 position =
								grid.origin + grid.cell[0] * fractional.x + grid.cell[1] * fractional.y +
								grid.cell[2] * fractional.z;
							const float rawValue = grid.values[GridIndex(dims, offset.x, offset.y, offset.z)];
							cubeCorners[corner] = GridSample{position, sign * rawValue, normalAt(offset), GridIndex(dims, offset.x, offset.y, offset.z)};
						}

						for (const std::array<int, 4> &tetrahedron : kCubeTetrahedra)
						{
							const std::array<GridSample, 4> tetCorners = {
								cubeCorners[tetrahedron[0]], cubeCorners[tetrahedron[1]],
								cubeCorners[tetrahedron[2]], cubeCorners[tetrahedron[3]]};
							PolygoniseTetrahedron(out, isoValue, tetCorners);
						}
					}
				}
			}
		}
	} // namespace

	IndexedIsosurfaceMesh GenerateIndexedIsosurfaceMesh(
		const OrbitalGridData &grid, float isoValue, const IsosurfaceMeshOptions &options)
	{
		IndexedIsosurfaceMesh vertices;
		if (grid.dimensions.x < 2 || grid.dimensions.y < 2 || grid.dimensions.z < 2 ||
			!std::isfinite(isoValue) || isoValue <= 0.0f ||
			std::abs(glm::determinant(grid.cell)) < 1e-12f ||
			grid.values.size() != static_cast<std::size_t>(grid.dimensions.x) *
				static_cast<std::size_t>(grid.dimensions.y) * static_cast<std::size_t>(grid.dimensions.z))
			return vertices;

		GenerateLobeMesh(vertices, grid, isoValue, 1.0f, options);
		GenerateLobeMesh(vertices, grid, isoValue, -1.0f, options);
		return vertices;
	}
	std::vector<IsosurfaceVertex> GenerateIsosurfaceMesh(
		const OrbitalGridData &grid, float isoValue, const IsosurfaceMeshOptions &options)
	{
		const auto mesh = GenerateIndexedIsosurfaceMesh(grid, isoValue, options);
		std::vector<IsosurfaceVertex> triangles;
		triangles.reserve(mesh.indices.size());
		for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
		{
			const auto &a = mesh.vertices[mesh.indices[i]];
			const auto &b = mesh.vertices[mesh.indices[i + 1]];
			const auto &c = mesh.vertices[mesh.indices[i + 2]];
			const glm::vec3 face = glm::normalize(glm::cross(b.position - a.position, c.position - a.position));
			for (const auto &vertex : {a, b, c})
				triangles.push_back({vertex.position,
					options.smoothShading && glm::dot(vertex.normal, vertex.normal) > 0.0f ? vertex.normal : face,
					vertex.sign});
		}
		return triangles;
	}
} // namespace DefectStudio
