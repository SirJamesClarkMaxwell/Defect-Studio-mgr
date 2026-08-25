#include <gtest/gtest.h>

#include "Domain/Electronic/ElectronicStructureModel.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] OrbitalRecord MakeRecord(
			int band,
			float upEnergy, float upOccupation, float upLocalization,
			float downEnergy, float downOccupation, float downLocalization)
		{
			OrbitalRecord record;
			record.band = band;
			record.up = OrbitalChannelData{upEnergy, upOccupation, upLocalization, std::nullopt};
			record.down = OrbitalChannelData{downEnergy, downOccupation, downLocalization, std::nullopt};
			return record;
		}
	} // namespace

	TEST(ElectronicStructureModelTests, FilterByLocalizationThresholdKeepsRecordAboveEitherChannel)
	{
		const std::vector<OrbitalRecord> orbitals = {
			MakeRecord(0, 0.0f, 1.0f, 0.9f, 0.0f, 1.0f, 0.1f),  // up localized, down not
			MakeRecord(1, 0.5f, 1.0f, 0.2f, 0.5f, 1.0f, 0.3f),  // neither localized
			MakeRecord(2, 1.0f, 0.0f, 0.1f, 1.0f, 0.0f, 0.95f)}; // down localized, up not

		const std::vector<OrbitalRecord> filtered =
			FilterByLocalizationThreshold(orbitals, LocalizationThresholdSettings{0.5f});

		ASSERT_EQ(filtered.size(), 2u);
		EXPECT_EQ(filtered[0].band, 0);
		EXPECT_EQ(filtered[1].band, 2);
	}

	TEST(ElectronicStructureModelTests, ClassifySpinMultiplicityReturnsUnknownForEmptyWindow)
	{
		EXPECT_EQ(ClassifySpinMultiplicity({}), SpinMultiplicity::Unknown);
	}

	TEST(ElectronicStructureModelTests, ClassifySpinMultiplicityReturnsSingletForPairedOccupation)
	{
		const std::vector<OrbitalRecord> window = {
			MakeRecord(0, -0.1f, 1.0f, 0.8f, -0.1f, 1.0f, 0.8f),
			MakeRecord(1, 1.4f, 1.0f, 0.7f, 1.4f, 1.0f, 0.7f)};

		EXPECT_EQ(ClassifySpinMultiplicity(window), SpinMultiplicity::Singlet);
	}

	TEST(ElectronicStructureModelTests, ClassifySpinMultiplicityReturnsSingletForSingleChannelPopulated)
	{
		// Non-spin-polarized calculation (or puntukas leaving the down channel at zero) - only
		// one channel carries real occupation. Still closed-shell, not an unpaired-spin signal.
		const std::vector<OrbitalRecord> window = {
			MakeRecord(0, -0.1f, 1.0f, 0.8f, -0.1f, 0.0f, 0.0f),
			MakeRecord(1, 1.4f, 1.0f, 0.7f, 1.4f, 0.0f, 0.0f)};

		EXPECT_EQ(ClassifySpinMultiplicity(window), SpinMultiplicity::Singlet);
	}

	TEST(ElectronicStructureModelTests, ClassifySpinMultiplicityReturnsTripletForUnpairedOccupation)
	{
		// One channel has an extra occupied mid-gap level the other channel lacks - net spin
		// polarization within the window, matching the exc_ms/triplet screenshot (single arrow
		// per gap level, asymmetric between up/down).
		const std::vector<OrbitalRecord> window = {
			MakeRecord(0, -0.1f, 1.0f, 0.8f, -0.1f, 1.0f, 0.8f),
			MakeRecord(1, 0.2f, 1.0f, 0.6f, 0.2f, 0.0f, 0.6f)};

		EXPECT_EQ(ClassifySpinMultiplicity(window), SpinMultiplicity::Triplet);
	}

	TEST(ElectronicStructureModelTests, UpsampleOrbitalGridPreservesCornersAndInterpolatesMidpoints)
	{
		// 2x2x2 grid, values = x index (0 or 1) at every y/z - a linear ramp along x only, so the
		// expected result at any upsampled point is trivial to reason about by hand.
		OrbitalGridData grid;
		grid.dimensions = glm::ivec3(2, 2, 2);
		grid.cell = glm::mat3(1.0f);
		grid.values = {
			0.0f, 0.0f, 0.0f, 0.0f, // x=0 plane (y0z0, y0z1, y1z0, y1z1)
			1.0f, 1.0f, 1.0f, 1.0f, // x=1 plane
		};

		const OrbitalGridData upsampled = UpsampleOrbitalGrid(grid, 2);

		ASSERT_EQ(upsampled.dimensions, glm::ivec3(3, 3, 3));
		ASSERT_EQ(upsampled.values.size(), 27u);

		const auto at = [&](int i, int j, int k)
		{
			return upsampled.values[(static_cast<std::size_t>(i) * 3 + j) * 3 + k];
		};
		EXPECT_FLOAT_EQ(at(0, 0, 0), 0.0f);   // original corner, preserved exactly
		EXPECT_FLOAT_EQ(at(2, 1, 1), 1.0f);   // original corner, preserved exactly
		EXPECT_FLOAT_EQ(at(1, 0, 0), 0.5f);   // new midpoint along the ramp axis
		EXPECT_FLOAT_EQ(at(1, 2, 2), 0.5f);   // midpoint regardless of y/z (value is x-only)
	}

	TEST(ElectronicStructureModelTests, UpsampleOrbitalGridReturnsInputUnchangedForFactorOne)
	{
		OrbitalGridData grid;
		grid.dimensions = glm::ivec3(2, 2, 2);
		grid.values = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};

		const OrbitalGridData result = UpsampleOrbitalGrid(grid, 1);

		EXPECT_EQ(result.dimensions, grid.dimensions);
		EXPECT_EQ(result.values, grid.values);
	}
} // namespace DefectStudio::Tests
