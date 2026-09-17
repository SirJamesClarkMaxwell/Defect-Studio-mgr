#include <gtest/gtest.h>

#include "Renderer/Scene/SceneObjectAppearance.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneArrowRenderColorsTests, GradientKeepsDistinctStartAndFinishColors)
	{
		RendererWindowState::ArrowStyle style;
		style.useGradient = true;
		style.gradient.start = glm::vec3(1.0f, 0.0f, 0.0f);
		style.gradient.finish = glm::vec3(0.0f, 0.0f, 1.0f);
		style.alpha = 0.6f;

		const SceneArrowRenderColors colors = ResolveSceneArrowRenderColors(style, false);

		EXPECT_FLOAT_EQ(colors.start.r, style.gradient.start.r);
		EXPECT_FLOAT_EQ(colors.start.g, style.gradient.start.g);
		EXPECT_FLOAT_EQ(colors.start.b, style.gradient.start.b);
		EXPECT_FLOAT_EQ(colors.finish.r, style.gradient.finish.r);
		EXPECT_FLOAT_EQ(colors.finish.g, style.gradient.finish.g);
		EXPECT_FLOAT_EQ(colors.finish.b, style.gradient.finish.b);
		EXPECT_FLOAT_EQ(colors.start.a, style.alpha);
		EXPECT_FLOAT_EQ(colors.finish.a, style.alpha);
	}

	TEST(SceneArrowRenderColorsTests, FlatArrowUsesItsColorAtBothEnds)
	{
		RendererWindowState::ArrowStyle style;
		style.useGradient = false;
		style.color = glm::vec3(0.2f, 0.4f, 0.8f);

		const SceneArrowRenderColors colors = ResolveSceneArrowRenderColors(style, false);

		EXPECT_FLOAT_EQ(colors.start.r, colors.finish.r);
		EXPECT_FLOAT_EQ(colors.start.g, colors.finish.g);
		EXPECT_FLOAT_EQ(colors.start.b, colors.finish.b);
		EXPECT_FLOAT_EQ(colors.start.r, style.color.r);
		EXPECT_FLOAT_EQ(colors.start.g, style.color.g);
		EXPECT_FLOAT_EQ(colors.start.b, style.color.b);
	}

	TEST(SceneObjectSelectionColorTests, SelectionBlendsEveryColorTowardTheSharedAccent)
	{
		const glm::vec3 base(0.1f, 0.2f, 0.3f);

		const glm::vec3 unchanged = ApplySceneSelectionHighlight(base, false);
		const glm::vec3 selected = ApplySceneSelectionHighlight(base, true);
		const glm::vec3 expected = glm::mix(base, SceneSelectionHighlightColor(), 0.55f);

		EXPECT_FLOAT_EQ(unchanged.r, base.r);
		EXPECT_FLOAT_EQ(unchanged.g, base.g);
		EXPECT_FLOAT_EQ(unchanged.b, base.b);
		EXPECT_FLOAT_EQ(selected.r, expected.r);
		EXPECT_FLOAT_EQ(selected.g, expected.g);
		EXPECT_FLOAT_EQ(selected.b, expected.b);
	}
} // namespace DefectStudio::Tests
