#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Renderer/Scene/ViewportNavigationMath.hpp"

namespace DefectStudio::Tests
{
	TEST(ViewportNavigationMathTests, ProjectionOrderDrawsBackMarkersBeforeFrontMarkers)
	{
		const OrientationAxes axes = {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f)};
		const auto markers = ProjectNavigationAxisMarkers(glm::mat4(1.0f), axes, glm::vec2(20.0f), 10.0f);
		const auto order = SortNavigationMarkersBackToFront(markers);

		EXPECT_EQ(markers[order.front()].axis, 2);
		EXPECT_EQ(markers[order.front()].sign, -1);
		EXPECT_EQ(markers[order.back()].axis, 2);
		EXPECT_EQ(markers[order.back()].sign, 1);
	}

	TEST(ViewportNavigationMathTests, HitTestChoosesNearestFrontCircleWhenMarkersOverlap)
	{
		const OrientationAxes axes = {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f)};
		const auto markers = ProjectNavigationAxisMarkers(glm::mat4(1.0f), axes, glm::vec2(40.0f), 20.0f);
		const auto hit = HitTestNavigationAxisMarkers(markers, glm::vec2(40.0f), 10.0f, 8.0f);

		ASSERT_TRUE(hit.has_value());
		EXPECT_EQ(markers[*hit].axis, 2);
		EXPECT_EQ(markers[*hit].sign, 1);
	}

	TEST(ViewportNavigationMathTests, ClickingAlignedSideAgainReturnsOppositeSide)
	{
		EXPECT_EQ(
			ResolveNavigationViewSide(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::vec3(-1.0f, 0.0f, 0.0f));
		EXPECT_EQ(
			ResolveNavigationViewSide(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)),
			glm::vec3(1.0f, 0.0f, 0.0f));
	}

	TEST(ViewportNavigationMathTests, LatticeAxesAreNormalizedWithoutChangingDirection)
	{
		TransformBases bases;
		glm::mat3 lattice(1.0f);
		lattice[0] = glm::vec3(2.0f, 0.0f, 0.0f);
		lattice[1] = glm::vec3(0.0f, 3.0f, 4.0f);
		lattice[2] = glm::vec3(0.0f, 0.0f, -7.0f);
		bases.lattice = lattice;

		const OrientationAxes axes = ResolveNormalizedOrientationAxes(TransformOrientation::Lattice, bases);

		EXPECT_EQ(axes[0], glm::vec3(1.0f, 0.0f, 0.0f));
		EXPECT_EQ(axes[1], glm::vec3(0.0f, 0.6f, 0.8f));
		EXPECT_EQ(axes[2], glm::vec3(0.0f, 0.0f, -1.0f));
	}
} // namespace DefectStudio::Tests
