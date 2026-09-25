#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "Renderer/Path/PathEvaluator.hpp"
#include "Renderer/Path/PathHandleRules.hpp"

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
			path.nextElementId = 40;
			return path;
		}

		PathSegment Cubic(PathElementId id, glm::vec3 start, glm::vec3 end, BezierHandleType type = BezierHandleType::Free)
		{
			return {id, CubicBezierSegmentData{{PathElementId{id.value + 10}, start, type}, {PathElementId{id.value + 20}, end, type}}};
		}
		PathSegment Line(PathElementId id) { return {id, LineSegmentData{}}; }
		PathSegment Arc(PathElementId id) { return {id, CircularArcSegmentData{{0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 2.0f}}; }

		ResolvedNodes ResolveAuthored(const ScenePath &path)
		{
			ResolvedNodes result;
			for (const PathNode &node : path.nodes)
				result.positions.push_back(node.position);
			return result;
		}

		void ExpectPosition(glm::vec3 actual, glm::vec3 expected)
		{
			EXPECT_NEAR(glm::distance(glm::dvec3(actual), glm::dvec3(expected)), 0.0, 1.0e-6);
		}
	} // namespace

	TEST(PathHandleRulesTests, MakeTangentUsesOneThirdOppositeCubicRays)
	{
		ScenePath path = MakePath(
			{glm::vec3(-3.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f, 4.0f, 0.0f)},
			{Cubic(PathElementId{4}, {-2.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}), Cubic(PathElementId{5}, {1.0f, 1.0f, 0.0f}, {1.0f, 3.0f, 0.0f})});
		ASSERT_TRUE(MakeTangent(path, PathElementId{2}));
		const glm::vec3 direction = glm::normalize(glm::vec3(3.0f, 4.0f, 0.0f));
		const auto &incoming = std::get<CubicBezierSegmentData>(path.segments[0].data).endHandle;
		const auto &outgoing = std::get<CubicBezierSegmentData>(path.segments[1].data).startHandle;
		ExpectPosition(incoming.position, -direction);
		ExpectPosition(outgoing.position, direction * (4.0f / 3.0f));
	}

	TEST(PathHandleRulesTests, MakeTangentAlignsCubicToRigidLineAndArc)
	{
		ScenePath lineCubic = MakePath(
			{glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(2.0f, 2.0f, 0.0f)},
			{Line(PathElementId{4}), Cubic(PathElementId{5}, {1.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 0.0f})});
		ASSERT_TRUE(MakeTangent(lineCubic, PathElementId{2}));
		const auto &lineHandle = std::get<CubicBezierSegmentData>(lineCubic.segments[1].data).startHandle;
		ExpectPosition(lineHandle.position, glm::vec3(glm::length(glm::vec3(2.0f, 2.0f, 0.0f)) / 3.0f, 0.0f, 0.0f));
		EXPECT_TRUE(std::holds_alternative<LineSegmentData>(lineCubic.segments[0].data));

		ScenePath cubicArc = MakePath(
			{glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)},
			{Cubic(PathElementId{4}, {-1.0f, 0.0f, 0.0f}, {-0.5f, -1.0f, 0.0f}), Arc(PathElementId{5})});
		ASSERT_TRUE(MakeTangent(cubicArc, PathElementId{2}));
		const glm::dvec3 tangent = EvaluateSegment(cubicArc, ResolveAuthored(cubicArc), 1, 0.0).Value().tangent;
		const auto &arcHandle = std::get<CubicBezierSegmentData>(cubicArc.segments[0].data).endHandle;
		const glm::dvec3 cubicTangent = glm::normalize(glm::dvec3(cubicArc.nodes[1].position) - glm::dvec3(arcHandle.position));
		EXPECT_NEAR(glm::dot(cubicTangent, tangent), 1.0, 1.0e-6);
		EXPECT_TRUE(std::holds_alternative<CircularArcSegmentData>(cubicArc.segments[1].data));
	}

	TEST(PathHandleRulesTests, MakeTangentRejectsRigidCornersAndDegeneracyAtomically)
	{
		for (const std::vector<PathSegment> &segments : std::vector<std::vector<PathSegment>>{{Line(PathElementId{4}), Line(PathElementId{5})}, {Line(PathElementId{4}), Arc(PathElementId{5})}, {Arc(PathElementId{4}), Arc(PathElementId{5})}})
		{
			ScenePath path = MakePath({glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f)}, segments);
			const ScenePath before = path;
			const Result<void> result = MakeTangent(path, PathElementId{2});
			ASSERT_FALSE(result);
			EXPECT_STREQ(result.Error().code.c_str(), PathDiagnosticCodeName(PathDiagnosticCode::TangentNotApplicable));
			EXPECT_EQ(path.nodes.size(), before.nodes.size());
			EXPECT_EQ(path.segments.size(), before.segments.size());
			EXPECT_EQ(path.nextElementId, before.nextElementId);
		}
		ScenePath degenerate = MakePath(
			{glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f)},
			{Cubic(PathElementId{4}, {-0.5f, 0.0f, 0.0f}, {-0.25f, 0.0f, 0.0f}), Cubic(PathElementId{5}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f})});
		const ScenePath before = degenerate;
		ASSERT_FALSE(MakeTangent(degenerate, PathElementId{2}));
		EXPECT_EQ(degenerate.nextElementId, before.nextElementId);
		EXPECT_TRUE(glm::all(glm::equal(std::get<CubicBezierSegmentData>(degenerate.segments[1].data).startHandle.position, std::get<CubicBezierSegmentData>(before.segments[1].data).startHandle.position)));
	}

	TEST(PathHandleRulesTests, ApplyAutoHandlesOnlyChangesAutoHandles)
	{
		ScenePath path = MakePath(
			{glm::vec3(-2.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f)},
			{Cubic(PathElementId{4}, {-1.0f, 2.0f, 0.0f}, {-1.0f, 2.0f, 0.0f}, BezierHandleType::Auto), Cubic(PathElementId{5}, {1.0f, 2.0f, 0.0f}, {1.0f, 2.0f, 0.0f}, BezierHandleType::Auto)});
		std::get<CubicBezierSegmentData>(path.segments[0].data).startHandle.type = BezierHandleType::Free;
		const glm::vec3 freePosition = std::get<CubicBezierSegmentData>(path.segments[0].data).startHandle.position;
		std::get<CubicBezierSegmentData>(path.segments[1].data).endHandle.type = BezierHandleType::Vector;
		const glm::vec3 vectorPosition = std::get<CubicBezierSegmentData>(path.segments[1].data).endHandle.position;
		ASSERT_TRUE(ApplyAutoHandles(path));
		EXPECT_TRUE(glm::all(glm::equal(std::get<CubicBezierSegmentData>(path.segments[0].data).startHandle.position, freePosition)));
		EXPECT_TRUE(glm::all(glm::equal(std::get<CubicBezierSegmentData>(path.segments[1].data).endHandle.position, vectorPosition)));
		ExpectPosition(std::get<CubicBezierSegmentData>(path.segments[0].data).endHandle.position, glm::vec3(-2.0f / 3.0f, 0.0f, 0.0f));
		ExpectPosition(std::get<CubicBezierSegmentData>(path.segments[1].data).startHandle.position, glm::vec3(2.0f / 3.0f, 0.0f, 0.0f));
		EXPECT_TRUE(std::holds_alternative<CubicBezierSegmentData>(path.segments[0].data));
		EXPECT_TRUE(std::holds_alternative<CubicBezierSegmentData>(path.segments[1].data));
	}
} // namespace DefectStudio::Tests
