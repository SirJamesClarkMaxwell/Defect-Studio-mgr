#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>

#include "Renderer/Scene/SelectionHitTest.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] glm::mat4 BuildTestViewProjection()
		{
			const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
			return projection * view;
		}
	} // namespace

	TEST(SelectionHitTestTests, ProjectToScreenPlacesOriginAtViewportCenter)
	{
		const glm::mat4 viewProjection = BuildTestViewProjection();
		const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
			viewProjection, glm::vec2(800.0f, 600.0f), glm::vec3(0.0f));

		ASSERT_TRUE(screen.has_value());
		EXPECT_NEAR(screen->x, 400.0f, 0.01f);
		EXPECT_NEAR(screen->y, 300.0f, 0.01f);
	}

	TEST(SelectionHitTestTests, ProjectToScreenLeftOfTargetMapsLeftOfCenter)
	{
		const glm::mat4 viewProjection = BuildTestViewProjection();
		const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
			viewProjection, glm::vec2(800.0f, 600.0f), glm::vec3(-1.0f, 0.0f, 0.0f));

		ASSERT_TRUE(screen.has_value());
		EXPECT_LT(screen->x, 400.0f);
	}

	TEST(SelectionHitTestTests, ProjectToScreenReturnsNulloptBehindCamera)
	{
		const glm::mat4 viewProjection = BuildTestViewProjection();
		const std::optional<glm::vec2> screen = SelectionHitTest::ProjectToScreen(
			viewProjection, glm::vec2(800.0f, 600.0f), glm::vec3(0.0f, 0.0f, 10.0f));

		EXPECT_FALSE(screen.has_value());
	}

	TEST(SelectionHitTestTests, PointInRectBoundaryInclusive)
	{
		const glm::vec2 rectMin(10.0f, 10.0f);
		const glm::vec2 rectMax(20.0f, 20.0f);

		EXPECT_TRUE(SelectionHitTest::PointInRect(glm::vec2(15.0f, 15.0f), rectMin, rectMax));
		EXPECT_TRUE(SelectionHitTest::PointInRect(glm::vec2(10.0f, 10.0f), rectMin, rectMax));
		EXPECT_TRUE(SelectionHitTest::PointInRect(glm::vec2(20.0f, 20.0f), rectMin, rectMax));
		EXPECT_FALSE(SelectionHitTest::PointInRect(glm::vec2(9.9f, 15.0f), rectMin, rectMax));
		EXPECT_FALSE(SelectionHitTest::PointInRect(glm::vec2(15.0f, 20.1f), rectMin, rectMax));
	}

	TEST(SelectionHitTestTests, PointInCircleUsesRadiusInclusive)
	{
		const glm::vec2 center(50.0f, 50.0f);

		EXPECT_TRUE(SelectionHitTest::PointInCircle(glm::vec2(50.0f, 50.0f), center, 10.0f));
		EXPECT_TRUE(SelectionHitTest::PointInCircle(glm::vec2(60.0f, 50.0f), center, 10.0f));
		EXPECT_FALSE(SelectionHitTest::PointInCircle(glm::vec2(61.0f, 50.0f), center, 10.0f));
	}

	TEST(SelectionHitTestTests, DistancePointToSegmentProjectsOntoInterior)
	{
		const glm::vec2 segA(0.0f, 0.0f);
		const glm::vec2 segB(10.0f, 0.0f);

		EXPECT_NEAR(SelectionHitTest::DistancePointToSegment(glm::vec2(5.0f, 3.0f), segA, segB), 3.0f, 0.001f);
		EXPECT_NEAR(SelectionHitTest::DistancePointToSegment(glm::vec2(5.0f, 0.0f), segA, segB), 0.0f, 0.001f);
	}

	TEST(SelectionHitTestTests, DistancePointToSegmentClampsPastEndpoints)
	{
		const glm::vec2 segA(0.0f, 0.0f);
		const glm::vec2 segB(10.0f, 0.0f);

		// Closest point on the infinite line would be (13,0), off the far end of the segment - must
		// clamp to segB=(10,0) instead (3-4-5 triangle to it), same reasoning as
		// ClosestPointsRaySegmentClampsToSegmentEndpoint below but for this function's screen-space 2D
		// counterpart.
		EXPECT_NEAR(SelectionHitTest::DistancePointToSegment(glm::vec2(13.0f, 4.0f), segA, segB), 5.0f, 0.001f);
	}

	TEST(SelectionHitTestTests, PointInTriangleInsideAndOutside)
	{
		const glm::vec2 a(0.0f, 0.0f);
		const glm::vec2 b(10.0f, 0.0f);
		const glm::vec2 c(5.0f, 10.0f);

		EXPECT_TRUE(SelectionHitTest::PointInTriangle(glm::vec2(5.0f, 3.0f), a, b, c));
		EXPECT_FALSE(SelectionHitTest::PointInTriangle(glm::vec2(5.0f, -1.0f), a, b, c));
		// Same triangle, opposite winding (a/b swapped) - the sign-of-cross test must not depend on it.
		EXPECT_TRUE(SelectionHitTest::PointInTriangle(glm::vec2(5.0f, 3.0f), b, a, c));
	}

	TEST(SelectionHitTestTests, DistancePointToTriangle2DZeroInsideDistanceOutside)
	{
		const glm::vec2 a(0.0f, 0.0f);
		const glm::vec2 b(10.0f, 0.0f);
		const glm::vec2 c(5.0f, 10.0f);

		EXPECT_NEAR(SelectionHitTest::DistancePointToTriangle2D(glm::vec2(5.0f, 3.0f), a, b, c), 0.0f, 0.001f);
		// Straight below the a-b edge (which runs along y=0) - nearest edge distance is exactly 2.
		EXPECT_NEAR(SelectionHitTest::DistancePointToTriangle2D(glm::vec2(5.0f, -2.0f), a, b, c), 2.0f, 0.001f);
	}

	// Regression for a real bug: the segment parameter was computed with the closed-form RAY
	// parameter formula from a two-segment solve instead of its own (they only agree when the
	// segment has unit length or is perpendicular to the ray in a way that zeroes the cross term),
	// so bond-click hit-testing (which uses this against real bonds - arbitrary length, arbitrary
	// camera angle) picked the wrong point along the bond far more often than not. This case has
	// neither degeneracy: a length-2 segment at an oblique angle to the ray.
	TEST(SelectionHitTestTests, ClosestPointsRaySegmentFindsTrueClosestPointOnNonUnitObliqueSegment)
	{
		const glm::vec3 rayOrigin(0.0f, 0.0f, -5.0f);
		const glm::vec3 rayDir(0.0f, 0.0f, 1.0f);
		const glm::vec3 segA(1.0f, -1.0f, 0.0f);
		const glm::vec3 segB(1.0f, 1.0f, 0.0f);

		float t = 0.0f;
		glm::vec3 closestOnSegment(0.0f);
		SelectionHitTest::ClosestPointsRaySegment(rayOrigin, rayDir, segA, segB, t, closestOnSegment);

		// True closest point on the segment is its midpoint (1,0,0) at ray parameter t=5 - the buggy
		// formula instead landed on segB (1,1,0), a full half-length of the bond away.
		EXPECT_NEAR(closestOnSegment.x, 1.0f, 0.001f);
		EXPECT_NEAR(closestOnSegment.y, 0.0f, 0.001f);
		EXPECT_NEAR(closestOnSegment.z, 0.0f, 0.001f);
		EXPECT_NEAR(t, 5.0f, 0.001f);
	}

	TEST(SelectionHitTestTests, ClosestPointsRaySegmentClampsToSegmentEndpoint)
	{
		// The ray's closest approach to the segment's infinite line is at x=0 (the ray sits on the
		// z-axis), which lies beyond segB along the segment's direction - the unclamped solution
		// would put s outside [0,1], and the clamp should snap it to segB rather than leaving it
		// out of range.
		const glm::vec3 rayOrigin(0.0f, 0.0f, -5.0f);
		const glm::vec3 rayDir(0.0f, 0.0f, 1.0f);
		const glm::vec3 segA(-3.0f, 0.0f, 0.0f);
		const glm::vec3 segB(-2.0f, 0.0f, 0.0f);

		float t = 0.0f;
		glm::vec3 closestOnSegment(0.0f);
		SelectionHitTest::ClosestPointsRaySegment(rayOrigin, rayDir, segA, segB, t, closestOnSegment);

		EXPECT_NEAR(closestOnSegment.x, -2.0f, 0.001f);
		EXPECT_NEAR(closestOnSegment.y, 0.0f, 0.001f);
		EXPECT_NEAR(closestOnSegment.z, 0.0f, 0.001f);
	}
} // namespace DefectStudio::Tests
