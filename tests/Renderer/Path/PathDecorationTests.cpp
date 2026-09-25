#include "Core/dspch.hpp"

#include <gtest/gtest.h>

#include "Renderer/Path/PathDecoration.hpp"

namespace DefectStudio::Tests
{
	TEST(PathDecorationTests, ContoursHaveValidDistinctLegacyLikeProperties)
	{
		const auto none = BuildDecorationContour({}, 1.0);
		EXPECT_TRUE(none.points.empty());
		EXPECT_DOUBLE_EQ(none.trim, 0.0);
		PathEndpointDecoration arrow{PathDecorationKind::Arrow};
		PathEndpointDecoration stealth{PathDecorationKind::Stealth};
		PathEndpointDecoration open{PathDecorationKind::OpenArrow};
		PathEndpointDecoration bar{PathDecorationKind::Bar};
		const auto arrowContour = BuildDecorationContour(arrow, 2.0);
		const auto stealthContour = BuildDecorationContour(stealth, 2.0);
		const auto openContour = BuildDecorationContour(open, 2.0);
		const auto barContour = BuildDecorationContour(bar, 2.0);
		EXPECT_TRUE(arrowContour.closesBack);
		EXPECT_FALSE(stealthContour.closesBack);
		EXPECT_FALSE(openContour.filled);
		EXPECT_LT(barContour.trim, arrowContour.trim);
		EXPECT_GT(barContour.points.front().halfWidth, arrowContour.points.front().halfWidth);
		for (const auto &point : arrowContour.points)
			EXPECT_GE(point.halfWidth, 0.0);
		for (const PathDecorationKind kind : {PathDecorationKind::Arrow, PathDecorationKind::Stealth, PathDecorationKind::OpenArrow,
			PathDecorationKind::Bar, PathDecorationKind::Circle, PathDecorationKind::Square, PathDecorationKind::Diamond})
			EXPECT_GE(BuildDecorationContour({kind}, 1.0).points.size(), 3u);
	}

	TEST(PathDecorationTests, TrimmedRangeUsesDecorationInsertionLength)
	{
		PathStrokeStyle style;
		style.width = 2.0f;
		style.startDecoration.kind = PathDecorationKind::Arrow;
		style.endDecoration.kind = PathDecorationKind::Circle;
		const ShaftRange range = TrimmedRange(10.0, style);
		EXPECT_NEAR(range.start, BuildDecorationContour(style.startDecoration, style.width).trim, 1e-12);
		EXPECT_NEAR(range.end, 10.0 - BuildDecorationContour(style.endDecoration, style.width).trim, 1e-12);
		EXPECT_TRUE(TrimmedRange(0.1, style).IsEmpty());
	}
} // namespace DefectStudio::Tests
