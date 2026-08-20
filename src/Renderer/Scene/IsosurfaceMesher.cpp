#include "Core/dspch.hpp"

#include "Renderer/Scene/IsosurfaceMesher.hpp"

#include <array>
#include <cmath>
#include <cstddef>

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
		};

		[[nodiscard]] std::size_t GridIndex(const glm::ivec3 &dims, int i, int j, int k)
		{
			return static_cast<std::size_t>(i) * static_cast<std::size_t>(dims.y) * static_cast<std::size_t>(dims.z) +
				static_cast<std::size_t>(j) * static_cast<std::size_t>(dims.z) + static_cast<std::size_t>(k);
		}

		[[nodiscard]] glm::vec3 EdgeCrossing(float iso, const GridSample &a, const GridSample &b)
		{
			const float denominator = b.value - a.value;
			const float t = std::abs(denominator) > 1e-12f ? (iso - a.value) / denominator : 0.5f;
			return glm::mix(a.position, b.position, glm::clamp(t, 0.0f, 1.0f));
		}

		void EmitTriangle(
			std::vector<IsosurfaceVertex> &out, float sign,
			const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2)
		{
			const glm::vec3 crossProduct = glm::cross(p1 - p0, p2 - p0);
			const glm::vec3 normal = glm::length(crossProduct) > 1e-12f
				? glm::normalize(crossProduct)
				: glm::vec3(0.0f, 1.0f, 0.0f);
			out.push_back(IsosurfaceVertex{p0, normal, sign});
			out.push_back(IsosurfaceVertex{p1, normal, sign});
			out.push_back(IsosurfaceVertex{p2, normal, sign});
		}

		// Handles all 16 in/out configurations of one tetrahedron's 4 corners against `iso`.
		// popCount 0/4: fully inside/outside, no surface. popCount 1/3: one corner on its own -
		// one triangle from the 3 edges touching it. popCount 2: two-and-two split - four edges
		// cross, forming a quad split into two triangles.
		void PolygoniseTetrahedron(
			std::vector<IsosurfaceVertex> &out, float sign, float iso, const std::array<GridSample, 4> &corners)
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

				const glm::vec3 p0 = EdgeCrossing(iso, corners[singleton], corners[others[0]]);
				const glm::vec3 p1 = EdgeCrossing(iso, corners[singleton], corners[others[1]]);
				const glm::vec3 p2 = EdgeCrossing(iso, corners[singleton], corners[others[2]]);

				if (singletonIsIn)
					EmitTriangle(out, sign, p0, p1, p2);
				else
					EmitTriangle(out, sign, p0, p2, p1);
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

				const glm::vec3 pac = EdgeCrossing(iso, corners[a], corners[c]);
				const glm::vec3 pad = EdgeCrossing(iso, corners[a], corners[d]);
				const glm::vec3 pbd = EdgeCrossing(iso, corners[b], corners[d]);
				const glm::vec3 pbc = EdgeCrossing(iso, corners[b], corners[c]);

				EmitTriangle(out, sign, pac, pad, pbd);
				EmitTriangle(out, sign, pac, pbd, pbc);
			}
		}

		void GenerateLobeMesh(
			std::vector<IsosurfaceVertex> &out, const OrbitalGridData &grid, float isoValue, float sign)
		{
			const glm::ivec3 &dims = grid.dimensions;
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
								static_cast<float>(offset.x) / static_cast<float>(dims.x),
								static_cast<float>(offset.y) / static_cast<float>(dims.y),
								static_cast<float>(offset.z) / static_cast<float>(dims.z));
							const glm::vec3 position =
								grid.cell[0] * fractional.x + grid.cell[1] * fractional.y + grid.cell[2] * fractional.z;
							const float rawValue = grid.values[GridIndex(dims, offset.x, offset.y, offset.z)];
							cubeCorners[corner] = GridSample{position, sign * rawValue};
						}

						for (const std::array<int, 4> &tetrahedron : kCubeTetrahedra)
						{
							const std::array<GridSample, 4> tetCorners = {
								cubeCorners[tetrahedron[0]], cubeCorners[tetrahedron[1]],
								cubeCorners[tetrahedron[2]], cubeCorners[tetrahedron[3]]};
							PolygoniseTetrahedron(out, sign, isoValue, tetCorners);
						}
					}
				}
			}
		}
	} // namespace

	std::vector<IsosurfaceVertex> GenerateIsosurfaceMesh(const OrbitalGridData &grid, float isoValue)
	{
		std::vector<IsosurfaceVertex> vertices;
		if (grid.dimensions.x < 2 || grid.dimensions.y < 2 || grid.dimensions.z < 2 || isoValue <= 0.0f)
			return vertices;

		GenerateLobeMesh(vertices, grid, isoValue, 1.0f);
		GenerateLobeMesh(vertices, grid, isoValue, -1.0f);
		return vertices;
	}
} // namespace DefectStudio
