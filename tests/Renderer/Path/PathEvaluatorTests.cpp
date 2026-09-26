#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include "Renderer/Path/PathEvaluator.hpp"

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
			path.nextElementId = 20;
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
		PathSegment Arc(PathElementId id, float sweep) { return {id, CircularArcSegmentData{{0.0f, 0.0f, 1.0f}, sweep}}; }
		PathSegment Cubic(PathElementId id, glm::vec3 start, glm::vec3 end)
		{
			return {id, CubicBezierSegmentData{{PathElementId{30}, start, {}}, {PathElementId{31}, end, {}}}};
		}
	} // namespace

	TEST(PathEvaluatorTests, LineEvaluatesPositionUnitTangentAndChordLength)
	{
		const ScenePath path = MakePath({{0.0f, 0.0f, 0.0f}, {3.0f, 4.0f, 0.0f}}, {Line(PathElementId{3})});
		const ResolvedNodes resolved = ResolveAuthored(path);
		const Result<PathSample> sample = EvaluateSegment(path, resolved, 0, 0.25);
		ASSERT_TRUE(sample);
		EXPECT_NEAR(sample->position.x, 0.75, 1e-12);
		EXPECT_NEAR(sample->position.y, 1.0, 1e-12);
		EXPECT_NEAR(glm::length(sample->tangent), 1.0, 1e-12);
		EXPECT_NEAR(SegmentLength(path, resolved, 0).Value(), 5.0, 1e-12);
	}

	TEST(PathEvaluatorTests, CubicHasExactEndpointsHandleTangentsAndBoundedErrorLength)
	{
		const ScenePath path = MakePath(
			{{0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}},
			{Cubic(PathElementId{3}, {1.0f, 1.0f, 0.0f}, {2.0f, -1.0f, 0.0f})});
		const ResolvedNodes resolved = ResolveAuthored(path);
		const Result<PathSample> start = EvaluateSegment(path, resolved, 0, 0.0);
		const Result<PathSample> end = EvaluateSegment(path, resolved, 0, 1.0);
		ASSERT_TRUE(start);
		ASSERT_TRUE(end);
		EXPECT_NEAR(glm::distance(start->position, glm::dvec3(0.0)), 0.0, 1e-12);
		EXPECT_NEAR(glm::distance(end->position, glm::dvec3(3.0, 0.0, 0.0)), 0.0, 1e-12);
		EXPECT_NEAR(glm::dot(start->tangent, glm::normalize(glm::dvec3(1.0, 1.0, 0.0))), 1.0, 1e-12);
		EXPECT_NEAR(glm::dot(end->tangent, glm::normalize(glm::dvec3(1.0, 1.0, 0.0))), 1.0, 1e-12);
		EXPECT_NEAR(SegmentLength(path, resolved, 0).Value(), 3.27480395943194, 1e-9);

		const ScenePath longer = MakePath(
			{{0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}},
			{Cubic(PathElementId{3}, {1.0f, 2.0f, 0.0f}, {2.0f, -2.0f, 0.0f})});
		EXPECT_GT(SegmentLength(longer, ResolveAuthored(longer), 0).Value(), SegmentLength(path, resolved, 0).Value());
	}

	TEST(PathEvaluatorTests, ArcDerivationEvaluationAndLengthSupportPositiveAndNegativeSweeps)
	{
		const float sweep = 2.0f * std::numbers::pi_v<float> / 3.0f;
		const Result<ArcGeometry> arc = DeriveArc({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0f, 0.0f, 1.0f}, sweep);
		ASSERT_TRUE(arc);
		// Tolerance is float-sourced, not evaluator-sourced: signedSweepRadians is stored as float
		// (plan v2 C9), so a 120 degree sweep carries ~3e-8 relative error into r = |AB| / (2 sin(t/2)).
		EXPECT_NEAR(arc->radius, 1.0 / std::sqrt(3.0), 1e-7);

		for (const float testedSweep : {std::numbers::pi_v<float> / 2.0f, std::numbers::pi_v<float>, -sweep})
		{
			const ScenePath path = MakePath({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, {Arc(PathElementId{3}, testedSweep)});
			const ResolvedNodes resolved = ResolveAuthored(path);
			const Result<PathSample> endpoint = EvaluateSegment(path, resolved, 0, 1.0);
			ASSERT_TRUE(endpoint);
			EXPECT_NEAR(glm::distance(endpoint->position, glm::dvec3(1.0, 0.0, 0.0)), 0.0, 1e-12);
			const Result<double> length = SegmentLength(path, resolved, 0);
			ASSERT_TRUE(length);
			EXPECT_NEAR(length.Value(), DeriveArc({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0f, 0.0f, 1.0f}, testedSweep).Value().radius * std::abs(testedSweep), 1e-12);
		}
		const ScenePath positive = MakePath({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, {Arc(PathElementId{3}, sweep)});
		const ScenePath negative = MakePath({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, {Arc(PathElementId{3}, -sweep)});
		EXPECT_LT(EvaluateSegment(positive, ResolveAuthored(positive), 0, 0.0)->tangent.y, 0.0);
		EXPECT_GT(EvaluateSegment(negative, ResolveAuthored(negative), 0, 0.0)->tangent.y, 0.0);
	}

	TEST(PathEvaluatorTests, ArcRejectsInvalidInputsAndMixedLengthsInvert)
	{
		const Result<ArcGeometry> zeroChord = DeriveArc(glm::dvec3(0.0), glm::dvec3(0.0), {0.0f, 0.0f, 1.0f}, 1.0f);
		const Result<ArcGeometry> parallelNormal = DeriveArc(glm::dvec3(0.0), {1.0, 0.0, 0.0}, {1.0f, 0.0f, 0.0f}, 1.0f);
		const Result<ArcGeometry> invalidSweep = DeriveArc(glm::dvec3(0.0), {1.0, 0.0, 0.0}, {0.0f, 0.0f, 1.0f}, 0.0f);
		const Result<ArcGeometry> nonFinite = DeriveArc(glm::dvec3(std::numeric_limits<double>::quiet_NaN()), {1.0, 0.0, 0.0}, {0.0f, 0.0f, 1.0f}, 1.0f);
		ASSERT_FALSE(zeroChord);
		ASSERT_FALSE(parallelNormal);
		ASSERT_FALSE(invalidSweep);
		ASSERT_FALSE(nonFinite);
		EXPECT_STREQ(zeroChord.Error().code.c_str(), PathDiagnosticCodeName(PathDiagnosticCode::ZeroChord));
		EXPECT_STREQ(parallelNormal.Error().code.c_str(), PathDiagnosticCodeName(PathDiagnosticCode::ArcNormalParallelToChord));
		EXPECT_STREQ(invalidSweep.Error().code.c_str(), PathDiagnosticCodeName(PathDiagnosticCode::ArcSweepOutOfRange));
		EXPECT_STREQ(nonFinite.Error().code.c_str(), PathDiagnosticCodeName(PathDiagnosticCode::NonFinite));

		const ScenePath path = MakePath(
			{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 0.0f}},
			{Line(PathElementId{5}), Cubic(PathElementId{6}, {1.5f, 1.0f, 0.0f}, {3.5f, -1.0f, 0.0f}), Arc(PathElementId{7}, std::numbers::pi_v<float> / 2.0f)});
		const ResolvedNodes resolved = ResolveAuthored(path);
		const std::vector<double> cumulative = CumulativeLengths(path, resolved);
		ASSERT_EQ(cumulative.size(), 4u);
		EXPECT_LE(cumulative[0], cumulative[1]);
		EXPECT_LE(cumulative[1], cumulative[2]);
		EXPECT_LE(cumulative[2], cumulative[3]);
		double sum = 0.0;
		for (std::size_t index = 0; index < path.segments.size(); ++index)
		{
			const double length = SegmentLength(path, resolved, index).Value();
			sum += length;
			const double t = SegmentParamAtLength(path, resolved, index, length * 0.5).Value();
			EXPECT_NEAR(SegmentParamAtLength(path, resolved, index, 0.0).Value(), 0.0, 1e-12);
			EXPECT_NEAR(t, 0.5, 1e-8);
		}
		EXPECT_NEAR(cumulative.back(), sum, 1e-9);
	}
} // namespace DefectStudio::Tests
