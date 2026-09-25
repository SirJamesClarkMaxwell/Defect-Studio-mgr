#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathDash.hpp"
#include "Renderer/Path/PathTopology.hpp"

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
			path.nextElementId = 20;
			return path;
		}

		ResolvedNodes ResolveAuthored(const ScenePath &path)
		{
			ResolvedNodes result;
			for (const PathNode &node : path.nodes)
				result.positions.push_back(node.position);
			return result;
		}

		PathSegment Line(PathElementId id) { return {id, LineSegmentData{}}; }
		PathSegment Arc(PathElementId id, float sweep) { return {id, CircularArcSegmentData{{0.0f, 0.0f, 1.0f}, sweep}}; }
		PathSegment Cubic(PathElementId id, glm::vec3 start, glm::vec3 end, BezierHandleType type = BezierHandleType::Free)
		{
			return {id, CubicBezierSegmentData{{PathElementId{30}, start, type}, {PathElementId{31}, end, type}}};
		}

		void ExpectSamePathShape(const ScenePath &actual, const ScenePath &expected)
		{
			ASSERT_EQ(actual.nodes.size(), expected.nodes.size());
			ASSERT_EQ(actual.segments.size(), expected.segments.size());
			EXPECT_EQ(actual.nextElementId, expected.nextElementId);
			for (std::size_t index = 0; index < actual.nodes.size(); ++index)
			{
				EXPECT_EQ(actual.nodes[index].id, expected.nodes[index].id);
				EXPECT_TRUE(glm::all(glm::equal(actual.nodes[index].position, expected.nodes[index].position)));
			}
			for (std::size_t index = 0; index < actual.segments.size(); ++index)
				EXPECT_EQ(actual.segments[index].id, expected.segments[index].id);
		}

		void ExpectSameSegmentData(const PathSegment &actual, const PathSegment &expected)
		{
			ASSERT_EQ(actual.data.index(), expected.data.index());
			if (const auto *actualCubic = std::get_if<CubicBezierSegmentData>(&actual.data))
			{
				const auto &expectedCubic = std::get<CubicBezierSegmentData>(expected.data);
				EXPECT_EQ(actualCubic->startHandle.id, expectedCubic.startHandle.id);
				EXPECT_EQ(actualCubic->endHandle.id, expectedCubic.endHandle.id);
				EXPECT_EQ(actualCubic->startHandle.type, expectedCubic.startHandle.type);
				EXPECT_EQ(actualCubic->endHandle.type, expectedCubic.endHandle.type);
				EXPECT_TRUE(glm::all(glm::equal(actualCubic->startHandle.position, expectedCubic.startHandle.position)));
				EXPECT_TRUE(glm::all(glm::equal(actualCubic->endHandle.position, expectedCubic.endHandle.position)));
			}
			else if (const auto *actualArc = std::get_if<CircularArcSegmentData>(&actual.data))
			{
				const auto &expectedArc = std::get<CircularArcSegmentData>(expected.data);
				EXPECT_TRUE(glm::all(glm::equal(actualArc->planeNormal, expectedArc.planeNormal)));
				EXPECT_EQ(actualArc->signedSweepRadians, expectedArc.signedSweepRadians);
			}
		}

		void ExpectSplitPreservesCurve(const ScenePath &original, const ScenePath &split, double at)
		{
			for (int sample = 0; sample <= 64; ++sample)
			{
				const double t = static_cast<double>(sample) / 64.0;
				const PathSample before = EvaluateSegment(original, ResolveAuthored(original), 0, t).Value();
				const std::size_t segment = t <= at ? 0 : 1;
				const double local = t <= at ? t / at : (t - at) / (1.0 - at);
				const PathSample after = EvaluateSegment(split, ResolveAuthored(split), segment, local).Value();
				EXPECT_NEAR(glm::distance(before.position, after.position), 0.0, 1.0e-6);
			}
		}
	} // namespace

	TEST(PathTopologyTests, InsertNodeSplitsLinesAndPreservesCurve)
	{
		const ScenePath original = MakePath({glm::vec3(0.0f), glm::vec3(8.0f, 4.0f, 0.0f)}, {Line(PathElementId{3})});
		ScenePath path = original;
		const Result<PathElementId> inserted = InsertNode(path, 0, 0.25);
		ASSERT_TRUE(inserted);
		EXPECT_TRUE(glm::all(glm::equal(path.nodes[1].position, glm::vec3(2.0f, 1.0f, 0.0f))));
		ASSERT_TRUE(std::holds_alternative<LineSegmentData>(path.segments[0].data));
		ASSERT_TRUE(std::holds_alternative<LineSegmentData>(path.segments[1].data));
		ExpectSplitPreservesCurve(original, path, 0.25);
	}

	TEST(PathTopologyTests, InsertNodeSplitsCubicByDeCasteljau)
	{
		const ScenePath original = MakePath({glm::vec3(0.0f), glm::vec3(6.0f, 0.0f, 0.0f)}, {Cubic(PathElementId{3}, {1.0f, 3.0f, 0.0f}, {5.0f, -3.0f, 0.0f}, BezierHandleType::Aligned)});
		ScenePath path = original;
		ASSERT_TRUE(InsertNode(path, 0, 0.5));
		const auto &left = std::get<CubicBezierSegmentData>(path.segments[0].data);
		const auto &right = std::get<CubicBezierSegmentData>(path.segments[1].data);
		EXPECT_TRUE(glm::all(glm::equal(path.nodes[1].position, glm::vec3(3.0f, 0.0f, 0.0f))));
		EXPECT_TRUE(glm::all(glm::equal(left.startHandle.position, glm::vec3(0.5f, 1.5f, 0.0f))));
		EXPECT_TRUE(glm::all(glm::equal(left.endHandle.position, glm::vec3(1.75f, 0.75f, 0.0f))));
		EXPECT_TRUE(glm::all(glm::equal(right.startHandle.position, glm::vec3(4.25f, -0.75f, 0.0f))));
		EXPECT_TRUE(glm::all(glm::equal(right.endHandle.position, glm::vec3(5.5f, -1.5f, 0.0f))));
		EXPECT_EQ(left.startHandle.type, BezierHandleType::Aligned);
		EXPECT_EQ(right.endHandle.type, BezierHandleType::Aligned);
		ExpectSplitPreservesCurve(original, path, 0.5);
	}

	TEST(PathTopologyTests, InsertNodeSplitsArcAndKeepsRadius)
	{
		const float sweep = std::numbers::pi_v<float> / 2.0f;
		const ScenePath original = MakePath({glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f)}, {Arc(PathElementId{3}, sweep)});
		ScenePath path = original;
		ASSERT_TRUE(InsertNode(path, 0, 0.5));
		const auto &left = std::get<CircularArcSegmentData>(path.segments[0].data);
		const auto &right = std::get<CircularArcSegmentData>(path.segments[1].data);
		EXPECT_NEAR(left.signedSweepRadians, sweep * 0.5f, 1.0e-7f);
		EXPECT_NEAR(right.signedSweepRadians, sweep * 0.5f, 1.0e-7f);
		const double radius = DeriveArc(glm::dvec3(original.nodes[0].position), glm::dvec3(original.nodes[1].position), {0.0f, 0.0f, 1.0f}, sweep).Value().radius;
		EXPECT_NEAR(DeriveArc(glm::dvec3(path.nodes[0].position), glm::dvec3(path.nodes[1].position), left.planeNormal, left.signedSweepRadians).Value().radius, radius, radius * 1.0e-6);
		EXPECT_NEAR(DeriveArc(glm::dvec3(path.nodes[1].position), glm::dvec3(path.nodes[2].position), right.planeNormal, right.signedSweepRadians).Value().radius, radius, radius * 1.0e-6);
		ExpectSplitPreservesCurve(original, path, 0.5);
	}

	TEST(PathTopologyTests, InsertNodeRejectsInvalidInputAtomically)
	{
		const ScenePath original = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, {Line(PathElementId{3})});
		for (const double t : {-0.1, 0.0, 1.0, 1.1, std::numeric_limits<double>::quiet_NaN()})
		{
			ScenePath path = original;
			const Result<PathElementId> result = InsertNode(path, 0, t);
			ASSERT_FALSE(result);
			ExpectSamePathShape(path, original);
		}
		ScenePath path = original;
		const Result<PathElementId> result = InsertNode(path, 3, 0.5);
		ASSERT_FALSE(result);
		EXPECT_STREQ(result.Error().code.c_str(), PathDiagnosticCodeName(PathDiagnosticCode::InvalidSegmentIndex));
		ExpectSamePathShape(path, original);
	}

	TEST(PathTopologyTests, ExtendEndInheritsTerminalTypeAndKeepsExistingNodes)
	{
		ScenePath line = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, {Line(PathElementId{3})});
		const PathElementId originalEnd = line.nodes.back().id;
		ASSERT_TRUE(ExtendEnd(line, PathEnd::End, {2.0f, 0.0f, 0.0f}));
		EXPECT_EQ(line.nodes[1].id, originalEnd);
		EXPECT_TRUE(std::holds_alternative<LineSegmentData>(line.segments.back().data));

		ScenePath cubic = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, {Cubic(PathElementId{3}, {0.25f, 1.0f, 0.0f}, {0.75f, -1.0f, 0.0f})});
		ASSERT_TRUE(ExtendEnd(cubic, PathEnd::Start, {-1.0f, 0.0f, 0.0f}));
		const auto &cubicData = std::get<CubicBezierSegmentData>(cubic.segments.front().data);
		EXPECT_EQ(cubicData.startHandle.type, BezierHandleType::Vector);
		EXPECT_EQ(cubicData.endHandle.type, BezierHandleType::Vector);

		ScenePath arc = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, {Arc(PathElementId{3}, std::numbers::pi_v<float> / 2.0f)});
		ASSERT_TRUE(ExtendEnd(arc, PathEnd::End, {2.0f, 0.0f, 0.0f}));
		const auto &arcData = std::get<CircularArcSegmentData>(arc.segments.back().data);
		EXPECT_TRUE(glm::all(glm::equal(arcData.planeNormal, glm::vec3(0.0f, 0.0f, 1.0f))));
		EXPECT_EQ(arcData.signedSweepRadians, std::numbers::pi_v<float> / 2.0f);

		ScenePath single = MakePath({glm::vec3(0.0f)}, {});
		ASSERT_TRUE(ExtendEnd(single, PathEnd::End, {1.0f, 0.0f, 0.0f}));
		EXPECT_TRUE(std::holds_alternative<LineSegmentData>(single.segments.front().data));
	}

	TEST(PathTopologyTests, DeleteNodeHandlesMatrixAndRejectionsAtomically)
	{
		ScenePath lines = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f)}, {Line(PathElementId{4}), Line(PathElementId{5})});
		ASSERT_TRUE(DeleteNode(lines, PathElementId{2}));
		EXPECT_EQ(lines.nodes.size(), 2u);
		EXPECT_EQ(lines.segments.size(), 1u);
		EXPECT_TRUE(std::holds_alternative<LineSegmentData>(lines.segments.front().data));

		const ScenePath mixed = MakePath({glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 0.0f)}, {Line(PathElementId{4}), Arc(PathElementId{5}, std::numbers::pi_v<float> / 2.0f)});
		ScenePath rejected = mixed;
		const Result<void> merge = DeleteNode(rejected, PathElementId{2});
		ASSERT_FALSE(merge);
		EXPECT_STREQ(merge.Error().code.c_str(), PathDiagnosticCodeName(PathDiagnosticCode::MergeRequiresLineNeighbours));
		ExpectSamePathShape(rejected, mixed);

		ASSERT_TRUE(DeleteNode(lines, lines.nodes.front().id));
		EXPECT_EQ(lines.nodes.size(), 1u);
		EXPECT_TRUE(lines.segments.empty());
		const ScenePath last = lines;
		EXPECT_FALSE(DeleteNode(lines, lines.nodes.front().id));
		ExpectSamePathShape(lines, last);
		EXPECT_FALSE(DeleteNode(lines, PathElementId{999}));
	}

	TEST(PathTopologyTests, ReverseIsExactInvolutionAndPreservesGeometry)
	{
		ScenePath path = MakePath(
			{glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(3.0f, 1.0f, 0.0f)},
			{Cubic(PathElementId{4}, {0.5f, 1.0f, 0.0f}, {1.5f, -1.0f, 0.0f}), Arc(PathElementId{5}, std::numbers::pi_v<float> / 2.0f)});
		const ScenePath original = path;
		ASSERT_TRUE(ReversePath(path));
		EXPECT_EQ(path.nodes.front().id, original.nodes.back().id);
		EXPECT_NEAR(std::get<CircularArcSegmentData>(path.segments.front().data).signedSweepRadians, -std::get<CircularArcSegmentData>(original.segments.back().data).signedSweepRadians, 0.0f);
		for (int sample = 0; sample <= 64; ++sample)
		{
			const double t = static_cast<double>(sample) / 64.0;
			const PathSample before = EvaluateSegment(original, ResolveAuthored(original), 0, t).Value();
			const PathSample after = EvaluateSegment(path, ResolveAuthored(path), 1, 1.0 - t).Value();
			EXPECT_NEAR(glm::distance(before.position, after.position), 0.0, 1.0e-6);
			EXPECT_NEAR(glm::dot(before.tangent, after.tangent), -1.0, 1.0e-6);
		}
		ASSERT_TRUE(ReversePath(path));
		ExpectSamePathShape(path, original);
		for (std::size_t index = 0; index < path.segments.size(); ++index)
			ExpectSameSegmentData(path.segments[index], original.segments[index]);
		EXPECT_EQ(std::get<CircularArcSegmentData>(path.segments[1].data).signedSweepRadians, std::get<CircularArcSegmentData>(original.segments[1].data).signedSweepRadians);
	}

	TEST(PathTopologyTests, ReverseMirrorsStyledPathDecorationsGradientAndDashPhase)
	{
		ScenePath path = MakePath({glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f)}, {Line(PathElementId{3})});
		path.style.startDecoration.kind = PathDecorationKind::Arrow;
		path.style.endDecoration.kind = PathDecorationKind::Circle;
		path.style.gradient.enabled = true;
		path.style.gradient.stops = {{0.2f, glm::vec3(1.0f, 0.0f, 0.0f), 1.0f}, {0.7f, glm::vec3(0.0f, 0.0f, 1.0f), 0.5f}};
		path.style.dash = {true, 2.0f, 1.0f, 0.25f};
		const ScenePath original = path;
		ASSERT_TRUE(ReversePath(path));
		EXPECT_EQ(path.style.startDecoration.kind, PathDecorationKind::Circle);
		EXPECT_EQ(path.style.endDecoration.kind, PathDecorationKind::Arrow);
		ASSERT_EQ(path.style.gradient.stops.size(), 2u);
		EXPECT_NEAR(path.style.gradient.stops[0].position, 0.3f, 1e-6f);
		EXPECT_NEAR(path.style.gradient.stops[1].position, 0.8f, 1e-6f);
		// The phase value itself is an implementation detail; what has to hold is that a dash covering
		// world position s before the reversal covers L - s after it.
		const std::vector<DashInterval> before = BuildDashIntervals(0.0, 10.0, original.style.dash);
		const std::vector<DashInterval> after = BuildDashIntervals(0.0, 10.0, path.style.dash);
		ASSERT_EQ(before.size(), after.size());
		for (std::size_t index = 0; index < before.size(); ++index)
		{
			const DashInterval &mirrored = after[after.size() - 1u - index];
			EXPECT_NEAR(10.0 - before[index].end, mirrored.start, 1e-6);
			EXPECT_NEAR(10.0 - before[index].start, mirrored.end, 1e-6);
		}
	}
} // namespace DefectStudio::Tests
