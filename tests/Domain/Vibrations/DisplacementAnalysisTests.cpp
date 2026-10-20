#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "Domain/Vibrations/DisplacementAnalysis.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		CcdDisplacement Displacement()
		{
			CcdDisplacement result;
			result.symbols = {"B", "N", "N"}; result.masses = {2, 1, 1};
			result.cell = {glm::dvec3(10, 0, 0), glm::dvec3(0, 10, 0), glm::dvec3(0, 0, 10)};
			result.gsPositions = {{0.1, 0, 0}, {9.9, 0, 0}, {4, 0, 0}};
			result.displacement = {{2, 0, 0}, {1, 0, 0}, {1, 0, 0}};
			result.deltaQ = std::sqrt(10.0);
			return result;
		}
	}

	TEST(DisplacementAnalysisTests, CentreUsesLargestWeightAnchorAcrossPeriodicBoundary)
	{
		auto displacement = Displacement();
		displacement.gsPositions[2] = {20.1, 0, 0};
		const auto centre = DisplacementCentre(displacement);
		ASSERT_TRUE(centre);
		EXPECT_NEAR(centre->x, 0.08, 1e-12);
		EXPECT_DOUBLE_EQ(centre->y, 0); EXPECT_DOUBLE_EQ(centre->z, 0);
		const auto curve = LocalisationCurve(displacement);
		ASSERT_EQ(curve.size(), 3u);
		EXPECT_NEAR(curve.back().first, 0.18, 1e-12);
		EXPECT_DOUBLE_EQ(curve.back().second, 1);
		for (std::size_t i = 1; i < curve.size(); ++i)
		{
			EXPECT_LE(curve[i - 1].first, curve[i].first);
			EXPECT_LE(curve[i - 1].second, curve[i].second);
		}
	}

	TEST(DisplacementAnalysisTests, SkewedCellUsesCartesianNearestImageRatherThanFractionalWrapping)
	{
		auto displacement = Displacement();
		displacement.symbols = {"B", "B"}; displacement.masses = {1, 1};
		displacement.displacement = {{1, 0, 0}, {1, 0, 0}};
		displacement.gsPositions = {{0, 0, 0}, {0.85, 0.27, 0}};
		displacement.cell = {glm::dvec3(1, 0, 0), glm::dvec3(0.8, 0.6, 0), glm::dvec3(0, 0, 1)};
		const auto centre = DisplacementCentre(displacement);
		ASSERT_TRUE(centre);
		EXPECT_NEAR(centre->x, 0.925, 1e-12); EXPECT_NEAR(centre->y, 0.135, 1e-12);
		const auto curve = LocalisationCurve(displacement);
		ASSERT_EQ(curve.size(), 2u);
		EXPECT_NEAR(curve[0].first, std::hypot(0.15, 0.27) / 2, 1e-12);
		EXPECT_DOUBLE_EQ(curve[0].second, 0.5); EXPECT_DOUBLE_EQ(curve[1].second, 1);
	}

	TEST(DisplacementAnalysisTests, CarryingCountSortsMassWeightedSharesAndUsesRequestedFraction)
	{
		const auto displacement = Displacement();
		EXPECT_EQ(AtomsCarrying(displacement, 0.8), 1u);
		EXPECT_EQ(AtomsCarrying(displacement, 0.9), 2u);
		EXPECT_EQ(AtomsCarrying(displacement, 1.0), 3u);
		EXPECT_EQ(AtomsCarrying(displacement, 0), 0u);
		EXPECT_EQ(AtomsCarrying(displacement, 1.1), 0u);
		EXPECT_EQ(AtomsCarrying(displacement, std::numeric_limits<double>::quiet_NaN()), 0u);
	}

	TEST(DisplacementAnalysisTests, OverlapIsAbsoluteNormalisedAndInvariantUnderScaling)
	{
		const auto a = Displacement();
		ASSERT_TRUE(DisplacementOverlap(a, a)); EXPECT_NEAR(*DisplacementOverlap(a, a), 1, 1e-12);
		auto b = a;
		for (auto &vector : b.displacement) vector = {0, vector.x, 0};
		ASSERT_TRUE(DisplacementOverlap(a, b)); EXPECT_DOUBLE_EQ(*DisplacementOverlap(a, b), 0);
		b = a;
		for (auto &vector : b.displacement) vector *= -3.0;
		b.deltaQ *= 3;
		ASSERT_TRUE(DisplacementOverlap(a, b)); EXPECT_NEAR(*DisplacementOverlap(a, b), 1, 1e-12);
		b = a; b.displacement[2] = {0, 1, 0};
		ASSERT_TRUE(DisplacementOverlap(a, b)); EXPECT_NEAR(*DisplacementOverlap(a, b), 0.9, 1e-12);
	}

	TEST(DisplacementAnalysisTests, OverlapRejectsDifferentCountsOrderAndMasses)
	{
		const auto a = Displacement();
		auto b = a; b.symbols.pop_back();
		EXPECT_FALSE(DisplacementOverlap(a, b));
		b = a; std::swap(b.symbols[0], b.symbols[1]);
		EXPECT_FALSE(DisplacementOverlap(a, b));
		b = a; b.masses[0] += 0.002;
		EXPECT_FALSE(DisplacementOverlap(a, b));
		b = a; b.masses[0] += 0.0005;
		EXPECT_TRUE(DisplacementOverlap(a, b));
	}

	TEST(DisplacementAnalysisTests, EmptyZeroMalformedAndNonfiniteInputsHaveNoAnalysis)
	{
		EXPECT_FALSE(DisplacementCentre({})); EXPECT_TRUE(LocalisationCurve({}).empty());
		EXPECT_EQ(AtomsCarrying({}, 0.9), 0u); EXPECT_FALSE(DisplacementOverlap({}, {}));
		auto displacement = Displacement(); displacement.displacement.assign(3, glm::dvec3(0)); displacement.deltaQ = 0;
		EXPECT_FALSE(DisplacementCentre(displacement)); EXPECT_TRUE(LocalisationCurve(displacement).empty());
		EXPECT_EQ(AtomsCarrying(displacement, 0.9), 0u); EXPECT_FALSE(DisplacementOverlap(displacement, displacement));
		displacement = Displacement(); displacement.masses.pop_back();
		EXPECT_FALSE(DisplacementCentre(displacement)); EXPECT_EQ(AtomsCarrying(displacement, 0.9), 0u);
		displacement = Displacement(); displacement.cell[2] = displacement.cell[0];
		EXPECT_FALSE(DisplacementCentre(displacement)); EXPECT_TRUE(LocalisationCurve(displacement).empty());
		displacement = Displacement(); displacement.gsPositions[0].x = std::numeric_limits<double>::infinity();
		EXPECT_FALSE(DisplacementCentre(displacement));
		displacement = Displacement(); displacement.displacement[0].x = std::numeric_limits<double>::quiet_NaN();
		EXPECT_EQ(AtomsCarrying(displacement, 0.9), 0u); EXPECT_FALSE(DisplacementOverlap(displacement, displacement));
	}
}
