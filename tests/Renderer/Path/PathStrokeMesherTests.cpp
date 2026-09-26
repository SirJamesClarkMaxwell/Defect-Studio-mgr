#include "Core/dspch.hpp"

#include <algorithm>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

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
			PathDecorationKind::Stealth, PathDecorationKind::Latex, PathDecorationKind::Bar,
			PathDecorationKind::Circle, PathDecorationKind::Square, PathDecorationKind::Diamond,
			PathDecorationKind::Kite};

		ScenePath CurvedArcPath()
		{
			ScenePath path;
			path.nodes = {{PathElementId{1u}, glm::vec3(0.0f), {}}, {PathElementId{2u}, glm::vec3(2.0f, 0.0f, 0.0f), {}}};
			path.segments = {{PathElementId{3u}, CircularArcSegmentData{glm::vec3(0.0f, 0.0f, 1.0f), std::numbers::pi_v<float> / 2.0f}}};
			return path;
		}

		ResolvedNodes ResolveNodePositions(const ScenePath &path)
		{
			ResolvedNodes resolved;
			for (const PathNode &node : path.nodes)
				resolved.positions.push_back(node.position);
			return resolved;
		}

		EvaluatedPath TessellatedCurvedArc()
		{
			const ScenePath path = CurvedArcPath();
			TessellationSettings settings;
			settings.worldTolerance = 1.0e-4;
			return Tessellate(path, ResolveNodePositions(path), settings);
		}

		bool RangesOverlap(const StrokeMeshRange &a, const StrokeMeshRange &b)
		{
			if (a.IsEmpty() || b.IsEmpty())
				return false;
			return a.firstIndex < b.firstIndex + b.indexCount && b.firstIndex < a.firstIndex + a.indexCount;
		}

		bool AllVertexFieldsFinite(const StrokeGeometry &geometry)
		{
			for (const StrokeTubeVertex &vertex : geometry.tubeVertices)
				if (!Finite(vertex.position) || !Finite(vertex.normal) || !Finite(vertex.color) || !std::isfinite(vertex.arcT) || !std::isfinite(vertex.dashCoord))
					return false;
			for (const StrokeRibbonVertex &vertex : geometry.ribbonVertices)
				if (!Finite(vertex.position) || !Finite(vertex.tangent) || !Finite(vertex.normal) || !Finite(vertex.color) ||
					!std::isfinite(vertex.side) || !std::isfinite(vertex.arcT) || !std::isfinite(vertex.dashCoord) || !std::isfinite(vertex.halfWidth))
					return false;
			return true;
		}

		void AssertAllIndicesInBounds(const StrokeGeometry &geometry, const std::size_t vertexCount)
		{
			ASSERT_EQ(geometry.indices.size() % 3u, 0u);
			for (std::size_t index = 0; index < geometry.indices.size(); ++index)
			{
				ASSERT_LT(index, geometry.indices.size());
				const std::uint32_t vertexIndex = geometry.indices[index];
				ASSERT_LT(vertexIndex, vertexCount);
			}
		}

		void AssertRangeReferencesInBounds(const StrokeGeometry &geometry, const StrokeMeshRange &range, const std::size_t vertexCount)
		{
			ASSERT_LE(range.firstIndex, geometry.indices.size());
			ASSERT_LE(range.indexCount, geometry.indices.size() - range.firstIndex);
			for (std::uint32_t offset = 0; offset < range.indexCount; ++offset)
			{
				const std::size_t indexPosition = static_cast<std::size_t>(range.firstIndex) + offset;
				ASSERT_LT(indexPosition, geometry.indices.size());
				const std::uint32_t vertexIndex = geometry.indices[indexPosition];
				ASSERT_LT(vertexIndex, vertexCount);
			}
		}

		void CollectReferencedVertices(const StrokeGeometry &geometry, const StrokeMeshRange &range, const std::size_t vertexCount,
			std::vector<std::uint32_t> &vertices)
		{
			vertices.clear();
			AssertRangeReferencesInBounds(geometry, range, vertexCount);
			if (range.firstIndex > geometry.indices.size() || range.indexCount > geometry.indices.size() - range.firstIndex)
				return;
			for (std::uint32_t offset = 0; offset < range.indexCount; ++offset)
			{
				const std::size_t indexPosition = static_cast<std::size_t>(range.firstIndex) + offset;
				ASSERT_LT(indexPosition, geometry.indices.size());
				const std::uint32_t vertexIndex = geometry.indices[indexPosition];
				ASSERT_LT(vertexIndex, vertexCount);
				if (std::find(vertices.begin(), vertices.end(), vertexIndex) == vertices.end())
					vertices.push_back(vertexIndex);
			}
		}

		void AssertDecoratedEndHandoffInvariant(const StrokeGeometry &geometry, const PathStrokeStyle &style,
			const EvaluatedSample &endpoint, const bool start)
		{
			const bool usesTube = !geometry.tubeVertices.empty();
			const std::size_t vertexCount = usesTube ? geometry.tubeVertices.size() : geometry.ribbonVertices.size();
			ASSERT_GT(vertexCount, 0u);
			ASSERT_FALSE(geometry.shaft.IsEmpty());
			std::vector<std::uint32_t> shaftVertices;
			CollectReferencedVertices(geometry, geometry.shaft, vertexCount, shaftVertices);
			ASSERT_FALSE(shaftVertices.empty());

			const PathEndpointDecoration &decoration = start ? style.startDecoration : style.endDecoration;
			const DecorationContour contour = BuildDecorationContour(decoration, style.width);
			ASSERT_FALSE(contour.points.empty());
			const double backS = contour.points.back().s;
			const glm::dvec3 inward = start ? endpoint.tangent : -endpoint.tangent;
			const glm::vec3 expectedPosition(endpoint.position + inward * backS);
			const glm::vec3 expectedTangent(endpoint.tangent);
			const glm::vec3 expectedNormal(endpoint.normal);
			const float boundaryArc = static_cast<float>(start ? geometry.shaftRange.start : geometry.shaftRange.end);
			std::vector<std::uint32_t> boundaryVertices;
			for (const std::uint32_t vertexIndex : shaftVertices)
			{
				ASSERT_LT(vertexIndex, vertexCount);
				const float dashCoord = usesTube ? geometry.tubeVertices[vertexIndex].dashCoord : geometry.ribbonVertices[vertexIndex].dashCoord;
				if (std::abs(dashCoord - boundaryArc) <= 1.0e-5f)
					boundaryVertices.push_back(vertexIndex);
			}
			ASSERT_FALSE(boundaryVertices.empty());

			if (usesTube && style.profile == StrokeProfile::Round)
			{
				const float radius = style.width * 0.5f;
				for (const std::uint32_t vertexIndex : boundaryVertices)
				{
					ASSERT_LT(vertexIndex, geometry.tubeVertices.size());
					const StrokeTubeVertex &vertex = geometry.tubeVertices[vertexIndex];
					const glm::vec3 offset = vertex.position - expectedPosition;
					const float offsetLength = glm::length(offset);
					EXPECT_NEAR(offsetLength, radius, 1.0e-5f);
					ASSERT_GT(offsetLength, 0.0f);
					EXPECT_NEAR(glm::dot(offset, expectedTangent), 0.0f, 1.0e-5f);
					EXPECT_NEAR(glm::distance(glm::normalize(offset), vertex.normal), 0.0f, 1.0e-5f);
				}

				for (std::uint32_t radial = 0; radial < style.radialSegments; ++radial)
				{
					const double angle = 2.0 * std::numbers::pi * static_cast<double>(radial) / static_cast<double>(style.radialSegments);
					const glm::vec3 expectedNormalAtRadial = glm::vec3(std::cos(angle) * endpoint.normal + std::sin(angle) * endpoint.binormal);
					bool matched = false;
					for (const std::uint32_t vertexIndex : boundaryVertices)
					{
						ASSERT_LT(vertexIndex, geometry.tubeVertices.size());
						const StrokeTubeVertex &vertex = geometry.tubeVertices[vertexIndex];
						matched = glm::distance(vertex.normal, expectedNormalAtRadial) <= 1.0e-5f &&
							glm::distance(vertex.position, expectedPosition + radius * expectedNormalAtRadial) <= 1.0e-5f;
						if (matched)
							break;
					}
					EXPECT_TRUE(matched) << "shaft boundary does not carry the endpoint frame";
				}
			}
			else if (!usesTube)
			{
				for (const std::uint32_t vertexIndex : boundaryVertices)
				{
					ASSERT_LT(vertexIndex, geometry.ribbonVertices.size());
					const StrokeRibbonVertex &vertex = geometry.ribbonVertices[vertexIndex];
					EXPECT_NEAR(glm::distance(vertex.position, expectedPosition), 0.0f, 1.0e-5f);
					EXPECT_NEAR(glm::distance(vertex.tangent, expectedTangent), 0.0f, 1.0e-5f);
					EXPECT_NEAR(glm::distance(vertex.normal, expectedNormal), 0.0f, 1.0e-5f);
					EXPECT_NEAR(vertex.halfWidth, style.width * 0.5f, 1.0e-5f);
				}
			}
			else
			{
				ASSERT_EQ(style.profile, StrokeProfile::Flat);
				ASSERT_GT(style.ribbonThickness, 0.0f);
				const glm::vec3 tangent(endpoint.tangent);
				const glm::vec3 normal(endpoint.normal);
				const glm::vec3 binormal(endpoint.binormal);
				float minimumNormal = std::numeric_limits<float>::infinity();
				float maximumNormal = -std::numeric_limits<float>::infinity();
				float minimumBinormal = std::numeric_limits<float>::infinity();
				float maximumBinormal = -std::numeric_limits<float>::infinity();
				for (const std::uint32_t vertexIndex : boundaryVertices)
				{
					ASSERT_LT(vertexIndex, geometry.tubeVertices.size());
					const glm::vec3 offset = geometry.tubeVertices[vertexIndex].position - expectedPosition;
					EXPECT_NEAR(glm::dot(offset, tangent), 0.0f, 1.0e-5f);
					const float normalProjection = glm::dot(offset, normal);
					const float binormalProjection = glm::dot(offset, binormal);
					minimumNormal = std::min(minimumNormal, normalProjection);
					maximumNormal = std::max(maximumNormal, normalProjection);
					minimumBinormal = std::min(minimumBinormal, binormalProjection);
					maximumBinormal = std::max(maximumBinormal, binormalProjection);
				}
				EXPECT_NEAR(maximumNormal - minimumNormal, style.width, 1.0e-5f);
				EXPECT_NEAR(maximumBinormal - minimumBinormal, style.ribbonThickness, 1.0e-5f);
			}
		}

		void AssertTubeGeometryMatches(const StrokeGeometry &expected, const StrokeGeometry &actual)
		{
			ASSERT_EQ(expected.indices, actual.indices);
			ASSERT_EQ(expected.tubeVertices.size(), actual.tubeVertices.size());
			ASSERT_EQ(expected.ribbonVertices.size(), actual.ribbonVertices.size());
			for (std::size_t index = 0; index < expected.tubeVertices.size(); ++index)
			{
				ASSERT_LT(index, expected.tubeVertices.size());
				ASSERT_LT(index, actual.tubeVertices.size());
				EXPECT_EQ(expected.tubeVertices[index].position, actual.tubeVertices[index].position);
			}
			for (std::size_t index = 0; index < expected.ribbonVertices.size(); ++index)
			{
				ASSERT_LT(index, expected.ribbonVertices.size());
				ASSERT_LT(index, actual.ribbonVertices.size());
				EXPECT_EQ(expected.ribbonVertices[index].position, actual.ribbonVertices[index].position);
			}
		}

		void AssertReferencedTubeSpan(const StrokeGeometry &geometry, const StrokeMeshRange &range,
			const glm::vec3 &axis, const float expectedSpan)
		{
			std::vector<std::uint32_t> vertices;
			CollectReferencedVertices(geometry, range, geometry.tubeVertices.size(), vertices);
			ASSERT_FALSE(vertices.empty());
			float minimum = std::numeric_limits<float>::infinity();
			float maximum = -std::numeric_limits<float>::infinity();
			for (const std::uint32_t vertexIndex : vertices)
			{
				ASSERT_LT(vertexIndex, geometry.tubeVertices.size());
				const float projection = glm::dot(geometry.tubeVertices[vertexIndex].position, axis);
				minimum = std::min(minimum, projection);
				maximum = std::max(maximum, projection);
			}
			EXPECT_NEAR(maximum - minimum, expectedSpan, 1.0e-5f);
		}
	} // namespace

	TEST(PathStrokeMesherTests, FlatZeroThicknessKeepsTheSheetVertexArray)
	{
		const EvaluatedPath path = StraightPath();
		PathStrokeStyle defaultStyle;
		defaultStyle.profile = StrokeProfile::Flat;
		defaultStyle.width = 0.2f;
		defaultStyle.radialSegments = 7;
		const StrokeGeometry defaultGeometry = BuildStroke(path, defaultStyle);

		PathStrokeStyle explicitZero = defaultStyle;
		explicitZero.ribbonThickness = 0.0f;
		const StrokeGeometry zeroGeometry = BuildStroke(path, explicitZero);

		ASSERT_FALSE(defaultGeometry.ribbonVertices.empty());
		ASSERT_TRUE(defaultGeometry.tubeVertices.empty());
		ASSERT_FALSE(zeroGeometry.ribbonVertices.empty());
		ASSERT_TRUE(zeroGeometry.tubeVertices.empty());
		EXPECT_EQ(zeroGeometry.ribbonVertices.size(), defaultGeometry.ribbonVertices.size());
		EXPECT_EQ(zeroGeometry.indices.size(), defaultGeometry.indices.size());
	}

	TEST(PathStrokeMesherTests, PositiveFlatRibbonThicknessUsesTubeVerticesOnly)
	{
		PathStrokeStyle style;
		style.profile = StrokeProfile::Flat;
		style.width = 0.2f;
		style.ribbonThickness = 0.3f;
		const StrokeGeometry geometry = BuildStroke(StraightPath(), style);

		ASSERT_FALSE(geometry.tubeVertices.empty());
		EXPECT_TRUE(geometry.ribbonVertices.empty());
	}

	TEST(PathStrokeMesherTests, ThickFlatRingsHaveFourCornersRegardlessOfRadialSegments)
	{
		const EvaluatedPath path = StraightPath();
		PathStrokeStyle style;
		style.profile = StrokeProfile::Flat;
		style.width = 0.2f;
		style.ribbonThickness = 0.3f;
		style.radialSegments = 11;
		const StrokeGeometry geometry = BuildStroke(path, style);

		ASSERT_EQ(geometry.tubeVertices.size(), path.samples.size() * 4u);
		EXPECT_TRUE(geometry.ribbonVertices.empty());
	}

	TEST(PathStrokeMesherTests, ThickFlatRingSpansAreTheInputWidthAndThickness)
	{
		const EvaluatedPath path = StraightPath();
		PathStrokeStyle style;
		style.profile = StrokeProfile::Flat;
		style.width = 0.6f;
		style.ribbonThickness = 0.4f;
		style.radialSegments = 3;
		const StrokeGeometry geometry = BuildStroke(path, style);

		ASSERT_EQ(geometry.tubeVertices.size(), path.samples.size() * 4u);
		for (std::size_t sampleIndex = 0; sampleIndex < path.samples.size(); ++sampleIndex)
		{
			const std::size_t firstVertex = sampleIndex * 4u;
			ASSERT_LE(firstVertex + 4u, geometry.tubeVertices.size());
			const glm::vec3 normal(path.samples[sampleIndex].normal);
			const glm::vec3 binormal(path.samples[sampleIndex].binormal);
			float minimumNormal = std::numeric_limits<float>::infinity();
			float maximumNormal = -std::numeric_limits<float>::infinity();
			float minimumBinormal = std::numeric_limits<float>::infinity();
			float maximumBinormal = -std::numeric_limits<float>::infinity();
			for (std::size_t corner = 0; corner < 4u; ++corner)
			{
				ASSERT_LT(firstVertex + corner, geometry.tubeVertices.size());
				const glm::vec3 offset = geometry.tubeVertices[firstVertex + corner].position - glm::vec3(path.samples[sampleIndex].position);
				const float normalProjection = glm::dot(offset, normal);
				const float binormalProjection = glm::dot(offset, binormal);
				minimumNormal = std::min(minimumNormal, normalProjection);
				maximumNormal = std::max(maximumNormal, normalProjection);
				minimumBinormal = std::min(minimumBinormal, binormalProjection);
				maximumBinormal = std::max(maximumBinormal, binormalProjection);
			}
			EXPECT_NEAR(maximumNormal - minimumNormal, style.width, 1.0e-5f);
			EXPECT_NEAR(maximumBinormal - minimumBinormal, style.ribbonThickness, 1.0e-5f);
		}
	}

	TEST(PathStrokeMesherTests, ThickFlatEndDecorationSpansTheRibbonThickness)
	{
		const EvaluatedPath path = StraightPath();
		PathStrokeStyle style;
		style.profile = StrokeProfile::Flat;
		style.width = 0.2f;
		style.ribbonThickness = 0.35f;
		style.endDecoration.kind = PathDecorationKind::Arrow;
		const StrokeGeometry geometry = BuildStroke(path, style);

		ASSERT_FALSE(geometry.tubeVertices.empty());
		ASSERT_TRUE(geometry.ribbonVertices.empty());
		ASSERT_FALSE(geometry.endDecoration.IsEmpty());
		const EvaluatedSample &endpoint = path.samples.back();
		const float decorationFullWidth = 2.0f * style.endDecoration.widthScale * style.width;
		AssertReferencedTubeSpan(geometry, geometry.endDecoration, glm::vec3(endpoint.normal), decorationFullWidth);
		AssertReferencedTubeSpan(geometry, geometry.endDecoration, glm::vec3(endpoint.binormal), style.ribbonThickness);
	}

	TEST(PathStrokeMesherTests, ThickFlatCurvedDecoratedEndUsesTheExistingEndpointHandoff)
	{
		const EvaluatedPath path = TessellatedCurvedArc();
		ASSERT_GT(path.samples.size(), 2u);
		PathStrokeStyle style;
		style.profile = StrokeProfile::Flat;
		style.width = 0.2f;
		style.ribbonThickness = 0.3f;
		style.radialSegments = 9;
		style.endDecoration.kind = PathDecorationKind::Arrow;
		const StrokeGeometry geometry = BuildStroke(path, style);

		ASSERT_FALSE(geometry.tubeVertices.empty());
		ASSERT_TRUE(geometry.ribbonVertices.empty());
		AssertDecoratedEndHandoffInvariant(geometry, style, path.samples.back(), false);
	}

	TEST(PathStrokeMesherTests, RibbonThicknessIsIgnoredByRoundAndCameraFacingProfiles)
	{
		for (const StrokeProfile profile : {StrokeProfile::Round, StrokeProfile::CameraFacing})
		{
			PathStrokeStyle style;
			style.profile = profile;
			style.width = 0.2f;
			style.radialSegments = 7;
			style.endDecoration.kind = PathDecorationKind::Arrow;
			const StrokeGeometry baseline = BuildStroke(StraightPath(), style);
			style.ribbonThickness = 0.6f;
			const StrokeGeometry withThickness = BuildStroke(StraightPath(), style);
			SCOPED_TRACE(static_cast<int>(profile));
			AssertTubeGeometryMatches(baseline, withThickness);
		}
	}

	TEST(PathStrokeMesherTests, NegativeAndNonFiniteRibbonThicknessUsesZeroThicknessGeometry)
	{
		PathStrokeStyle zeroStyle;
		zeroStyle.profile = StrokeProfile::Flat;
		zeroStyle.width = 0.2f;
		const StrokeGeometry zero = BuildStroke(StraightPath(), zeroStyle);
		ASSERT_FALSE(zero.ribbonVertices.empty());
		ASSERT_TRUE(zero.tubeVertices.empty());

		for (const float thickness : {-0.1f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
		{
			PathStrokeStyle invalid = zeroStyle;
			invalid.ribbonThickness = thickness;
			const StrokeGeometry geometry = BuildStroke(StraightPath(), invalid);
			SCOPED_TRACE(thickness);
			EXPECT_TRUE(geometry.tubeVertices.empty());
			EXPECT_FALSE(geometry.ribbonVertices.empty());
			EXPECT_EQ(geometry.ribbonVertices.size(), zero.ribbonVertices.size());
			EXPECT_EQ(geometry.indices.size(), zero.indices.size());
		}
	}
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

	TEST(PathStrokeMesherTests, CurvedDecoratedEndHandoffMatchesInEveryProfile)
	{
		const EvaluatedPath path = TessellatedCurvedArc();
		ASSERT_GT(path.samples.size(), 2u);
		for (const StrokeProfile profile : {StrokeProfile::Round, StrokeProfile::Flat, StrokeProfile::CameraFacing})
		{
			PathStrokeStyle style;
			style.profile = profile;
			style.width = 0.2f;
			style.radialSegments = 8;
			style.endDecoration.kind = PathDecorationKind::Arrow;
			SCOPED_TRACE(static_cast<int>(profile));
			const StrokeGeometry geometry = BuildStroke(path, style);
			AssertDecoratedEndHandoffInvariant(geometry, style, path.samples.back(), false);
		}
	}

	TEST(PathStrokeMesherTests, CurvedDecoratedStartUsesTheSameHandoffInvariant)
	{
		const EvaluatedPath path = TessellatedCurvedArc();
		ASSERT_GT(path.samples.size(), 2u);
		for (const StrokeProfile profile : {StrokeProfile::Round, StrokeProfile::Flat, StrokeProfile::CameraFacing})
		{
			PathStrokeStyle style;
			style.profile = profile;
			style.width = 0.2f;
			style.radialSegments = 8;
			style.startDecoration.kind = PathDecorationKind::Arrow;
			SCOPED_TRACE(static_cast<int>(profile));
			const StrokeGeometry geometry = BuildStroke(path, style);
			AssertDecoratedEndHandoffInvariant(geometry, style, path.samples.front(), true);
		}
	}

	TEST(PathStrokeMesherTests, CurvedPathWithDecorationsAtBothEndsKeepsDisjointFiniteShaftGeometry)
	{
		const EvaluatedPath path = TessellatedCurvedArc();
		ASSERT_GT(path.samples.size(), 2u);
		for (const StrokeProfile profile : {StrokeProfile::Round, StrokeProfile::Flat, StrokeProfile::CameraFacing})
		{
			PathStrokeStyle style;
			style.profile = profile;
			style.width = 0.2f;
			style.radialSegments = 8;
			style.startDecoration.kind = PathDecorationKind::Arrow;
			style.endDecoration.kind = PathDecorationKind::Arrow;
			const StrokeGeometry geometry = BuildStroke(path, style);
			SCOPED_TRACE(static_cast<int>(profile));
			ASSERT_FALSE(geometry.shaft.IsEmpty());
			EXPECT_GT(geometry.shaftRange.end, geometry.shaftRange.start);
			EXPECT_FALSE(RangesOverlap(geometry.startDecoration, geometry.endDecoration));
			EXPECT_FALSE(RangesOverlap(geometry.startDecoration, geometry.shaft));
			EXPECT_FALSE(RangesOverlap(geometry.endDecoration, geometry.shaft));

			const std::size_t vertexCount = profile == StrokeProfile::Round ? geometry.tubeVertices.size() : geometry.ribbonVertices.size();
			AssertRangeReferencesInBounds(geometry, geometry.startDecoration, vertexCount);
			AssertRangeReferencesInBounds(geometry, geometry.endDecoration, vertexCount);
			AssertRangeReferencesInBounds(geometry, geometry.shaft, vertexCount);
			AssertAllIndicesInBounds(geometry, vertexCount);
			EXPECT_TRUE(AllVertexFieldsFinite(geometry));
		}
	}

	TEST(PathStrokeMesherTests, FilledEndDecorationSuppressesRoundCapAtDecoratedEnd)
	{
		const EvaluatedPath path = TessellatedCurvedArc();
		ASSERT_GT(path.samples.size(), 2u);
		PathStrokeStyle roundStyle;
		roundStyle.width = 0.2f;
		roundStyle.radialSegments = 8;
		roundStyle.cap = PathLineCap::Round;
		roundStyle.startDecoration.kind = PathDecorationKind::Arrow;
		roundStyle.endDecoration.kind = PathDecorationKind::Arrow;
		const StrokeGeometry round = BuildStroke(path, roundStyle);

		PathStrokeStyle buttStyle = roundStyle;
		buttStyle.cap = PathLineCap::Butt;
		const StrokeGeometry butt = BuildStroke(path, buttStyle);

		EXPECT_EQ(round.tubeVertices.size(), butt.tubeVertices.size());
		EXPECT_EQ(round.indices.size(), butt.indices.size());
	}

	TEST(PathStrokeMesherTests, RoundCapRemainsAtAnUndecoratedEnd)
	{
		const EvaluatedPath path = TessellatedCurvedArc();
		ASSERT_GT(path.samples.size(), 2u);
		PathStrokeStyle roundStyle;
		roundStyle.width = 0.2f;
		roundStyle.radialSegments = 8;
		roundStyle.cap = PathLineCap::Round;
		roundStyle.startDecoration.kind = PathDecorationKind::Arrow;
		const StrokeGeometry round = BuildStroke(path, roundStyle);

		PathStrokeStyle buttStyle = roundStyle;
		buttStyle.cap = PathLineCap::Butt;
		const StrokeGeometry butt = BuildStroke(path, buttStyle);

		EXPECT_GT(round.tubeVertices.size(), butt.tubeVertices.size());
		EXPECT_GT(round.indices.size(), butt.indices.size());
	}

	TEST(PathStrokeMesherTests, RoundCapRemainsAtAnUnfilledDecoratedEnd)
	{
		const EvaluatedPath path = TessellatedCurvedArc();
		ASSERT_GT(path.samples.size(), 2u);
		PathStrokeStyle roundStyle;
		roundStyle.width = 0.2f;
		roundStyle.radialSegments = 8;
		roundStyle.cap = PathLineCap::Round;
		roundStyle.startDecoration.kind = PathDecorationKind::Arrow;
		roundStyle.endDecoration.kind = PathDecorationKind::Arrow;
		roundStyle.endDecoration.filled = false;
		const StrokeGeometry round = BuildStroke(path, roundStyle);

		PathStrokeStyle buttStyle = roundStyle;
		buttStyle.cap = PathLineCap::Butt;
		const StrokeGeometry butt = BuildStroke(path, buttStyle);

		EXPECT_GT(round.tubeVertices.size(), butt.tubeVertices.size());
		EXPECT_GT(round.indices.size(), butt.indices.size());
	}

	// ribbonNormal seeds the FRAME, so it acts at tessellation time. BuildStroke is handed an
	// already-tessellated path and cannot see it at all - testing it here was testing the wrong
	// layer, and passing two identical EvaluatedPaths made the assertion unfalsifiable as well.
	TEST(PathStrokeMesherTests, RibbonNormalOnlyChangesGeometryThroughTheFrameSeed)
	{
		const EvaluatedPath path = StraightPath();
		PathStrokeStyle flat;
		flat.profile = StrokeProfile::Flat;
		flat.width = 0.2f;
		PathStrokeStyle rotated = flat;
		rotated.ribbonNormal = glm::vec3(0.0f, 0.0f, 1.0f);

		// Same EvaluatedPath, different ribbonNormal: the mesher must not react, because the field
		// is not its input. The renderer applies it by seeding the tessellation instead - see
		// OpenGlPathRenderer, and FixedRibbonNormalSeedsTheProjectedFrame in PathTessellatorTests for the
		// half that does react.
		const StrokeGeometry a = BuildStroke(path, flat);
		const StrokeGeometry b = BuildStroke(path, rotated);
		ASSERT_FALSE(a.ribbonVertices.empty());
		ASSERT_EQ(a.ribbonVertices.size(), b.ribbonVertices.size());
		for (std::size_t index = 0; index < a.ribbonVertices.size(); ++index)
			EXPECT_EQ(a.ribbonVertices[index].position, b.ribbonVertices[index].position);
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
		// Recorded from a run, not reasoned out: the decoration's back moved from s == 0.2 to s == 0.6
		// and its half-width from 0.1 to 0.25, so the arrowhead is now two and a half times the tube's
		// radius instead of exactly equal to it. The offset axis is the binormal - Sample() seeds normal
		// (0,0,1) and binormal cross(tangent, normal) = (0,-1,0) - which is the detail a hand-written
		// expectation gets wrong.
		expectNear(flat.ribbonVertices[0].position, glm::vec3(2.0f, 0.0f, 0.0f));
		expectNear(flat.ribbonVertices[2].position, glm::vec3(1.4f, -0.25f, 0.0f));
		expectNear(flat.ribbonVertices[3].position, glm::vec3(1.4f, 0.25f, 0.0f));
		expectNear(flat.ribbonVertices[4].position, glm::vec3(1.4f, 0.0f, 0.0f));

		style.profile = StrokeProfile::Round;
		style.radialSegments = 4;
		const StrokeGeometry round = BuildStroke(StraightPath(), style);
		ASSERT_GE(round.tubeVertices.size(), 8u);
		// The ring's off-axis components land at ~1e-17 rather than exactly zero, so this compares
		// near rather than equal - EXPECT_EQ on a float that went through a trig function is a test
		// that fails on a compiler flag change, not on a regression.
		expectNear(round.tubeVertices[0].position, glm::vec3(2.0f, 0.0f, 0.0f));
		expectNear(round.tubeVertices[4].position, glm::vec3(1.4f, 0.25f, 0.0f));
		expectNear(round.tubeVertices[5].position, glm::vec3(1.4f, 0.0f, 0.25f));
		expectNear(round.tubeVertices[6].position, glm::vec3(1.4f, -0.25f, 0.0f));
	}

	TEST(PathStrokeMesherTests, HollowDecorationHasAnInnerWallRatherThanAClosedSolidFan)
	{
		PathStrokeStyle filledStyle;
		filledStyle.width = 0.2f;
		filledStyle.radialSegments = 8;
		filledStyle.endDecoration = {PathDecorationKind::Arrow, 2.0f, 2.0f, true};
		PathStrokeStyle hollowStyle = filledStyle;
		hollowStyle.endDecoration.filled = false;

		const StrokeGeometry filled = BuildStroke(StraightPath(), filledStyle);
		const StrokeGeometry hollow = BuildStroke(StraightPath(), hollowStyle);
		ASSERT_FALSE(filled.tubeVertices.empty());
		ASSERT_FALSE(hollow.tubeVertices.empty());
		ASSERT_FALSE(filled.endDecoration.IsEmpty());
		ASSERT_FALSE(hollow.endDecoration.IsEmpty());
		EXPECT_NE(filled.tubeVertices.size(), hollow.tubeVertices.size());
		EXPECT_NE(filled.endDecoration.indexCount, hollow.endDecoration.indexCount);

		// A hollow body must add an inner wall: at least one indexed hollow vertex is not present in
		// the filled body's outer contour/fan. This is a geometric hole check, not a count-only check.
		bool hasInnerVertex = false;
		for (std::uint32_t hollowIndex = hollow.endDecoration.firstIndex;
			hollowIndex < hollow.endDecoration.firstIndex + hollow.endDecoration.indexCount; ++hollowIndex)
		{
			ASSERT_LT(hollowIndex, hollow.indices.size());
			const std::uint32_t hollowVertexIndex = hollow.indices[hollowIndex];
			ASSERT_LT(hollowVertexIndex, hollow.tubeVertices.size());
			const glm::vec3 &hollowPosition = hollow.tubeVertices[hollowVertexIndex].position;
			bool matchesFilled = false;
			for (std::uint32_t filledIndex = filled.endDecoration.firstIndex;
				filledIndex < filled.endDecoration.firstIndex + filled.endDecoration.indexCount; ++filledIndex)
			{
				ASSERT_LT(filledIndex, filled.indices.size());
				const std::uint32_t filledVertexIndex = filled.indices[filledIndex];
				ASSERT_LT(filledVertexIndex, filled.tubeVertices.size());
				if (glm::distance(hollowPosition, filled.tubeVertices[filledVertexIndex].position) <= 1e-6f)
				{
					matchesFilled = true;
					break;
				}
			}
			if (!matchesFilled)
			{
				hasInnerVertex = true;
				break;
			}
		}
		EXPECT_TRUE(hasInnerVertex);
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
