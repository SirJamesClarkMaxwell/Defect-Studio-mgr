#include "Core/dspch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

#include "Renderer/Path/PathStrokeMesher.hpp"
#include "Renderer/Path/PathSolidBevelGeometry.hpp"
#include "Renderer/Path/PathSolidBevelTopology.hpp"

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
			int winding = 0;
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
						edges.push_back({first, second, 1u, 1});
					else
					{
						++found->references;
						found->winding += samePosition(found->first, first) ? 1 : -1;
					}
				}
			}
			for (const PositionEdge &edge : edges)
			{
				EXPECT_EQ(edge.references, 2u) << "edge " << edge.first.x << "," << edge.first.y << "," << edge.first.z
					<< " -> " << edge.second.x << "," << edge.second.y << "," << edge.second.z;
				EXPECT_EQ(edge.winding, 0) << "Adjacent faces must traverse their shared edge in opposite directions.";
			}
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
					<< "outside silhouette at " << vertex.position.x << "," << vertex.position.y << ","
					<< vertex.position.z << " normal=" << vertex.normal.x << "," << vertex.normal.y << ","
					<< vertex.normal.z;
				EXPECT_LE(std::abs(vertex.position.z), 0.15f + kPositionTolerance);
			}
		}

		[[nodiscard]] detail::ThickFlatMesh Prism(const std::vector<glm::dvec2> &outline,
			const double halfThickness = 0.1)
		{
			detail::ThickFlatMesh mesh;
			for (const double z : {halfThickness, -halfThickness})
				for (const glm::dvec2 point : outline)
					mesh.vertices.push_back({glm::dvec3(point, z)});
			std::vector<std::uint32_t> top;
			std::vector<std::uint32_t> bottom;
			for (std::uint32_t index = 0u; index < outline.size(); ++index)
			{
				top.push_back(index);
				bottom.push_back(static_cast<std::uint32_t>(outline.size() * 2u - 1u - index));
			}
			mesh.faces.push_back({top, std::vector<bool>(top.size(), true)});
			mesh.faces.push_back({bottom, std::vector<bool>(bottom.size(), true)});
			for (std::uint32_t index = 0u; index < outline.size(); ++index)
			{
				const std::uint32_t next = (index + 1u) % static_cast<std::uint32_t>(outline.size());
				mesh.faces.push_back({{index, index + static_cast<std::uint32_t>(outline.size()),
					next + static_cast<std::uint32_t>(outline.size()), next}, std::vector<bool>(4u, true)});
			}
			return mesh;
		}

		[[nodiscard]] std::vector<glm::dvec3> FaceNormals(const detail::ThickFlatMesh &mesh)
		{
			std::vector<glm::dvec3> result;
			for (const detail::ThickFlatMeshFace &face : mesh.faces)
			{
				glm::dvec3 normal(0.0);
				for (std::size_t index = 0u; index < face.vertices.size(); ++index)
					normal += glm::cross(mesh.vertices[face.vertices[index]].position,
						mesh.vertices[face.vertices[(index + 1u) % face.vertices.size()]].position);
				result.push_back(glm::normalize(normal));
			}
			return result;
		}
	} // namespace

	TEST(PathDecorationBevelTests, SquareCornerTurnProducesConvexPatch)
	{
		const detail::ThickFlatMesh mesh = Prism({{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}});
		detail::ThickFlatBevelTopology topology;
		ASSERT_TRUE(detail::BuildThickFlatBevelTopology(mesh, FaceNormals(mesh), 0.1, topology));
		for (const auto &[vertex, info] : topology.vertices)
		{
			SCOPED_TRACE(vertex);
			EXPECT_EQ(info.turn, detail::ThickFlatBevelTurn::Convex);
		}
	}

	TEST(PathDecorationBevelTests, ArrowShoulderTurnProducesReentrantPatch)
	{
		const std::vector<glm::dvec2> outline = {
			{0.0, 0.0}, {2.0, 0.0}, {2.0, 1.0}, {1.0, 1.0}, {1.0, 2.0}, {0.0, 2.0}};
		const detail::ThickFlatMesh mesh = Prism(outline);
		detail::ThickFlatBevelTopology topology;
		ASSERT_TRUE(detail::BuildThickFlatBevelTopology(mesh, FaceNormals(mesh), 0.1, topology));
		EXPECT_EQ(topology.vertices.at(3u).turn, detail::ThickFlatBevelTurn::Reflex);
		EXPECT_EQ(topology.vertices.at(static_cast<std::uint32_t>(outline.size()) + 3u).turn,
			detail::ThickFlatBevelTurn::Reflex);
	}

	TEST(PathDecorationBevelTests, CircleShallowTurnsRemainAContinuousChain)
	{
		std::vector<glm::dvec2> outline;
		for (std::size_t index = 0u; index < 32u; ++index)
		{
			const double angle = 2.0 * std::numbers::pi * static_cast<double>(index) / 32.0;
			outline.emplace_back(std::cos(angle), std::sin(angle));
		}
		const detail::ThickFlatMesh mesh = Prism(outline);
		detail::ThickFlatBevelTopology topology;
		ASSERT_TRUE(detail::BuildThickFlatBevelTopology(mesh, FaceNormals(mesh), 0.1, topology));
		for (const auto &[vertex, info] : topology.vertices)
		{
			SCOPED_TRACE(vertex);
			EXPECT_EQ(info.turn, detail::ThickFlatBevelTurn::Smooth);
		}
	}

	TEST(PathDecorationBevelTests, LocalRadiusLimitsArrowDiamondKiteAndShortBarCorners)
	{
		struct Case
		{
			std::vector<glm::dvec2> outline;
			std::uint32_t vertex = 0u;
			double expectedRadius = 0.0;
		};
		const std::array cases = {
			Case{{{0.0, 0.0}, {3.0, -1.25}, {3.0, 1.25}}, 0u, 0.609375},
			Case{{{0.0, 0.0}, {1.5, -1.25}, {3.0, 0.0}, {1.5, 1.25}}, 0u, 0.732209},
			Case{{{0.0, 0.0}, {1.0, -1.25}, {3.0, 0.0}, {1.0, 1.25}}, 2u, 0.663327},
			Case{{{0.0, -1.25}, {0.3, -1.25}, {0.3, 1.25}, {0.0, 1.25}}, 0u, 0.135}};

		for (const Case &testCase : cases)
		{
			const detail::ThickFlatMesh mesh = Prism(testCase.outline, 10.0);
			detail::ThickFlatBevelTopology topology;
			ASSERT_TRUE(detail::BuildThickFlatBevelTopology(mesh, FaceNormals(mesh), 1.0, topology));
			EXPECT_NEAR(topology.vertices.at(testCase.vertex).radius, testCase.expectedRadius, 1.0e-5);
		}
	}

	TEST(PathDecorationBevelTests, ArrowReflexShouldersStayInsideTheSharpSilhouette)
	{
		const StrokeGeometry geometry = BuildStroke(
			StraightPath(), DecoratedStyle(PathDecorationKind::Arrow, 0.04f, 4u, 0.5f));
		ExpectFiniteMesh(geometry);
		ExpectClosedMesh(geometry);
		ExpectOutwardWinding(geometry);
		ExpectInsideSharpArrowSilhouette(geometry);
	}

	TEST(PathDecorationBevelTests, TaperedStripEmitsItsNondegenerateTriangle)
	{
		detail::ThickFlatMesh mesh;
		mesh.vertices = {{{0.0, 0.0, 0.0}}, {{1.0, 0.0, 0.0}}, {{0.0, 1.0, 0.0}}};
		detail::ThickFlatBevelOutput output;
		detail::EmitThickFlatBevelPolygon(output, mesh, {0, 1, 2, 0},
			{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 0.0}});
		ASSERT_EQ(output.indices.size(), 3u);
		for (const auto &vertex : output.vertices) EXPECT_GT(vertex.normal.z, 0.0f);
	}

	TEST(PathDecorationBevelTests, SingleSegmentArrowKeepsOppositeSharedEdgeWinding)
	{
		const auto geometry = BuildStroke(StraightPath(), DecoratedStyle(PathDecorationKind::Arrow, 0.04f, 1u, 0.5f));
		ExpectClosedMesh(geometry);
		ExpectOutwardWinding(geometry);
	}

	TEST(PathDecorationBevelTests, ShapeZeroReflexShouldersKeepTheirFanAtEvenSegmentCounts)
	{
		for (const auto segments : {4u, 16u})
		{
			SCOPED_TRACE(segments);
			const auto geometry = BuildStroke(StraightPath(), DecoratedStyle(PathDecorationKind::Arrow, 0.04f, segments, 0.0f));
			ExpectFiniteMesh(geometry);
			ExpectClosedMesh(geometry);
			ExpectOutwardWinding(geometry);
			ExpectInsideSharpArrowSilhouette(geometry);
		}
	}

	TEST(PathDecorationBevelTests, StraightContourMidpointDoesNotAcquireADuplicateCornerPatch)
	{
		const auto mesh = Prism({{-1.0, -1.0}, {0.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}});
		auto style = DecoratedStyle(PathDecorationKind::None, 0.04f, 4u, 0.5f);
		StrokeGeometry geometry;
		detail::FinalizeThickFlatMesh(mesh, style, geometry);
		ExpectFiniteMesh(geometry);
		ExpectClosedMesh(geometry);
		ExpectOutwardWinding(geometry);
	}

	TEST(PathDecorationBevelTests, CornerEdgeOrderFollowsTheOutwardFaceWinding)
	{
		const auto mesh = Prism({{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}});
		detail::ThickFlatBevelTopology topology;
		ASSERT_TRUE(detail::BuildThickFlatBevelTopology(mesh, FaceNormals(mesh), 0.04, topology));
		for (const auto &[vertex, info] : topology.vertices)
			for (std::size_t index = 0; index < info.orderedEdges.size(); ++index)
			{
				const auto &incoming = topology.edges[info.orderedEdges[index]];
				const auto &outgoing = topology.edges[info.orderedEdges[(index + info.orderedEdges.size() - 1) % info.orderedEdges.size()]];
				bool found = false;
				for (const auto &first : incoming.incidents)
					for (const auto &second : outgoing.incidents)
						if (first.face == second.face)
						{
							const auto &face = mesh.faces[first.face];
							EXPECT_EQ(face.vertices[(first.corner + 1) % face.vertices.size()], vertex);
							EXPECT_EQ(face.vertices[second.corner], vertex);
							found = true;
						}
				EXPECT_TRUE(found);
			}
	}

	TEST(PathDecorationBevelTests, ArrowSquareAndBarBevelStayClosedInsideSharpBounds)
	{
		for (const auto kind : {PathDecorationKind::Arrow, PathDecorationKind::Square, PathDecorationKind::Bar})
			for (const auto segments : {1u, 4u, 16u})
				for (const float shape : {0.0f, 0.5f, 1.0f})
				{
					SCOPED_TRACE(testing::Message() << static_cast<int>(kind) << "," << segments << "," << shape);
					const auto sharp = BuildStroke(StraightPath(), DecoratedStyle(kind, 0.0f, segments, shape));
					ASSERT_FALSE(sharp.tubeVertices.empty());
					glm::vec3 low(1.0e10f), high(-1.0e10f);
					for (const auto &vertex : sharp.tubeVertices)
					{
						low = glm::min(low, vertex.position);
						high = glm::max(high, vertex.position);
					}
					const auto bevel = BuildStroke(StraightPath(), DecoratedStyle(kind, 0.04f, segments, shape));
					ExpectFiniteMesh(bevel);
					ExpectClosedMesh(bevel);
					ExpectOutwardWinding(bevel);
					double signedVolume = 0.0;
					for (std::size_t index = 0; index < bevel.indices.size(); index += 3)
					{
						const auto a = glm::dvec3(bevel.tubeVertices[bevel.indices[index]].position);
						const auto b = glm::dvec3(bevel.tubeVertices[bevel.indices[index + 1]].position);
						const auto c = glm::dvec3(bevel.tubeVertices[bevel.indices[index + 2]].position);
						signedVolume += glm::dot(a, glm::cross(b, c)) / 6.0;
					}
					EXPECT_GT(signedVolume, 0.0); // a consistently wound closed solid must face outward
					for (const auto &vertex : bevel.tubeVertices)
						for (int axis = 0; axis < 3; ++axis)
						{
							EXPECT_GE(vertex.position[axis], low[axis] - kPositionTolerance);
							EXPECT_LE(vertex.position[axis], high[axis] + kPositionTolerance);
						}
					if (kind == PathDecorationKind::Arrow)
						ExpectInsideSharpArrowSilhouette(bevel);
				}
	}

	TEST(PathDecorationBevelTests, BevelPartsKeepTheOtherPartSharpAndTheSolidClosed)
	{
		for (const auto parts : {PathBevelParts::Shaft, PathBevelParts::Decorations})
		{
			auto style = DecoratedStyle(PathDecorationKind::Square, 0.04f, 4u, 0.5f);
			style.ribbonBevelParts = parts;
			const auto geometry = BuildStroke(StraightPath(), style);
			ExpectClosedMesh(geometry);
			ExpectOutwardWinding(geometry);
			const glm::vec3 sharpPoint = parts == PathBevelParts::Shaft
				? glm::vec3(0.0f, 0.25f, 0.15f) : glm::vec3(1.0f, 0.1f, 0.15f);
			EXPECT_TRUE(std::any_of(geometry.tubeVertices.begin(), geometry.tubeVertices.end(),
				[&](const auto &vertex) {
					return parts == PathBevelParts::Shaft ? glm::distance(vertex.position, sharpPoint) < kPositionTolerance
						: std::abs(vertex.position.y - sharpPoint.y) < kPositionTolerance &&
						std::abs(vertex.position.z - sharpPoint.z) < kPositionTolerance &&
						vertex.position.x > 0.59f && vertex.position.x < 1.41f;
				}));
		}
	}

	TEST(PathDecorationBevelTests, ExcessiveArrowBevelLocallyClampsTheTip)
	{
		const StrokeGeometry geometry = BuildStroke(
			StraightPath(), DecoratedStyle(PathDecorationKind::Arrow, 0.4f, 4u, 0.5f));
		ExpectFiniteMesh(geometry);
		ExpectInsideSharpArrowSilhouette(geometry);
	}
}
