#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "Renderer/Path/PathStrokeMesher.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr float kPositionTolerance = 1.0e-5f;

		struct PositionEdge
		{
			glm::vec3 first{0.0f};
			glm::vec3 second{0.0f};
			std::size_t references = 0u;
		};

		[[nodiscard]] EvaluatedPath StraightPath()
		{
			EvaluatedPath path;
			path.totalLength = 2.0;
			path.samples = {
				{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {}, 0.0, 0.0, 0.0},
				{{2.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {}, 1.0, 2.0, 1.0}};
			return path;
		}

		[[nodiscard]] PathStrokeStyle DecoratedStyle(const PathDecorationKind kind, const float bevel,
			const std::uint32_t segments, const float shape)
		{
			PathStrokeStyle style;
			style.profile = StrokeProfile::Flat;
			style.width = 0.2f;
			style.ribbonThickness = 0.3f;
			style.ribbonBevel = bevel;
			style.ribbonBevelSegments = segments;
			style.ribbonBevelShape = shape;
			style.startDecoration = {.kind = kind, .filled = true};
			style.endDecoration = {.kind = kind, .filled = true};
			return style;
		}

		[[nodiscard]] bool Finite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		void ExpectFiniteMesh(const StrokeGeometry &geometry)
		{
			ASSERT_FALSE(geometry.tubeVertices.empty());
			for (const StrokeTubeVertex &vertex : geometry.tubeVertices)
			{
				EXPECT_TRUE(Finite(vertex.position));
				EXPECT_TRUE(Finite(vertex.normal));
			}
			for (const std::uint32_t index : geometry.indices)
				EXPECT_LT(index, geometry.tubeVertices.size());
		}

		void ExpectClosedMesh(const StrokeGeometry &geometry)
		{
			const auto samePosition = [](const glm::vec3 &first, const glm::vec3 &second) {
				return glm::distance(first, second) <= kPositionTolerance;
			};
			std::vector<PositionEdge> edges;
			ASSERT_EQ(geometry.indices.size() % 3u, 0u);
			for (std::size_t index = 0u; index < geometry.indices.size(); index += 3u)
			{
				std::array<glm::vec3, 3u> positions{};
				for (std::size_t corner = 0u; corner < positions.size(); ++corner)
				{
					ASSERT_LT(geometry.indices[index + corner], geometry.tubeVertices.size());
					positions[corner] = geometry.tubeVertices[geometry.indices[index + corner]].position;
				}
				if (samePosition(positions[0], positions[1]) || samePosition(positions[1], positions[2]) ||
					samePosition(positions[2], positions[0]))
					continue;
				for (std::size_t corner = 0u; corner < positions.size(); ++corner)
				{
					const glm::vec3 &first = positions[corner];
					const glm::vec3 &second = positions[(corner + 1u) % positions.size()];
					auto found = std::find_if(edges.begin(), edges.end(), [&](const PositionEdge &edge) {
						return (samePosition(edge.first, first) && samePosition(edge.second, second)) ||
							(samePosition(edge.first, second) && samePosition(edge.second, first));
					});
					if (found == edges.end())
						edges.push_back({first, second, 1u});
					else
						++found->references;
				}
			}
			for (const PositionEdge &edge : edges)
				EXPECT_EQ(edge.references, 2u) << "edge " << edge.first.x << "," << edge.first.y << "," << edge.first.z
					<< " -> " << edge.second.x << "," << edge.second.y << "," << edge.second.z;
		}

		void ExpectOutwardWinding(const StrokeGeometry &geometry)
		{
			for (std::size_t index = 0u; index < geometry.indices.size(); index += 3u)
			{
				const StrokeTubeVertex &first = geometry.tubeVertices[geometry.indices[index]];
				const StrokeTubeVertex &second = geometry.tubeVertices[geometry.indices[index + 1u]];
				const StrokeTubeVertex &third = geometry.tubeVertices[geometry.indices[index + 2u]];
				const glm::vec3 cross = glm::cross(second.position - first.position, third.position - first.position);
				if (glm::dot(cross, cross) <= 1.0e-10f)
					continue;
				const glm::vec3 averageNormal = first.normal + second.normal + third.normal;
				ASSERT_GT(glm::dot(averageNormal, averageNormal), 1.0e-8f);
				EXPECT_GT(glm::dot(glm::normalize(cross), glm::normalize(averageNormal)), 0.0f)
					<< "triangle " << index / 3u;
			}
		}

		[[nodiscard]] double SegmentDistanceSquared(const glm::dvec2 point, const glm::dvec2 first,
			const glm::dvec2 second)
		{
			const glm::dvec2 edge = second - first;
			const double lengthSquared = glm::dot(edge, edge);
			const double fraction = lengthSquared > 0.0 ?
				std::clamp(glm::dot(point - first, edge) / lengthSquared, 0.0, 1.0) : 0.0;
			const glm::dvec2 offset = point - (first + edge * fraction);
			return glm::dot(offset, offset);
		}

		[[nodiscard]] bool InsideOrOnBoundary(const glm::dvec2 point, const std::vector<glm::dvec2> &polygon)
		{
			bool inside = false;
			for (std::size_t index = 0u, previous = polygon.size() - 1u; index < polygon.size(); previous = index++)
			{
				const glm::dvec2 &first = polygon[previous];
				const glm::dvec2 &second = polygon[index];
				if (SegmentDistanceSquared(point, first, second) <= 1.0e-10)
					return true;
				if ((first.y > point.y) != (second.y > point.y) &&
					point.x < (second.x - first.x) * (point.y - first.y) / (second.y - first.y) + first.x)
					inside = !inside;
			}
			return inside;
		}

		void ExpectInsideSharpArrowSilhouette(const StrokeGeometry &geometry)
		{
			// Hand-derived from width=0.2, the default Arrow scales (length=0.6, half-width=0.25),
			// and a two-unit straight path. The vertical steps are the two reflex shaft shoulders.
			const std::vector<glm::dvec2> silhouette = {
				{0.0, 0.0}, {0.6, 0.25}, {0.6, 0.1}, {1.4, 0.1}, {1.4, 0.25}, {2.0, 0.0},
				{1.4, -0.25}, {1.4, -0.1}, {0.6, -0.1}, {0.6, -0.25}};
			for (const StrokeTubeVertex &vertex : geometry.tubeVertices)
			{
				EXPECT_TRUE(InsideOrOnBoundary(glm::dvec2(vertex.position), silhouette))
					<< "outside silhouette at " << vertex.position.x << "," << vertex.position.y;
				EXPECT_LE(std::abs(vertex.position.z), 0.15f + kPositionTolerance);
			}
		}
	} // namespace

	TEST(PathDecorationBevelTests, ArrowReflexShouldersStayInsideTheSharpSilhouette)
	{
		const StrokeGeometry geometry = BuildStroke(
			StraightPath(), DecoratedStyle(PathDecorationKind::Arrow, 0.04f, 4u, 0.5f));
		ExpectFiniteMesh(geometry);
		ExpectClosedMesh(geometry);
		ExpectOutwardWinding(geometry);
		ExpectInsideSharpArrowSilhouette(geometry);
	}

	TEST(PathDecorationBevelTests, ExcessiveArrowBevelLocallyClampsTheTip)
	{
		const StrokeGeometry geometry = BuildStroke(
			StraightPath(), DecoratedStyle(PathDecorationKind::Arrow, 0.4f, 4u, 0.5f));
		ExpectFiniteMesh(geometry);
		ExpectInsideSharpArrowSilhouette(geometry);
	}
}
