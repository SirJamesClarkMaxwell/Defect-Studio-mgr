#include "Core/dspch.hpp"

#include <algorithm>

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <utility>

#include "Renderer/Path/PathDash.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		EvaluatedPath StraightPath()
		{
			EvaluatedPath path;
			path.totalLength = 2.0;
			path.samples = {{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {}, 0.0, 0.0, 0.0},
				{{2.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {}, 1.0, 2.0, 1.0}};
			return path;
		}
		EvaluatedSample Sample(const glm::dvec3 &position, const glm::dvec3 &tangent, const double arcLength, const double total)
		{
			const glm::dvec3 normal(0.0, 0.0, 1.0);
			return {position, tangent, normal, glm::cross(tangent, normal), {}, 0.0, arcLength, total > 0.0 ? arcLength / total : 0.0};
		}

		// A right angle in the XY plane, then the same path doubling straight back on itself. The second
		// is the case a naive mesher crosses two parallel tangents on and gets a zero-length normal.
		EvaluatedPath BentPath(const bool doubleBack)
		{
			EvaluatedPath path;
			path.totalLength = 2.0;
			const glm::dvec3 second = doubleBack ? glm::dvec3(-1.0, 0.0, 0.0) : glm::dvec3(0.0, 1.0, 0.0);
			path.samples = {Sample(glm::dvec3(0.0), glm::dvec3(1.0, 0.0, 0.0), 0.0, 2.0),
				Sample(glm::dvec3(1.0, 0.0, 0.0), glm::dvec3(1.0, 0.0, 0.0), 1.0, 2.0),
				Sample(glm::dvec3(1.0, 0.0, 0.0) + second, second, 2.0, 2.0)};
			return path;
		}

		// Edges of the triangle list that belong to exactly one triangle. A closed surface has none with
		// a non-zero length; the degenerate ones sit on a cap apex where every vertex is the same point.
		std::size_t OpenBoundaryEdges(const StrokeGeometry &geometry)
		{
			std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
			for (std::size_t index = 0; index + 2u < geometry.indices.size(); index += 3u)
				for (std::size_t corner = 0; corner < 3u; ++corner)
				{
					const std::uint32_t a = geometry.indices[index + corner];
					const std::uint32_t b = geometry.indices[index + (corner + 1u) % 3u];
					++edges[{std::min(a, b), std::max(a, b)}];
				}
			std::size_t open = 0;
			for (const auto &[edge, count] : edges)
				if (count == 1 && glm::distance(geometry.tubeVertices[edge.first].position, geometry.tubeVertices[edge.second].position) > 1e-6f)
					++open;
			return open;
		}

		bool AllIndicesInBounds(const StrokeGeometry &geometry)
		{
			const std::size_t vertexCount = geometry.tubeVertices.empty() ? geometry.ribbonVertices.size() : geometry.tubeVertices.size();
			for (const std::uint32_t index : geometry.indices)
				if (index >= vertexCount)
					return false;
			return geometry.indices.size() % 3u == 0u;
		}

		bool Finite(const glm::vec3 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}

		bool Finite(const glm::vec4 &value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
		}

		bool AllVerticesFinite(const StrokeGeometry &geometry)
		{
			for (const StrokeTubeVertex &vertex : geometry.tubeVertices)
				if (!Finite(vertex.position) || !Finite(vertex.normal) || !Finite(vertex.color))
					return false;
			for (const StrokeRibbonVertex &vertex : geometry.ribbonVertices)
				if (!Finite(vertex.position) || !Finite(vertex.tangent) || !Finite(vertex.color))
					return false;
			return true;
		}

		constexpr PathDecorationKind kAllDecorations[] = {PathDecorationKind::None, PathDecorationKind::Arrow,
			PathDecorationKind::Stealth, PathDecorationKind::OpenArrow, PathDecorationKind::Bar,
			PathDecorationKind::Circle, PathDecorationKind::Square, PathDecorationKind::Diamond};
	} // namespace
	TEST(PathStrokeMesherTests, GradientClampsAndInterpolatesStops)
	{
		PathStrokeStyle style;
		style.color = glm::vec3(0.2f);
		style.alpha = 0.4f;
		const glm::vec4 flat = SampleStrokeColor(style, 0.5);
		EXPECT_FLOAT_EQ(flat.r, 0.2f);
		EXPECT_FLOAT_EQ(flat.a, 0.4f);
		style.gradient.enabled = true;
		style.gradient.stops = {{0.0f, glm::vec3(1.0f, 0.0f, 0.0f), 0.2f}, {1.0f, glm::vec3(0.0f, 0.0f, 1.0f), 0.8f}};
		EXPECT_FLOAT_EQ(SampleStrokeColor(style, -1.0).r, 1.0f);
		EXPECT_FLOAT_EQ(SampleStrokeColor(style, -1.0).a, 0.2f);
		EXPECT_FLOAT_EQ(SampleStrokeColor(style, 2.0).b, 1.0f);
		EXPECT_FLOAT_EQ(SampleStrokeColor(style, 2.0).a, 0.8f);
		const glm::vec4 middle = SampleStrokeColor(style, 0.5);
		EXPECT_NEAR(middle.r, 0.5f, 1e-6f);
		EXPECT_NEAR(middle.b, 0.5f, 1e-6f);
		EXPECT_NEAR(middle.a, 0.5f, 1e-6f);
	}

	TEST(PathStrokeMesherTests, InvalidStyleAndGradientProduceDiagnosticsOnly)
	{
		EvaluatedPath path;
		PathStrokeStyle style;
		style.width = 0.0f;
		const StrokeGeometry invalidStyle = BuildStroke(path, style);
		ASSERT_EQ(invalidStyle.diagnostics.size(), 1u);
		EXPECT_EQ(invalidStyle.diagnostics[0].code, PathDiagnosticCode::InvalidStrokeStyle);
		style.width = 1.0f;
		style.gradient.enabled = true;
		style.gradient.stops = {{0.8f, glm::vec3(1.0f), 1.0f}, {0.2f, glm::vec3(1.0f), 1.0f}};
		const StrokeGeometry invalidGradient = BuildStroke(path, style);
		ASSERT_EQ(invalidGradient.diagnostics.size(), 1u);
		EXPECT_EQ(invalidGradient.diagnostics[0].code, PathDiagnosticCode::InvalidGradient);
	}

	TEST(PathStrokeMesherTests, ProfilesDashesAndDecorationsProduceFiniteIndexedGeometry)
	{
		const EvaluatedPath path = StraightPath();
		PathStrokeStyle style;
		style.width = 0.2f;
		style.radialSegments = 6;
		style.dash = {true, 0.5f, 0.25f, 0.0f};
		style.startDecoration.kind = PathDecorationKind::Arrow;
		style.endDecoration.kind = PathDecorationKind::Diamond;
		const StrokeGeometry round = BuildStroke(path, style);
		EXPECT_FALSE(round.tubeVertices.empty());
		EXPECT_TRUE(round.ribbonVertices.empty());
		EXPECT_FALSE(round.startDecoration.IsEmpty());
		EXPECT_FALSE(round.endDecoration.IsEmpty());
		EXPECT_NEAR(round.dashedLength, DashCoverage(BuildDashIntervals(round.shaftRange.start, round.shaftRange.end, style.dash)), 1e-9);
		for (const StrokeTubeVertex &vertex : round.tubeVertices)
		{
			EXPECT_NEAR(glm::length(vertex.normal), 1.0f, 1e-6f);
			EXPECT_TRUE(std::isfinite(vertex.dashCoord));
		}
		style.profile = StrokeProfile::Flat;
		const StrokeGeometry flat = BuildStroke(path, style);
		EXPECT_TRUE(flat.tubeVertices.empty());
		EXPECT_FALSE(flat.ribbonVertices.empty());
		// Shaft vertices carry the shader's expansion sign; decoration vertices carry 0 because their
		// own varying half width is already baked into the position (see AppendDecoration).
		for (const StrokeRibbonVertex &vertex : flat.ribbonVertices)
			EXPECT_TRUE(vertex.side == -1.0f || vertex.side == 0.0f || vertex.side == 1.0f);
		const std::size_t shaftSides = static_cast<std::size_t>(std::count_if(
			flat.ribbonVertices.begin(), flat.ribbonVertices.end(),
			[](const StrokeRibbonVertex &vertex) { return vertex.side != 0.0f; }));
		EXPECT_GT(shaftSides, 0u);
		EXPECT_LT(shaftSides, flat.ribbonVertices.size()); // this path has an end decoration
	}

	TEST(PathStrokeMesherTests, CameraFacingDecorationAndShaftUseTheSameCameraPlane)
	{
		PathStrokeStyle style;
		style.profile = StrokeProfile::CameraFacing;
		style.width = 0.2f;
		style.endDecoration.kind = PathDecorationKind::Arrow;
		const StrokeGeometry geometry = BuildStroke(StraightPath(), style);
		ASSERT_FALSE(geometry.endDecoration.IsEmpty());
		ASSERT_FALSE(geometry.shaft.IsEmpty());

		for (const glm::vec3 camera : {glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f, 5.0f, 5.0f)})
		{
			const auto expanded = [camera](const StrokeRibbonVertex &vertex) {
				const glm::vec3 tangent = glm::normalize(vertex.tangent);
				const glm::vec3 view = glm::normalize(camera - vertex.position);
				const glm::vec3 axis = glm::normalize(glm::cross(tangent, view));
				return vertex.position + vertex.side * vertex.halfWidth * axis;
			};
			const glm::vec3 endpoint = expanded(geometry.ribbonVertices.front());
			const glm::vec3 tangent = glm::normalize(geometry.ribbonVertices.front().tangent);
			const glm::vec3 axis = glm::normalize(glm::cross(tangent, glm::normalize(camera - geometry.ribbonVertices.front().position)));
			const glm::vec3 planeNormal = glm::normalize(glm::cross(tangent, axis));
			const auto expectOnPlane = [&](const StrokeRibbonVertex &vertex) {
				EXPECT_NEAR(glm::dot(expanded(vertex) - endpoint, planeNormal), 0.0f, 1e-5f);
			};

			for (std::uint32_t index = geometry.endDecoration.firstIndex;
				index < geometry.endDecoration.firstIndex + geometry.endDecoration.indexCount; ++index)
				expectOnPlane(geometry.ribbonVertices[geometry.indices[index]]);
			for (std::uint32_t index = geometry.shaft.firstIndex;
				index < geometry.shaft.firstIndex + geometry.shaft.indexCount; ++index)
				expectOnPlane(geometry.ribbonVertices[geometry.indices[index]]);
		}
	}

	TEST(PathStrokeMesherTests, FlatAndRoundDecorationVerticesRemainStable)
	{
		const auto expectNear = [](const glm::vec3 &actual, const glm::vec3 &expected) {
			EXPECT_NEAR(actual.x, expected.x, 1e-5f);
			EXPECT_NEAR(actual.y, expected.y, 1e-5f);
			EXPECT_NEAR(actual.z, expected.z, 1e-5f);
		};
		PathStrokeStyle style;
		style.width = 0.2f;
		style.endDecoration.kind = PathDecorationKind::Arrow;

		style.profile = StrokeProfile::Flat;
		const StrokeGeometry flat = BuildStroke(StraightPath(), style);
		ASSERT_GE(flat.ribbonVertices.size(), 6u);
		// Recorded from a run, not reasoned out: that is what makes this a pin. The offset axis is
		// the binormal - Sample() seeds normal (0,0,1) and binormal cross(tangent, normal) = (0,-1,0)
		// - which is the detail a hand-written expectation gets wrong.
		expectNear(flat.ribbonVertices[0].position, glm::vec3(2.0f, 0.0f, 0.0f));
		expectNear(flat.ribbonVertices[2].position, glm::vec3(1.8f, -0.1f, 0.0f));
		expectNear(flat.ribbonVertices[3].position, glm::vec3(1.8f, 0.1f, 0.0f));
		expectNear(flat.ribbonVertices[4].position, glm::vec3(1.8f, 0.0f, 0.0f));

		style.profile = StrokeProfile::Round;
		style.radialSegments = 4;
		const StrokeGeometry round = BuildStroke(StraightPath(), style);
		ASSERT_GE(round.tubeVertices.size(), 8u);
		// The ring's off-axis components land at ~1e-17 rather than exactly zero, so this compares
		// near rather than equal - EXPECT_EQ on a float that went through a trig function is a test
		// that fails on a compiler flag change, not on a regression.
		expectNear(round.tubeVertices[0].position, glm::vec3(2.0f, 0.0f, 0.0f));
		expectNear(round.tubeVertices[4].position, glm::vec3(1.8f, 0.1f, 0.0f));
		expectNear(round.tubeVertices[5].position, glm::vec3(1.8f, 0.0f, 0.1f));
		expectNear(round.tubeVertices[6].position, glm::vec3(1.8f, -0.1f, 0.0f));
	}

	TEST(PathStrokeMesherTests, BendsOfNinetyAndOneHundredEightyDegreesKeepFiniteUnitNormals)
	{
		PathStrokeStyle style;
		style.width = 0.2f;
		style.radialSegments = 8;
		for (const bool doubleBack : {false, true})
		{
			const StrokeGeometry geometry = BuildStroke(BentPath(doubleBack), style);
			ASSERT_FALSE(geometry.tubeVertices.empty());
			EXPECT_TRUE(AllIndicesInBounds(geometry));
			EXPECT_TRUE(AllVerticesFinite(geometry));
			for (const StrokeTubeVertex &vertex : geometry.tubeVertices)
				EXPECT_NEAR(glm::length(vertex.normal), 1.0f, 1e-6f);
		}
	}

	TEST(PathStrokeMesherTests, CapsCloseTheTubeAndButtAddsNoGeometry)
	{
		PathStrokeStyle style;
		style.width = 0.2f;
		style.radialSegments = 8;
		const StrokeGeometry butt = BuildStroke(StraightPath(), style);
		style.cap = PathLineCap::Square;
		const StrokeGeometry square = BuildStroke(StraightPath(), style);
		style.cap = PathLineCap::Round;
		const StrokeGeometry round = BuildStroke(StraightPath(), style);

		// Butt is the bare sweep: a square cap only slides the end rings outwards, a round cap adds
		// hemispheres, and only the round one leaves no hole at either end.
		EXPECT_EQ(butt.indices.size(), square.indices.size());
		EXPECT_EQ(butt.tubeVertices.size(), square.tubeVertices.size());
		EXPECT_GT(round.indices.size(), butt.indices.size());
		EXPECT_EQ(OpenBoundaryEdges(butt), 2u * style.radialSegments);
		EXPECT_EQ(OpenBoundaryEdges(round), 0u);
		EXPECT_TRUE(AllIndicesInBounds(round));
		EXPECT_GT(glm::distance(square.tubeVertices.front().position, butt.tubeVertices.front().position), 0.0f);

		// The join is documented as a no-op for the Round profile - assert that, so it cannot quietly
		// start changing the tube without the contract being updated with it.
		style.cap = PathLineCap::Butt;
		style.join = PathLineJoin::Round;
		EXPECT_EQ(BuildStroke(BentPath(false), style).indices.size(), [&] {
			PathStrokeStyle bevel = style;
			bevel.join = PathLineJoin::Bevel;
			return BuildStroke(BentPath(false), bevel).indices.size();
		}());
	}

	TEST(PathStrokeMesherTests, EveryDecorationKindIndexesInBoundsInBothProfiles)
	{
		for (const PathDecorationKind kind : kAllDecorations)
			for (const StrokeProfile profile : {StrokeProfile::Round, StrokeProfile::Flat})
			{
				PathStrokeStyle style;
				style.profile = profile;
				style.width = 0.2f;
				style.radialSegments = 5;
				style.startDecoration.kind = kind;
				style.endDecoration.kind = kind;
				const StrokeGeometry geometry = BuildStroke(StraightPath(), style);
				EXPECT_TRUE(AllIndicesInBounds(geometry)) << static_cast<int>(kind) << " " << static_cast<int>(profile);
				EXPECT_TRUE(AllVerticesFinite(geometry)) << static_cast<int>(kind);
				const bool decorated = kind != PathDecorationKind::None;
				EXPECT_EQ(!geometry.startDecoration.IsEmpty(), decorated) << static_cast<int>(kind);
				EXPECT_EQ(!geometry.endDecoration.IsEmpty(), decorated) << static_cast<int>(kind);
				EXPECT_LE(geometry.startDecoration.firstIndex + geometry.startDecoration.indexCount, geometry.indices.size());
				EXPECT_LE(geometry.endDecoration.firstIndex + geometry.endDecoration.indexCount, geometry.indices.size());
			}
	}

	TEST(PathStrokeMesherTests, ArcTRisesWithTheShaftAndCarriesTheGradientColour)
	{
		PathStrokeStyle style;
		style.width = 0.2f;
		style.radialSegments = 4;
		style.gradient.enabled = true;
		style.gradient.stops = {{0.0f, glm::vec3(1.0f, 0.0f, 0.0f), 1.0f}, {0.5f, glm::vec3(0.0f, 1.0f, 0.0f), 0.5f}, {1.0f, glm::vec3(0.0f, 0.0f, 1.0f), 0.0f}};
		const StrokeGeometry geometry = BuildStroke(StraightPath(), style);
		ASSERT_FALSE(geometry.tubeVertices.empty());
		float previous = -1.0f;
		for (const StrokeTubeVertex &vertex : geometry.tubeVertices)
		{
			EXPECT_GE(vertex.arcT, previous);
			previous = std::max(previous, vertex.arcT);
			const glm::vec4 expected = SampleStrokeColor(style, vertex.arcT);
			EXPECT_NEAR(vertex.color.r, expected.r, 1e-6f);
			EXPECT_NEAR(vertex.color.a, expected.a, 1e-6f);
			EXPECT_NEAR(vertex.dashCoord, vertex.arcT * static_cast<float>(StraightPath().totalLength), 1e-5f);
		}
		EXPECT_NEAR(geometry.tubeVertices.front().arcT, 0.0f, 1e-6f);
		EXPECT_NEAR(geometry.tubeVertices.back().arcT, 1.0f, 1e-6f);
	}

	TEST(PathStrokeMesherTests, DecorationsLongerThanThePathLeaveNoShaft)
	{
		PathStrokeStyle style;
		style.width = 2.0f; // an Arrow trims a full stroke width per end, so 2 + 2 > the 2-unit path
		style.radialSegments = 5;
		style.startDecoration.kind = PathDecorationKind::Arrow;
		style.endDecoration.kind = PathDecorationKind::Arrow;
		const StrokeGeometry geometry = BuildStroke(StraightPath(), style);
		ASSERT_EQ(geometry.diagnostics.size(), 1u);
		EXPECT_EQ(geometry.diagnostics[0].code, PathDiagnosticCode::DecorationsExceedPathLength);
		EXPECT_TRUE(geometry.shaft.IsEmpty());
		EXPECT_TRUE(geometry.shaftRange.IsEmpty());
		EXPECT_FALSE(geometry.startDecoration.IsEmpty());
		EXPECT_FALSE(geometry.endDecoration.IsEmpty());
		EXPECT_TRUE(AllIndicesInBounds(geometry));
	}

	TEST(PathStrokeMesherTests, DegeneratePathsProduceNoGeometryAndNoNonFiniteVertices)
	{
		PathStrokeStyle style;
		style.width = 0.2f;
		style.radialSegments = 6;
		style.cap = PathLineCap::Round;
		style.dash = {true, 0.3f, 0.2f, 0.05f};

		EvaluatedPath empty;
		EXPECT_TRUE(BuildStroke(empty, style).indices.empty());

		EvaluatedPath single;
		single.totalLength = 1.0;
		single.samples = {Sample(glm::dvec3(0.0), glm::dvec3(1.0, 0.0, 0.0), 0.0, 1.0)};
		EXPECT_TRUE(BuildStroke(single, style).indices.empty());

		EvaluatedPath zeroLength;
		zeroLength.samples = {Sample(glm::dvec3(0.0), glm::dvec3(1.0, 0.0, 0.0), 0.0, 0.0),
			Sample(glm::dvec3(0.0), glm::dvec3(1.0, 0.0, 0.0), 0.0, 0.0)};
		EXPECT_TRUE(BuildStroke(zeroLength, style).indices.empty());

		// Coincident interior samples and a full doubling back are the two shapes that produce a zero
		// vector to normalise; both must still come out finite.
		EvaluatedPath coincident = BentPath(true);
		coincident.samples.insert(coincident.samples.begin() + 1, coincident.samples[1]);
		const StrokeGeometry geometry = BuildStroke(coincident, style);
		EXPECT_TRUE(AllVerticesFinite(geometry));
		EXPECT_TRUE(AllIndicesInBounds(geometry));
	}
} // namespace DefectStudio::Tests
