#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "Domain/Electronic/DensityAnalysis.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		constexpr int kPoints = 40;
		constexpr float kCellLength = 10.0f; // 0.25 A spacing

		// Periodic Gaussian of width `sigma` (A) at fractional `centre`, scaled by `amplitude`, summed
		// over the 27 nearest images so a blob at a cell face is genuinely split across it.
		void AddBlob(OrbitalGridData &grid, const glm::vec3 &centre, const float sigma, const float amplitude)
		{
			for (int i = 0; i < kPoints; ++i)
				for (int j = 0; j < kPoints; ++j)
					for (int k = 0; k < kPoints; ++k)
					{
						const glm::vec3 fractional = glm::vec3(i, j, k) / static_cast<float>(kPoints);
						float value = 0.0f;
						for (int a = -1; a <= 1; ++a)
							for (int b = -1; b <= 1; ++b)
								for (int c = -1; c <= 1; ++c)
								{
									const glm::vec3 d = (fractional - centre + glm::vec3(a, b, c)) * kCellLength;
									value += std::exp(-glm::dot(d, d) / (2.0f * sigma * sigma));
								}
						grid.values[(i * kPoints + j) * kPoints + k] += amplitude * value;
					}
		}

		[[nodiscard]] OrbitalGridData EmptyGrid()
		{
			OrbitalGridData grid;
			grid.dimensions = glm::ivec3(kPoints);
			grid.cell = glm::mat3(kCellLength);
			grid.values.assign(static_cast<std::size_t>(kPoints) * kPoints * kPoints, 0.0f);
			return grid;
		}

		// Fraction of a 3D Gaussian's mass within radius r.
		[[nodiscard]] float GaussianFractionWithin(const float r, const float sigma)
		{
			const float x = r / sigma;
			return std::erf(x / std::numbers::sqrt2_v<float>) -
				std::sqrt(2.0f / std::numbers::pi_v<float>) * x * std::exp(-0.5f * x * x);
		}
	} // namespace

	TEST(DensityAnalysisTests, CentroidFindsABlobAwayFromTheMiddle)
	{
		OrbitalGridData grid = EmptyGrid();
		AddBlob(grid, {0.3f, 0.6f, 0.45f}, 0.8f, 1.0f);
		const std::optional<DensityCentre> centre = AbsoluteDensityCentroid(grid);
		ASSERT_TRUE(centre.has_value());
		EXPECT_NEAR(glm::distance(centre->cartesian, glm::vec3(3.0f, 6.0f, 4.5f)), 0.0f, 0.02f);
		EXPECT_NEAR(glm::distance(centre->fractional, glm::vec3(0.3f, 0.6f, 0.45f)), 0.0f, 0.002f);
	}

	TEST(DensityAnalysisTests, ABlobSplitAcrossACellFaceIsNotAveragedIntoTheMiddle)
	{
		OrbitalGridData grid = EmptyGrid();
		AddBlob(grid, {0.01f, 0.5f, 0.98f}, 0.8f, 1.0f);
		const std::optional<DensityCentre> centre = AbsoluteDensityCentroid(grid);
		ASSERT_TRUE(centre.has_value());
		// Fractional components stay in [0, 1): 0.01 and 0.98, not 0.5.
		EXPECT_NEAR(centre->fractional.x, 0.01f, 0.003f);
		EXPECT_NEAR(centre->fractional.y, 0.5f, 0.003f);
		EXPECT_NEAR(centre->fractional.z, 0.98f, 0.003f);
		EXPECT_GE(centre->fractional.x, 0.0f);
		EXPECT_LT(centre->fractional.z, 1.0f);
	}

	TEST(DensityAnalysisTests, TheCentroidWeighsMagnitudeSoANegativeLobeCountsToo)
	{
		OrbitalGridData grid = EmptyGrid();
		AddBlob(grid, {0.4f, 0.5f, 0.5f}, 0.7f, 1.0f);
		AddBlob(grid, {0.6f, 0.5f, 0.5f}, 0.7f, -1.0f);
		const std::optional<DensityCentre> centre = AbsoluteDensityCentroid(grid);
		ASSERT_TRUE(centre.has_value());
		EXPECT_NEAR(centre->fractional.x, 0.5f, 0.003f);
	}

	TEST(DensityAnalysisTests, OriginShiftsTheCartesianCentreOnly)
	{
		OrbitalGridData grid = EmptyGrid();
		grid.origin = glm::vec3(1.0f, -2.0f, 0.5f);
		AddBlob(grid, {0.3f, 0.6f, 0.45f}, 0.8f, 1.0f);
		const std::optional<DensityCentre> centre = AbsoluteDensityCentroid(grid);
		ASSERT_TRUE(centre.has_value());
		EXPECT_NEAR(glm::distance(centre->cartesian, glm::vec3(4.0f, 4.0f, 5.0f)), 0.0f, 0.02f);
		EXPECT_NEAR(centre->fractional.x, 0.3f, 0.002f);
	}

	TEST(DensityAnalysisTests, NothingToFindGivesNoCentre)
	{
		EXPECT_FALSE(AbsoluteDensityCentroid(EmptyGrid()).has_value());
		EXPECT_FALSE(AbsoluteDensityCentroid(OrbitalGridData{}).has_value());
	}

	TEST(DensityAnalysisTests, RadialProfileOfAGaussianFollowsItsAnalyticMass)
	{
		constexpr float sigma = 0.8f;
		OrbitalGridData grid = EmptyGrid();
		AddBlob(grid, {0.5f, 0.5f, 0.5f}, sigma, 1.0f);
		const DensityRadialProfile profile = AbsoluteDensityRadialProfile(grid, glm::vec3(5.0f));

		// Total = sum |v| * voxel volume = (2 pi sigma^2)^(3/2) for a unit-amplitude Gaussian.
		const double expectedTotal = std::pow(2.0 * std::numbers::pi * sigma * sigma, 1.5);
		EXPECT_NEAR(profile.total, expectedTotal, 0.01 * expectedTotal);
		ASSERT_FALSE(profile.cumulative.empty());
		EXPECT_DOUBLE_EQ(profile.cumulative.back(), profile.total);

		for (const float r : {sigma, 1.5f * sigma, 2.0f * sigma, 3.0f * sigma})
			EXPECT_NEAR(profile.FractionWithin(r), GaussianFractionWithin(r, sigma), 0.03f) << "r = " << r;
		EXPECT_FLOAT_EQ(profile.FractionWithin(100.0f), 1.0f);
		EXPECT_FLOAT_EQ(profile.FractionWithin(0.0f), 0.0f);
	}

	TEST(DensityAnalysisTests, RadiusHoldingInvertsFractionWithin)
	{
		OrbitalGridData grid = EmptyGrid();
		AddBlob(grid, {0.5f, 0.5f, 0.5f}, 0.8f, 1.0f);
		const DensityRadialProfile profile = AbsoluteDensityRadialProfile(grid, glm::vec3(5.0f));
		for (const float r : {0.6f, 1.0f, 1.7f, 2.4f})
			EXPECT_NEAR(profile.RadiusHolding(profile.FractionWithin(r)), r, profile.binWidth) << "r = " << r;
		EXPECT_FLOAT_EQ(DensityRadialProfile{}.FractionWithin(1.0f), 0.0f);
		EXPECT_FLOAT_EQ(DensityRadialProfile{}.RadiusHolding(0.5f), 0.0f);
	}

	TEST(DensityAnalysisTests, TheProfileUsesTheMinimumImageAcrossACellFace)
	{
		OrbitalGridData grid = EmptyGrid();
		AddBlob(grid, {0.01f, 0.5f, 0.98f}, 0.8f, 1.0f);
		const DensityRadialProfile profile = AbsoluteDensityRadialProfile(grid, glm::vec3(0.1f, 5.0f, 9.8f));
		EXPECT_GT(profile.FractionWithin(3.0f * 0.8f), 0.95f);
	}

	TEST(DensityAnalysisTests, TwoEqualBlobsSplitTheMassAndTheWarningCatchesIt)
	{
		// The delocalised case in miniature: half the density is 4 A away from the centre.
		OrbitalGridData grid = EmptyGrid();
		AddBlob(grid, {0.3f, 0.5f, 0.5f}, 0.6f, 1.0f);
		AddBlob(grid, {0.7f, 0.5f, 0.5f}, 0.6f, 1.0f);
		const DensityRadialProfile profile = AbsoluteDensityRadialProfile(grid, glm::vec3(3.0f, 5.0f, 5.0f));
		const float fraction = profile.FractionWithin(2.0f);
		EXPECT_NEAR(fraction, 0.5f, 0.03f);
		EXPECT_LT(fraction, kLocalisedFractionWarning + 0.03f);
	}
} // namespace DefectStudio::Tests
