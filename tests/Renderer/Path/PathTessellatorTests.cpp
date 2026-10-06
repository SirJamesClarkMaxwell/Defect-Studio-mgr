#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include "Renderer/Path/PathTessellator.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		ScenePath MakePath(std::vector<glm::vec3> positions, std::vector<PathSegment> segments)
		{
			ScenePath path;
			for (std::size_t index = 0; index < positions.size(); ++index)
				path.nodes.push_back({PathElementId{index + 1u}, positions[index], {}});
			path.segments = std::move(segments);
			for (std::size_t index = 0; index < path.segments.size(); ++index)
				if (auto *cubic = std::get_if<CubicBezierSegmentData>(&path.segments[index].data))
				{
					// The fixture arguments remain authored world control points; store their local offsets.
					cubic->startHandle.offset -= path.nodes[index].position;
					cubic->endHandle.offset -= path.nodes[index + 1].position;
				}
			return path;
		}

		ResolvedNodes ResolveAuthored(const ScenePath &path)
		{
			ResolvedNodes resolved;
			for (const PathNode &node : path.nodes)
				resolved.positions.push_back(node.position);
			for (std::size_t index = 0; index < path.segments.size(); ++index)
			{
				const auto *cubic = std::get_if<CubicBezierSegmentData>(&path.segments[index].data);
				if (cubic == nullptr)
				{
					resolved.handlePositions.push_back(glm::vec3(0.0f));
					resolved.handlePositions.push_back(glm::vec3(0.0f));
					continue;
				}
				resolved.handlePositions.push_back(path.nodes[index].position + cubic->startHandle.offset);
				resolved.handlePositions.push_back(path.nodes[index + 1].position + cubic->endHandle.offset);
			}
			return resolved;
		}

		PathSegment Line(PathElementId id) { return {id, LineSegmentData{}}; }
		PathSegment Arc(PathElementId id, float sweep) { return {id, CircularArcSegmentData{glm::vec3(0.0f, 0.0f, 1.0f), sweep}}; }
		PathSegment Cubic(PathElementId id, glm::vec3 start, glm::vec3 end)
		{
			// Handle ids must be unique across the whole path, so derive them from the segment id -
			// two cubics sharing ids 30/31 is a DuplicateElementId path, not an S-curve.
			return {id, CubicBezierSegmentData{{PathElementId{id.value * 100u + 1u}, start, BezierHandleType::Free}, {PathElementId{id.value * 100u + 2u}, end, BezierHandleType::Free}}};
		}

		bool HasDiagnostic(const EvaluatedPath &path, PathDiagnosticCode code)
		{
			return std::any_of(path.diagnostics.begin(), path.diagnostics.end(), [code](const PathDiagnostic &diagnostic) { return diagnostic.code == code; });
		}

		double DistanceToPolyline(glm::dvec3 point, const EvaluatedPath &path)
		{
			double best = std::numeric_limits<double>::infinity();
			for (std::size_t index = 1; index < path.samples.size(); ++index)
			{
				const glm::dvec3 a = path.samples[index - 1].position;
				const glm::dvec3 delta = path.samples[index].position - a;
				const double parameter = glm::clamp(glm::dot(point - a, delta) / glm::dot(delta, delta), 0.0, 1.0);
				best = std::min(best, glm::distance(point, a + parameter * delta));
			}
			return best;
		}

		void ExpectBoundedPolylineError(const ScenePath &path, const EvaluatedPath &evaluated, double tolerance)
		{
			for (int index = 0; index <= 4096; ++index)
			{
				const double t = static_cast<double>(index) / 4096.0;
				const PathSample sample = EvaluateSegment(path, ResolveAuthored(path), 0, t).Value();
				EXPECT_LE(DistanceToPolyline(sample.position, evaluated), tolerance);
			}
		}
	}

	TEST(PathTessellatorTests, LinesEmitOnlyEndpointsWithExactLengths)
	{
		const ScenePath path = MakePath({glm::vec3(0.0f), glm::vec3(3.0f, 4.0f, 0.0f)}, {Line(PathElementId{3})});
		const EvaluatedPath evaluated = Tessellate(path, ResolveAuthored(path), {});
		ASSERT_EQ(evaluated.samples.size(), 2u);
		EXPECT_DOUBLE_EQ(evaluated.samples[0].arcLength, 0.0);
		EXPECT_DOUBLE_EQ(evaluated.samples[1].arcLength, 5.0);
		EXPECT_DOUBLE_EQ(evaluated.samples[0].normalizedT, 0.0);
		EXPECT_DOUBLE_EQ(evaluated.samples[1].normalizedT, 1.0);
		EXPECT_DOUBLE_EQ(evaluated.totalLength, 5.0);
	}

	TEST(PathTessellatorTests, FixedRibbonNormalSeedsTheProjectedFrame)
	{
		const ScenePath path = MakePath({glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f)}, {Line(PathElementId{3})});
		TessellationSettings settings;
		settings.frameSeed = {FrameSeed::Mode::FixedNormal, glm::dvec3(0.0, 1.0, 1.0)};
		const EvaluatedPath evaluated = Tessellate(path, ResolveAuthored(path), settings);
		ASSERT_EQ(evaluated.samples.size(), 2u);
		for (const EvaluatedSample &sample : evaluated.samples)
		{
			const glm::dvec3 projected = glm::normalize(glm::dvec3(0.0, 1.0, 1.0));
			EXPECT_NEAR(glm::dot(sample.tangent, sample.normal), 0.0, 1.0e-9);
			EXPECT_GT(glm::dot(sample.normal, projected), 0.99);
		}
	}

	TEST(PathTessellatorTests, TolerancesRefineCurvesAndPreserveSharedNodes)
	{
		const ScenePath path = MakePath(
			{glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(3.0f, 1.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f)},
			{Line(PathElementId{5}), Cubic(PathElementId{6}, {2.25f, 1.0f, 0.0f}, {2.75f, -1.0f, 0.0f}), Arc(PathElementId{7}, std::numbers::pi_v<float> / 2.0f)});
		const ResolvedNodes resolved = ResolveAuthored(path);
		std::size_t previous = 0;
		for (const double tolerance : {1.0e-1, 1.0e-2, 1.0e-3})
		{
			TessellationSettings settings;
			settings.worldTolerance = tolerance;
			const EvaluatedPath evaluated = Tessellate(path, resolved, settings);
			EXPECT_GE(evaluated.samples.size(), previous);
			previous = evaluated.samples.size();
			ASSERT_FALSE(evaluated.samples.empty());
			EXPECT_NEAR(evaluated.samples.back().arcLength, evaluated.totalLength, 1.0e-9);
			EXPECT_NEAR(evaluated.totalLength, CumulativeLengths(path, resolved).back(), tolerance);
			for (std::size_t index = 1; index < evaluated.samples.size(); ++index)
				EXPECT_LE(evaluated.samples[index - 1].arcLength, evaluated.samples[index].arcLength);
		}
	}

	TEST(PathTessellatorTests, BoundsCubicAndSignedArcPolylineError)
	{
		const ScenePath cubic = MakePath({glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 0.0f)}, {Cubic(PathElementId{3}, {0.0f, 3.0f, 0.0f}, {3.0f, -3.0f, 0.0f})});
		for (const double tolerance : {1.0e-1, 1.0e-2, 1.0e-3})
		{
			TessellationSettings settings;
			settings.worldTolerance = tolerance;
			ExpectBoundedPolylineError(cubic, Tessellate(cubic, ResolveAuthored(cubic), settings), tolerance);
		}
		for (const float sweep : {2.0f * std::numbers::pi_v<float> / 3.0f, -2.0f * std::numbers::pi_v<float> / 3.0f})
		{
			const ScenePath arc = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, {Arc(PathElementId{3}, sweep)});
			TessellationSettings settings;
			settings.worldTolerance = 1.0e-3;
			ExpectBoundedPolylineError(arc, Tessellate(arc, ResolveAuthored(arc), settings), settings.worldTolerance);
		}
	}

	TEST(PathTessellatorTests, ReportsInvalidSettingsAndSubdivisionLimits)
	{
		const ScenePath path = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, {Arc(PathElementId{3}, std::numbers::pi_v<float>)});
		for (const TessellationSettings invalid : {
			TessellationSettings{0.0, 12, 4096, {}}, TessellationSettings{std::numeric_limits<double>::quiet_NaN(), 12, 4096, {}},
			TessellationSettings{0.01, 0, 4096, {}}, TessellationSettings{0.01, 12, 1, {}}})
		{
			const EvaluatedPath rejected = Tessellate(path, ResolveAuthored(path), invalid);
			EXPECT_TRUE(rejected.samples.empty());
			EXPECT_TRUE(HasDiagnostic(rejected, PathDiagnosticCode::InvalidTessellationSettings));
		}

		TessellationSettings limited;
		limited.worldTolerance = 1.0e-6;
		limited.maxDepth = 1;
		const EvaluatedPath depthLimited = Tessellate(path, ResolveAuthored(path), limited);
		EXPECT_TRUE(HasDiagnostic(depthLimited, PathDiagnosticCode::TessellationDepthLimit));
		limited.maxDepth = 12;
		limited.maxSamplesPerSegment = 4;
		const EvaluatedPath sampleLimited = Tessellate(path, ResolveAuthored(path), limited);
		EXPECT_TRUE(HasDiagnostic(sampleLimited, PathDiagnosticCode::TessellationSampleLimit));
	}

	TEST(PathTessellatorTests, ProducesOrthonormalFramesWithoutFlipsOnLongCurves)
	{
		const ScenePath arc = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, {Arc(PathElementId{3}, 2.0f * std::numbers::pi_v<float> - 1.0e-3f)});
		const ScenePath sCurve = MakePath(
			{glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f)},
			{Cubic(PathElementId{4}, {0.25f, 1.0f, 0.0f}, {0.75f, -1.0f, 0.0f}), Cubic(PathElementId{5}, {1.25f, 1.0f, 0.0f}, {1.75f, -1.0f, 0.0f})});
		for (const ScenePath *path : {&arc, &sCurve})
		{
			const EvaluatedPath evaluated = Tessellate(*path, ResolveAuthored(*path), {});
			ASSERT_GT(evaluated.samples.size(), 1u);
			for (std::size_t index = 0; index < evaluated.samples.size(); ++index)
			{
				const EvaluatedSample &sample = evaluated.samples[index];
				EXPECT_NEAR(glm::length(sample.tangent), 1.0, 1.0e-9);
				EXPECT_NEAR(glm::length(sample.normal), 1.0, 1.0e-9);
				EXPECT_NEAR(glm::length(sample.binormal), 1.0, 1.0e-9);
				EXPECT_NEAR(glm::dot(sample.tangent, sample.normal), 0.0, 1.0e-9);
				EXPECT_NEAR(glm::distance(glm::cross(sample.tangent, sample.normal), sample.binormal), 0.0, 1.0e-9);
				if (index > 0)
					EXPECT_GT(glm::dot(evaluated.samples[index - 1].normal, sample.normal), 0.0);
			}
		}
	}

	TEST(PathTessellatorTests, RejectsDegeneratePathsWithoutNonFiniteSamples)
	{
		const ScenePath zeroLine = MakePath({glm::vec3(0.0f), glm::vec3(0.0f)}, {Line(PathElementId{3})});
		const EvaluatedPath evaluated = Tessellate(zeroLine, ResolveAuthored(zeroLine), {});
		EXPECT_TRUE(evaluated.samples.empty());
		EXPECT_TRUE(HasDiagnostic(evaluated, PathDiagnosticCode::ZeroChord));
		const ScenePath invalidArc = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, {Arc(PathElementId{3}, 0.0f)});
		const EvaluatedPath rejected = Tessellate(invalidArc, ResolveAuthored(invalidArc), {});
		EXPECT_TRUE(rejected.samples.empty());
		EXPECT_TRUE(HasDiagnostic(rejected, PathDiagnosticCode::ArcNonFiniteDerived));
	}
} // namespace DefectStudio::Tests
