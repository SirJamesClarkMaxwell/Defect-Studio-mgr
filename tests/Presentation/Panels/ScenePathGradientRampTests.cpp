#include "Core/dspch.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "Presentation/Panels/ScenePathGradientRamp.hpp"
#include "Renderer/Path/PathStrokeMesher.hpp"

namespace DefectStudio::Tests
{
	void ExpectColorNear(const glm::vec4 &actual, const glm::vec4 &expected)
	{
		constexpr float kTolerance = 1e-6f;
		EXPECT_NEAR(actual.r, expected.r, kTolerance);
		EXPECT_NEAR(actual.g, expected.g, kTolerance);
		EXPECT_NEAR(actual.b, expected.b, kTolerance);
		EXPECT_NEAR(actual.a, expected.a, kTolerance);
	}

	TEST(ScenePathGradientRampTests, SampleGradientAtMatchesStrokeSamplerAtAndBeyondStops)
	{
		const PathGradient gradient{true, {
			{0.2f, glm::vec3(0.1f, 0.3f, 0.8f), 0.25f},
			{0.7f, glm::vec3(0.9f, 0.4f, 0.2f), 0.75f},
			{0.9f, glm::vec3(0.2f, 0.8f, 0.5f), 0.6f}}};
		PathStrokeStyle style;
		style.gradient = gradient;

		const std::array<double, 6> positions = {-0.4, 0.2, 0.35, 0.7, 0.9, 1.4};
		for (const double position : positions)
			ExpectColorNear(SampleGradientAt(gradient, static_cast<float>(position)), SampleStrokeColor(style, position));
	}

	TEST(ScenePathGradientRampTests, EmptyOrDisabledGradientSamplesAsOpaqueWhite)
	{
		const std::array<PathGradient, 3> gradients = {
			PathGradient{false, {}},
			PathGradient{true, {}},
			PathGradient{false, {{0.25f, glm::vec3(0.2f, 0.4f, 0.6f), 0.5f}}}};
		for (const PathGradient &gradient : gradients)
			EXPECT_EQ(SampleGradientAt(gradient, 0.37f), glm::vec4(1.0f));
	}

	TEST(ScenePathGradientRampTests, InsertionUsesWidestGapAndPreservesItsSampledColor)
	{
		PathGradient gradient{true, {
			{0.1f, glm::vec3(1.0f, 0.0f, 0.0f), 0.2f},
			{0.35f, glm::vec3(0.0f, 1.0f, 0.0f), 0.5f},
			{0.9f, glm::vec3(0.0f, 0.0f, 1.0f), 0.8f}}};
		std::vector<float> boundaries{0.0f};
		for (const PathGradientStop &stop : gradient.stops)
			boundaries.push_back(stop.position);
		boundaries.push_back(1.0f);
		ASSERT_GE(boundaries.size(), 2u);

		float lower = boundaries.front();
		float upper = boundaries.back();
		float widestWidth = -1.0f;
		auto lowerBoundary = boundaries.begin();
		auto upperBoundary = lowerBoundary + 1;
		for (; upperBoundary != boundaries.end(); ++lowerBoundary, ++upperBoundary)
		{
			const float width = *upperBoundary - *lowerBoundary;
			if (width > widestWidth)
			{
				lower = *lowerBoundary;
				upper = *upperBoundary;
				widestWidth = width;
			}
		}

		const float expectedPosition = lower + (upper - lower) * 0.5f;
		const glm::vec4 expectedColor = SampleGradientAt(gradient, expectedPosition);
		const std::size_t insertedIndex = InsertGradientStopInWidestGap(gradient);
		ASSERT_LT(insertedIndex, gradient.stops.size());
		const PathGradientStop &inserted = gradient.stops.at(insertedIndex);

		EXPECT_FLOAT_EQ(inserted.position, expectedPosition);
		ExpectColorNear(glm::vec4(inserted.color, inserted.alpha), expectedColor);
	}

	TEST(ScenePathGradientRampTests, EmptyGradientInsertionCreatesStopAtUnitIntervalMidpoint)
	{
		PathGradient gradient;
		const glm::vec4 expectedColor = SampleGradientAt(gradient, 0.5f);
		const std::size_t insertedIndex = InsertGradientStopInWidestGap(gradient);

		ASSERT_EQ(gradient.stops.size(), 1u);
		ASSERT_LT(insertedIndex, gradient.stops.size());
		const PathGradientStop &inserted = gradient.stops.at(insertedIndex);
		const float expectedPosition = (0.0f + 1.0f) * 0.5f;
		EXPECT_FLOAT_EQ(inserted.position, expectedPosition);
		ExpectColorNear(glm::vec4(inserted.color, inserted.alpha), expectedColor);
	}

	TEST(ScenePathGradientRampTests, GradientStopInsertionLeavesStopsFiniteSortedAndBounded)
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		PathGradient gradient{true, {
			{1.2f, glm::vec3(nan, 0.2f, 0.3f), nan},
			{nan, glm::vec3(0.4f, nan, 0.6f), 0.7f},
			{-0.3f, glm::vec3(0.7f, 0.8f, nan), 0.9f},
			{0.65f, glm::vec3(0.1f, 0.2f, 0.3f), 0.4f}}};
		const std::size_t insertedIndex = InsertGradientStopInWidestGap(gradient);

		ASSERT_FALSE(gradient.stops.empty());
		ASSERT_LT(insertedIndex, gradient.stops.size());
		float previousPosition = -std::numeric_limits<float>::infinity();
		for (const PathGradientStop &stop : gradient.stops)
		{
			EXPECT_TRUE(std::isfinite(stop.position));
			EXPECT_GE(stop.position, 0.0f);
			EXPECT_LE(stop.position, 1.0f);
			EXPECT_GE(stop.position, previousPosition);
			EXPECT_TRUE(std::isfinite(stop.color.x));
			EXPECT_TRUE(std::isfinite(stop.color.y));
			EXPECT_TRUE(std::isfinite(stop.color.z));
			EXPECT_TRUE(std::isfinite(stop.alpha));
			previousPosition = stop.position;
		}
	}

	TEST(ScenePathGradientRampTests, GradientStopInsertionPreservesCoincidentStops)
	{
		const float hardEdgePosition = 0.4f;
		const PathGradientStop first{hardEdgePosition, glm::vec3(1.0f, 0.0f, 0.0f), 0.2f};
		const PathGradientStop second{hardEdgePosition, glm::vec3(0.0f, 0.0f, 1.0f), 0.8f};
		PathGradient gradient{true, {first, second}};

		const std::size_t insertedIndex = InsertGradientStopInWidestGap(gradient);
		ASSERT_EQ(gradient.stops.size(), 3u);
		ASSERT_LT(insertedIndex, gradient.stops.size());

		std::size_t coincidentCount = 0;
		bool firstSurvived = false;
		bool secondSurvived = false;
		for (const PathGradientStop &stop : gradient.stops)
		{
			if (stop.position != hardEdgePosition)
				continue;
			++coincidentCount;
			firstSurvived = firstSurvived || (stop.color == first.color && stop.alpha == first.alpha);
			secondSurvived = secondSurvived || (stop.color == second.color && stop.alpha == second.alpha);
		}

		EXPECT_EQ(coincidentCount, 2u);
		EXPECT_TRUE(firstSurvived);
		EXPECT_TRUE(secondSurvived);
	}
}
