#include "Core/dspch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <gtest/gtest.h>

#include "Renderer/Path/PathDecoration.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr PathDecorationKind kAllKinds[] = {
			PathDecorationKind::None, PathDecorationKind::Arrow, PathDecorationKind::Stealth,
			PathDecorationKind::Latex, PathDecorationKind::Bar, PathDecorationKind::Circle,
			PathDecorationKind::Square, PathDecorationKind::Diamond, PathDecorationKind::Kite};

		void ExpectFiniteAndOrdered(const DecorationContour &contour)
		{
			for (std::size_t index = 0; index < contour.points.size(); ++index)
			{
				const DecorationContourPoint &point = contour.points[index];
				EXPECT_TRUE(std::isfinite(point.s));
				EXPECT_TRUE(std::isfinite(point.halfWidth));
				EXPECT_GE(point.s, 0.0);
				EXPECT_GE(point.halfWidth, 0.0);
				if (index > 0)
				{
					ASSERT_LT(index - 1u, contour.points.size());
					EXPECT_LE(contour.points[index - 1u].s, point.s);
				}
			}
		}
	}

	TEST(PathDecorationTests, SquareKeepsAConstantHalfWidth)
	{
		constexpr double strokeWidth = 2.0;
		const PathEndpointDecoration decoration{PathDecorationKind::Square, 1.5f, 0.75f, true};
		const DecorationContour contour = BuildDecorationContour(decoration, strokeWidth);
		ASSERT_FALSE(contour.points.empty());
		ASSERT_GE(contour.points.size(), 2u);
		const double expectedLength = decoration.lengthScale * strokeWidth;
		const double expectedHalfWidth = decoration.widthScale * strokeWidth;
		EXPECT_NEAR(contour.points.front().s, 0.0, 1e-12);
		EXPECT_NEAR(contour.points.back().s, expectedLength, 1e-12);
		for (const DecorationContourPoint &point : contour.points)
			EXPECT_NEAR(point.halfWidth, expectedHalfWidth, 1e-12);
		EXPECT_TRUE(contour.closesBack);
	}

	TEST(PathDecorationTests, PointedDecorationsFlareBeyondTubeRadius)
	{
		constexpr double strokeWidth = 0.05;
		constexpr double tubeRadius = strokeWidth / 2.0;
		constexpr PathDecorationKind pointedKinds[] = {PathDecorationKind::Arrow,
			PathDecorationKind::Stealth, PathDecorationKind::Latex, PathDecorationKind::Diamond,
			PathDecorationKind::Kite};

		for (const PathDecorationKind kind : pointedKinds)
		{
			const DecorationContour contour = BuildDecorationContour({kind}, strokeWidth);
			ASSERT_FALSE(contour.points.empty()) << static_cast<int>(kind);
			const auto widest = std::max_element(contour.points.begin(), contour.points.end(),
				[](const DecorationContourPoint &a, const DecorationContourPoint &b) {
					return a.halfWidth < b.halfWidth;
				});
			ASSERT_NE(widest, contour.points.end());
			EXPECT_GT(widest->halfWidth, tubeRadius) << static_cast<int>(kind);
		}
	}

	TEST(PathDecorationTests, CircleSamplesTheContractHalfCircle)
	{
		constexpr double strokeWidth = 2.0;
		const PathEndpointDecoration decoration{PathDecorationKind::Circle, 1.25f, 0.8f, true};
		const DecorationContour contour = BuildDecorationContour(decoration, strokeWidth);
		ASSERT_GE(contour.points.size(), 12u);
		const double length = decoration.lengthScale * strokeWidth;
		const double radius = length * 0.5;
		const double centre = length * 0.5;
		for (const DecorationContourPoint &point : contour.points)
			EXPECT_NEAR((point.s - centre) * (point.s - centre) + point.halfWidth * point.halfWidth,
				radius * radius, 1e-9);
	}

	TEST(PathDecorationTests, DiamondAndKitePeakAtTheirNamedFractions)
	{
		for (const auto [kind, fraction] : {std::pair{PathDecorationKind::Diamond, 0.5},
			std::pair{PathDecorationKind::Kite, 1.0 / 3.0}})
		{
			const PathEndpointDecoration decoration{kind, 1.5f, 0.75f, true};
			const DecorationContour contour = BuildDecorationContour(decoration, 2.0);
			ASSERT_FALSE(contour.points.empty());
			const auto widest = std::max_element(contour.points.begin(), contour.points.end(),
				[](const DecorationContourPoint &a, const DecorationContourPoint &b) {
					return a.halfWidth < b.halfWidth;
				});
			ASSERT_NE(widest, contour.points.end());
			const double length = decoration.lengthScale * 2.0;
			EXPECT_NEAR(widest->s, length * fraction, 1e-9);
			EXPECT_NEAR(contour.points.back().halfWidth, 0.0, 1e-12);
		}
	}

	TEST(PathDecorationTests, LatexIsAClosedCurveContour)
	{
		const DecorationContour contour = BuildDecorationContour(
			{PathDecorationKind::Latex, 1.0f, 1.0f, true}, 1.0);
		ASSERT_GT(contour.points.size(), 3u);
		EXPECT_TRUE(contour.closesBack);
		ExpectFiniteAndOrdered(contour);
	}

	TEST(PathDecorationTests, LatexKeepsAFlatSweptBack)
	{
		const DecorationContour contour = BuildDecorationContour({PathDecorationKind::Latex}, 0.05);
		ASSERT_GE(contour.points.size(), 2u);
		const auto widest = std::max_element(contour.points.begin(), contour.points.end(),
			[](const DecorationContourPoint &a, const DecorationContourPoint &b) {
				return a.halfWidth < b.halfWidth;
			});
		ASSERT_NE(widest, contour.points.end());
		EXPECT_GT(contour.points.back().halfWidth, 0.0);
		EXPECT_LT(contour.points.back().halfWidth, widest->halfWidth);
	}

	TEST(PathDecorationTests, BarIsAConstantWidthStubShorterThanSquare)
	{
		constexpr double strokeWidth = 0.4;
		const PathEndpointDecoration decoration{PathDecorationKind::Bar, 1.0f, 1.0f, true};
		const DecorationContour contour = BuildDecorationContour(decoration, strokeWidth);
		const DecorationContour square = BuildDecorationContour(
			{PathDecorationKind::Square, decoration.lengthScale, decoration.widthScale, true}, strokeWidth);
		ASSERT_FALSE(contour.points.empty());
		ASSERT_GE(contour.points.size(), 2u);
		ASSERT_FALSE(square.points.empty());
		for (const DecorationContourPoint &point : contour.points)
			EXPECT_NEAR(point.halfWidth, strokeWidth, 1e-12);
		EXPECT_NEAR(contour.points.back().s, 0.3 * decoration.lengthScale * strokeWidth, 1e-12);
		EXPECT_LT(contour.points.back().s, square.points.back().s);
		EXPECT_TRUE(contour.closesBack);
	}

	TEST(PathDecorationTests, ArrowClosesButStealthRemainsOpen)
	{
		const DecorationContour arrow = BuildDecorationContour({PathDecorationKind::Arrow}, 1.0);
		const DecorationContour stealth = BuildDecorationContour({PathDecorationKind::Stealth}, 1.0);
		EXPECT_TRUE(arrow.closesBack);
		EXPECT_FALSE(stealth.closesBack);
	}

	TEST(PathDecorationTests, FilledFlagIsCopiedForEveryDecorationKind)
	{
		for (const PathDecorationKind kind : kAllKinds)
			for (const bool filled : {false, true})
			{
				const PathEndpointDecoration decoration{kind, 1.0f, 1.0f, filled};
				const DecorationContour contour = BuildDecorationContour(decoration, 1.0);
				EXPECT_EQ(contour.filled, filled) << static_cast<int>(kind) << " " << filled;
			}
	}

	TEST(PathDecorationTests, InvalidContourInputsAreEmptyAndValidContoursAreFiniteOrdered)
	{
		for (const PathDecorationKind kind : kAllKinds)
		{
			for (const PathEndpointDecoration decoration : {
				PathEndpointDecoration{kind, 0.0f, 1.0f, true},
				PathEndpointDecoration{kind, 1.0f, 0.0f, true},
				PathEndpointDecoration{kind, -1.0f, 1.0f, true},
				PathEndpointDecoration{kind, 1.0f, -1.0f, true},
				PathEndpointDecoration{kind, std::numeric_limits<float>::quiet_NaN(), 1.0f, true},
				PathEndpointDecoration{kind, 1.0f, std::numeric_limits<float>::quiet_NaN(), true},
				PathEndpointDecoration{kind, 1.0f, 1.0f, true}})
			{
				const bool valid = decoration.lengthScale > 0.0f && decoration.widthScale > 0.0f &&
					std::isfinite(decoration.lengthScale) && std::isfinite(decoration.widthScale);
				const DecorationContour contour = BuildDecorationContour(decoration, 1.0);
				if (!valid || kind == PathDecorationKind::None)
				{
					EXPECT_TRUE(contour.points.empty());
					EXPECT_DOUBLE_EQ(contour.trim, 0.0);
				}
			}

			const DecorationContour contour = BuildDecorationContour({kind, 1.0f, 1.0f, true}, 1.0);
			if (kind == PathDecorationKind::None)
			{
				EXPECT_TRUE(contour.points.empty());
				EXPECT_DOUBLE_EQ(contour.trim, 0.0);
			}
			else
			{
				ASSERT_FALSE(contour.points.empty());
				ExpectFiniteAndOrdered(contour);
				EXPECT_NEAR(contour.points.front().s, 0.0, 1e-12);
			}
		}

		const DecorationContour badWidth = BuildDecorationContour({PathDecorationKind::Arrow}, -1.0);
		EXPECT_TRUE(badWidth.points.empty());
		EXPECT_DOUBLE_EQ(badWidth.trim, 0.0);
		const DecorationContour nonFiniteWidth = BuildDecorationContour(
			{PathDecorationKind::Arrow}, std::numeric_limits<double>::quiet_NaN());
		EXPECT_TRUE(nonFiniteWidth.points.empty());
		EXPECT_DOUBLE_EQ(nonFiniteWidth.trim, 0.0);
	}

	TEST(PathDecorationTests, ContoursHaveValidDistinctLegacyLikeProperties)
	{
		const auto none = BuildDecorationContour({}, 1.0);
		EXPECT_TRUE(none.points.empty());
		EXPECT_DOUBLE_EQ(none.trim, 0.0);
		const auto arrowContour = BuildDecorationContour({PathDecorationKind::Arrow}, 2.0);
		const auto stealthContour = BuildDecorationContour({PathDecorationKind::Stealth}, 2.0);
		const auto openContour = BuildDecorationContour({PathDecorationKind::Arrow, 1.0f, 1.0f, false}, 2.0);
		const auto barContour = BuildDecorationContour({PathDecorationKind::Bar}, 2.0);
		EXPECT_TRUE(arrowContour.closesBack);
		EXPECT_FALSE(stealthContour.closesBack);
		EXPECT_FALSE(openContour.filled);
		EXPECT_LT(barContour.trim, arrowContour.trim);
		ASSERT_FALSE(barContour.points.empty());
		ASSERT_FALSE(arrowContour.points.empty());
		EXPECT_GT(barContour.points.front().halfWidth, arrowContour.points.front().halfWidth);
		for (const auto &point : arrowContour.points)
			EXPECT_GE(point.halfWidth, 0.0);
		for (const PathDecorationKind kind : kAllKinds)
			if (kind != PathDecorationKind::None)
			{
				const DecorationContour contour = BuildDecorationContour({kind}, 1.0);
				EXPECT_GE(contour.points.size(), kind == PathDecorationKind::Bar ? 2u : 3u);
			}
	}

	TEST(PathDecorationTests, TrimmedRangeUsesDecorationInsertionLength)
	{
		PathStrokeStyle style;
		style.width = 2.0f;
		style.startDecoration = {PathDecorationKind::Arrow, 1.0f, 1.0f, true};
		style.endDecoration = {PathDecorationKind::Circle, 1.0f, 1.0f, true};
		const ShaftRange range = TrimmedRange(10.0, style);
		EXPECT_NEAR(range.start, BuildDecorationContour(style.startDecoration, style.width).trim, 1e-12);
		EXPECT_NEAR(range.end, 10.0 - BuildDecorationContour(style.endDecoration, style.width).trim, 1e-12);
	}

	TEST(PathDecorationTests, TrimmedRangeCollapsesToAnEmptyNonInvertedRangeWhenDecorationsOverlap)
	{
		PathStrokeStyle style;
		style.width = 2.0f;
		style.startDecoration = {PathDecorationKind::Arrow, 1.0f, 1.0f, true};
		style.endDecoration = {PathDecorationKind::Circle, 1.0f, 1.0f, true};
		const ShaftRange range = TrimmedRange(3.0, style);
		EXPECT_TRUE(range.IsEmpty());
		EXPECT_GE(range.end, range.start);
	}
} // namespace DefectStudio::Tests
