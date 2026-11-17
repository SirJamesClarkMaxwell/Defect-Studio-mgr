#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "Domain/Vibrations/ExperimentalSpectrum.hpp"

namespace DefectStudio::Tests
{
	TEST(ExperimentalSpectrumTests, SortsDropsNonFiniteAndAveragesEqualEnergies)
	{
		const double nan = std::numeric_limits<double>::quiet_NaN();
		const std::vector<double> x = {2.2, 2.0, nan, 2.1, 2.0};
		const std::vector<double> y = {3.0, 1.0, 5.0, 2.0, 3.0};
		ExperimentalSpectrum spectrum;
		std::string error;
		ASSERT_TRUE(BuildExperimentalSpectrum(x, y, SpectrumAxisUnit::ElectronVolt, spectrum, error)) << error;
		EXPECT_EQ(spectrum.energyEv, (std::vector<double>{2.0, 2.1, 2.2}));
		EXPECT_EQ(spectrum.intensity, (std::vector<double>{2.0, 2.0, 3.0}));
		EXPECT_DOUBLE_EQ(ToElectronVolt(1.945, SpectrumAxisUnit::ElectronVolt), 1.945);
	}

	TEST(ExperimentalSpectrumTests, RejectsMismatchedTooShortOrNonPositive)
	{
		ExperimentalSpectrum spectrum;
		spectrum.energyEv = {9.0};
		std::string error;
		EXPECT_FALSE(BuildExperimentalSpectrum(std::vector<double>{1.0, 2.0}, std::vector<double>{1.0}, SpectrumAxisUnit::ElectronVolt, spectrum, error));
		EXPECT_FALSE(error.empty());
		EXPECT_FALSE(BuildExperimentalSpectrum(std::vector<double>{1.0}, std::vector<double>{1.0}, SpectrumAxisUnit::ElectronVolt, spectrum, error));
		EXPECT_FALSE(BuildExperimentalSpectrum(std::vector<double>{1.0, 2.0}, std::vector<double>{0.0, -1.0}, SpectrumAxisUnit::ElectronVolt, spectrum, error));
		EXPECT_EQ(spectrum.energyEv, (std::vector<double>{9.0}));
	}

	TEST(ExperimentalSpectrumTests, PeakInsideTheWindowOnly)
	{
		ExperimentalSpectrum spectrum{{1.8, 1.9, 1.95, 2.0}, {10.0, 1.0, 4.0, 2.0}};
		EXPECT_DOUBLE_EQ(SpectrumPeakEnergyEv(spectrum, 1.85, 2.1), 1.95);
		EXPECT_DOUBLE_EQ(SpectrumPeakEnergyEv(spectrum, 0.0, 9.0), 1.8);
		EXPECT_TRUE(std::isnan(SpectrumPeakEnergyEv(spectrum, 3.0, 4.0)));
	}
} // namespace DefectStudio::Tests
