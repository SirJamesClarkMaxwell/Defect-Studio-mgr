#include <gtest/gtest.h>

#include "Presentation/Panels/SceneArrowEditorWidget.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneArrowAtomMatchTests, PositionUsesTheTwoAtomPositionsInSelectionOrder)
	{
		RendererWindowState::SceneArrow arrow;
		const RendererAtomData first{"C", glm::vec3(1.0f, 2.0f, 3.0f)};
		const RendererAtomData second{"O", glm::vec3(-4.0f, 5.0f, 6.0f)};

		MatchSceneArrowPositionToAtoms(arrow, first, second);

		EXPECT_EQ(arrow.start, first.cartesianPosition);
		EXPECT_EQ(arrow.end, second.cartesianPosition);
	}

	TEST(SceneArrowAtomMatchTests, TheBufferLeavesAGapOfOneRadiusAtEachEnd)
	{
		RendererWindowState::SceneArrow unbuffered;
		RendererWindowState::SceneArrow buffered;
		const RendererAtomData first{"C", glm::vec3(0.0f), glm::vec3(0.0f), 0.5f};
		const RendererAtomData second{"O", glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0.25f};

		MatchSceneArrowPositionToAtoms(unbuffered, first, second, 0.0f);
		MatchSceneArrowPositionToAtoms(buffered, first, second, 1.0f);

		EXPECT_GT(glm::distance(buffered.start, first.cartesianPosition),
			glm::distance(unbuffered.start, first.cartesianPosition));
		EXPECT_GT(glm::distance(buffered.end, second.cartesianPosition),
			glm::distance(unbuffered.end, second.cartesianPosition));
		EXPECT_LT(buffered.start.x, buffered.end.x);
	}

	TEST(SceneArrowAtomMatchTests, ABufferBiggerThanTheGapDoesNotInvertTheArrow)
	{
		RendererWindowState::SceneArrow arrow;
		// Overlapping spheres: 2.0 + 2.0 of trim over a 1.0 separation would put start past end.
		const RendererAtomData first{"C", glm::vec3(0.0f), glm::vec3(0.0f), 2.0f};
		const RendererAtomData second{"C", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f), 2.0f};

		MatchSceneArrowPositionToAtoms(arrow, first, second, 1.0f);

		EXPECT_LT(arrow.start.x, arrow.end.x);
		EXPECT_NEAR(arrow.end.x - arrow.start.x, 0.1f, 1e-5f);
	}

	TEST(SceneArrowAtomMatchTests, OneAtomColorCreatesAFlatElementColor)
	{
		RendererWindowState::SceneArrow arrow;
		arrow.style.useGradient = true;
		const RendererAtomData atom{"N", glm::vec3(0.0f), glm::vec3(0.15f, 0.25f, 0.95f)};

		MatchSceneArrowColorToAtom(arrow, atom);

		EXPECT_FALSE(arrow.style.useGradient);
		EXPECT_EQ(arrow.style.color, atom.color);
	}

	TEST(SceneArrowAtomMatchTests, TwoAtomColorsCreateAnOrderedGradient)
	{
		RendererWindowState::SceneArrow arrow;
		const RendererAtomData first{"C", glm::vec3(0.0f), glm::vec3(0.2f, 0.2f, 0.2f)};
		const RendererAtomData second{"O", glm::vec3(0.0f), glm::vec3(0.9f, 0.1f, 0.1f)};

		MatchSceneArrowColorToAtoms(arrow, first, second);

		EXPECT_TRUE(arrow.style.useGradient);
		EXPECT_EQ(arrow.style.gradient.start, first.color);
		EXPECT_EQ(arrow.style.gradient.finish, second.color);
	}

	TEST(SceneArrowAtomMatchTests, ReverseSwapsEndpointsWithoutRewritingGradientStops)
	{
		RendererWindowState::SceneArrow arrow;
		arrow.start = glm::vec3(1.0f, 2.0f, 3.0f);
		arrow.end = glm::vec3(4.0f, 5.0f, 6.0f);
		arrow.style.useGradient = true;
		arrow.style.gradient.start = glm::vec3(1.0f, 0.0f, 0.0f);
		arrow.style.gradient.finish = glm::vec3(0.0f, 0.0f, 1.0f);

		ReverseSceneArrow(arrow);

		EXPECT_EQ(arrow.start, glm::vec3(4.0f, 5.0f, 6.0f));
		EXPECT_EQ(arrow.end, glm::vec3(1.0f, 2.0f, 3.0f));
		EXPECT_EQ(arrow.style.gradient.start, glm::vec3(1.0f, 0.0f, 0.0f));
		EXPECT_EQ(arrow.style.gradient.finish, glm::vec3(0.0f, 0.0f, 1.0f));
	}

	TEST(SceneArrowAtomMatchTests, GateExplainsBothInvalidSelections)
	{
		const SceneArrowAtomMatchDescription none = DescribeSceneArrowAtomMatch(0);
		EXPECT_FALSE(none.canMatchPosition);
		EXPECT_FALSE(none.canMatchColor);
		EXPECT_EQ(none.positionTooltip, "Zaznacz dokladnie dwa atomy, aby dopasowac pozycje.");
		EXPECT_EQ(none.colorTooltip, "Zaznacz jeden lub dwa atomy, aby dopasowac kolor.");

		const SceneArrowAtomMatchDescription one = DescribeSceneArrowAtomMatch(1);
		EXPECT_FALSE(one.canMatchPosition);
		EXPECT_TRUE(one.canMatchColor);
		EXPECT_TRUE(one.colorTooltip.empty());

		const SceneArrowAtomMatchDescription two = DescribeSceneArrowAtomMatch(2);
		EXPECT_TRUE(two.canMatchPosition);
		EXPECT_TRUE(two.canMatchColor);
		EXPECT_TRUE(two.positionTooltip.empty());

		const SceneArrowAtomMatchDescription three = DescribeSceneArrowAtomMatch(3);
		EXPECT_FALSE(three.canMatchPosition);
		EXPECT_FALSE(three.canMatchColor);
	}
} // namespace DefectStudio::Tests
