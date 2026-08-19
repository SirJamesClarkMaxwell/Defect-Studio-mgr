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
} // namespace DefectStudio::Tests
