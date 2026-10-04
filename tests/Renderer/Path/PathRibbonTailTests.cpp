#include "Core/dspch.hpp"

#include <array>
#include <gtest/gtest.h>

#include "Renderer/Path/PathBindingResolver.hpp"
#include "Renderer/Path/PathSolidBevelGeometry.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"

namespace DefectStudio::Tests
{
	TEST(PathRibbonTailTests, CurvedBevelledArrowHasAClosedCapWithoutBackwardsVertices)
	{
		ScenePath path;
		path.nodes = {{AllocateElementId(path), {0, 0, 0}, {}}, {AllocateElementId(path), {4, 1, 0}, {}}};
		path.segments = {{AllocateElementId(path), CubicBezierSegmentData{
			{AllocateElementId(path), {-0.8f, -0.3f, 0}, BezierHandleType::Free},
			{AllocateElementId(path), {-2.0f, 0.1f, 0}, BezierHandleType::Free}}}};
		path.style.profile = StrokeProfile::Flat;
		path.style.width = 0.3f;
		path.style.ribbonThickness = 0.12f;
		path.style.startDecoration.kind = PathDecorationKind::None;
		path.style.endDecoration.kind = PathDecorationKind::Arrow;
		path.style.gradient.enabled = true;
		path.style.gradient.stops = {{0, {0, 0, 1}, 1}, {1, {1, 0, 0}, 1}};
		// Cover both the ring mesher and the united solid bevel mesher, at multiple LODs.
		for (const double tolerance : {0.002, 0.03})
			for (const float bevel : {0.0f, 0.04f})
				for (const bool smooth : {false, true})
				{
					SCOPED_TRACE(testing::Message() << tolerance << ',' << bevel << ',' << smooth);
					path.style.ribbonBevel = bevel;
					path.style.ribbonBevelSegments = 3;
					path.style.shadeSmooth = smooth;
					const auto evaluated = Tessellate(path, ResolveNodePositions(path, {}),
						TessellationSettings{tolerance, 12, 4096, {FrameSeed::Mode::FixedNormal, {0, 1, 0}}});
					ASSERT_FALSE(evaluated.samples.empty());
					const auto geometry = BuildStroke(evaluated, path.style);
					ASSERT_FALSE(geometry.indices.empty());
					const auto &start = evaluated.samples.front();
					bool hasCap = false;
					for (const auto &vertex : geometry.tubeVertices)
					{
						EXPECT_NEAR(glm::length(vertex.normal), 1.0f, 1.0e-4f);
						if (vertex.arcT != 0.0f) continue;
						EXPECT_GE(glm::dot(glm::dvec3(vertex.position) - start.position, start.tangent), -1.0e-5);
					}
					for (std::size_t index = geometry.shaft.firstIndex;
						index + 2 < geometry.shaft.firstIndex + geometry.shaft.indexCount; index += 3)
					{
						const auto &a = geometry.tubeVertices[geometry.indices[index]];
						const auto &b = geometry.tubeVertices[geometry.indices[index + 1]];
						const auto &c = geometry.tubeVertices[geometry.indices[index + 2]];
						if (a.arcT != 0 || b.arcT != 0 || c.arcT != 0) continue;
						const auto normal = glm::cross(b.position - a.position, c.position - a.position);
						if (glm::length(normal) < 1.0e-8f) continue;
						if (glm::dot(glm::dvec3(glm::normalize(normal)), -start.tangent) > 0.999)
						{
							hasCap = true;
							EXPECT_GT(glm::dot(glm::dvec3(a.normal), -start.tangent), 0.999);
							EXPECT_EQ(a.color, glm::vec4(0, 0, 1, 1));
						}
					}
					EXPECT_TRUE(hasCap);
				}
	}

	TEST(PathRibbonTailTests, BevelCornerEmitterKeepsTerminalVerticesInsideTheCapPlane)
	{
		detail::ThickFlatMesh mesh;
		mesh.vertices.push_back({});
		mesh.vertices.front().capOutwardNormal = glm::dvec3(-1, 0, 0);
		detail::ThickFlatBevelOutput output;
		detail::EmitThickFlatBevelPolygon(output, mesh, {0, 0, 0},
			{{-0.15, 0, 0}, {0.05, 0.2, 0}, {0.05, 0, 0.2}});
		ASSERT_EQ(output.vertices.size(), 3u);
		for (const auto &vertex : output.vertices) EXPECT_GE(vertex.position.x, 0.0f);
	}

	TEST(PathRibbonTailTests, NormalAveragingKeepsCapAndBevelEdgesHard)
	{
		StrokeGeometry geometry;
		geometry.tubeVertices = {
			{{0, 0, 0}, {0, 0, 1}, {}, 0, 0, 1}, {{1, 0, 0}, {0, 0, 1}, {}, 0, 0, 1},
			{{0, 1, 0}, {0, 0, 1}, {}, 0, 0, 1}, {{0, 0, 0}, {0, 1, 0}, {}, 0, 0, 2},
			{{0, 0, 1}, {0, 1, 0}, {}, 0, 0, 2}, {{1, 0, 0}, {0, 1, 0}, {}, 0, 0, 2}};
		geometry.indices = {0, 1, 2, 3, 4, 5};
		detail::SmoothThickFlatBevelNormals(geometry);
		EXPECT_EQ(geometry.tubeVertices[0].normal, glm::vec3(0, 0, 1));
		EXPECT_EQ(geometry.tubeVertices[3].normal, glm::vec3(0, 1, 0));
	}

	TEST(PathRibbonTailTests, SharpFallbackRetainsBothTrianglesOfAFoldedHandoffQuad)
	{
		// The first shaft band at the curved rigid arrow handoff from the caller's
		// closure failure. Its projection is not a simple polygon, so ear clipping
		// cannot replace the source strip's two triangles in the sharp fallback.
		detail::ThickFlatMesh mesh;
		for (const glm::dvec3 position : {glm::dvec3(0.318198055, -0.530330122, 0.1),
			glm::dvec3(0.454998761, -0.466198415, 0.1), glm::dvec3(0.559524357, -0.184996694, 0.1),
			glm::dvec3(0.530330062, -0.318198055, 0.1)})
			mesh.vertices.push_back({position});
		mesh.faces.push_back({{0, 1, 2, 3}, {}, detail::ThickFlatFaceOwner::Shaft});
		StrokeGeometry geometry;
		detail::FinalizeSharpThickFlatMesh(mesh, geometry);
		ASSERT_EQ(geometry.indices.size(), 6u);
		for (std::size_t edge = 0; edge < mesh.vertices.size(); ++edge)
		{
			const glm::vec3 a(mesh.vertices[edge].position), b(mesh.vertices[(edge + 1) % 4].position);
			std::size_t references = 0;
			for (std::size_t triangle = 0; triangle < geometry.indices.size(); triangle += 3)
				for (std::size_t corner = 0; corner < 3; ++corner)
				{
					const auto &first = geometry.tubeVertices[geometry.indices[triangle + corner]].position;
					const auto &second = geometry.tubeVertices[geometry.indices[triangle + (corner + 1) % 3]].position;
					if ((first == a && second == b) || (first == b && second == a)) ++references;
				}
			EXPECT_EQ(references, 1u);
		}
	}
}
