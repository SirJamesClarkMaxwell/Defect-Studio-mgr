#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "Domain/Defects/DefectConcentrations.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// Independent SI expression for the parabolic-band density in cm^-3, including spin.
		double EffectiveDensity(double mass, double temperatureK)
		{
			return 2.0e-6 * std::pow(2.0 * std::numbers::pi * mass * 9.1093837139e-31 *
				1.380649e-23 * temperatureK / std::pow(6.62607015e-34, 2), 1.5);
		}

		// Independently checks the dimensional neutrality equation, including multicharged defects.
		void ExpectNeutral(const DefectConcentrationSolution &solution)
		{
			ASSERT_TRUE(solution.converged) << solution.error;
			double sum = solution.holesCm3 - solution.electronsCm3 + solution.fixedChargeCm3;
			double largest = std::max({solution.holesCm3, solution.electronsCm3, std::abs(solution.fixedChargeCm3)});
			for (const auto &state : solution.chargeStates)
			{
				const double charge = state.charge * state.concentrationCm3;
				sum += charge;
				largest = std::max(largest, std::abs(charge));
			}
			ASSERT_GT(largest, 0.0);
			EXPECT_LT(std::abs(sum) / largest, 1e-8);
			EXPECT_LE(std::abs(solution.relativeNeutralityResidual), 1e-10);
		}
	} // namespace

	TEST(DefectConcentrationsTests, IntrinsicLimitHasTheMassShiftAndIntrinsicDensity)
	{
		constexpr double gap = 1.2;
		constexpr double temperature = 300.0;
		const EffectiveMassBands bands{0.5, 2.0};
		const auto solution = SolveDefectConcentrations({}, bands, gap, temperature);
		ExpectNeutral(solution);
		const double kT = kBoltzmannEvPerK * temperature;
		const double intrinsicFermi = gap / 2.0 + 0.75 * kT * std::log(bands.holeDosMass / bands.electronDosMass);
		const double ni = std::sqrt(EffectiveDensity(bands.electronDosMass, temperature) *
			EffectiveDensity(bands.holeDosMass, temperature)) * std::exp(-gap / (2.0 * kT));
		EXPECT_NEAR(solution.fermiLevelEv, intrinsicFermi, 1e-10);
		EXPECT_NEAR(solution.electronsCm3 / ni, 1.0, 1e-8);
		EXPECT_NEAR(solution.holesCm3 / ni, 1.0, 1e-8);
		EXPECT_TRUE(solution.chargeStates.empty());
	}

	TEST(DefectConcentrationsTests, ShallowEquilibriumDonorMatchesTheBoltzmannAnalyticSolution)
	{
		constexpr double gap = 1.2;
		constexpr double temperature = 200.0;
		constexpr double donorDensity = 1e14;
		constexpr double sites = 1e22;
		const EffectiveMassBands bands{0.5, 0.5};
		const double kT = kBoltzmannEvPerK * temperature;
		const double nc = EffectiveDensity(bands.electronDosMass, temperature);
		const double expectedFermi = gap + kT * std::log(donorDensity / nc);
		const double intercept = kT * std::log(sites / donorDensity) - expectedFermi;
		// (+/0) is 5 meV below CBM, g_0/g_+ = 2. Choose mu (A_+) so n_B = N_D.
		const std::array defects{ConcentrationDefect{"D", sites,
			{{1, intercept, 1.0}, {0, intercept + gap - 0.005, 2.0}}}};
		const auto solution = SolveDefectConcentrations(defects, bands, gap, temperature);
		ExpectNeutral(solution);
		// With p negligible: n_B^2 = N_sites*N_c*exp(-(A_+ + gap)/kT).
		const double analyticN = std::sqrt(sites * nc * std::exp(-(intercept + gap) / kT));
		EXPECT_NEAR(solution.electronsCm3 / analyticN, 1.0, 1e-5); // small Fermi-Dirac correction
		EXPECT_NEAR(solution.electronsCm3 / donorDensity, 1.0, 1e-5);
		ASSERT_EQ(solution.chargeStates.size(), 2u);
		ASSERT_EQ(solution.defectTotalsCm3.size(), 1u);
		EXPECT_LT(solution.chargeStates[1].concentrationCm3 / solution.defectTotalsCm3[0], 1e-3);
		EXPECT_NEAR(solution.chargeStates[0].concentrationCm3, solution.electronsCm3, donorDensity * 1e-8);
		// A supplied, fully ionized shallow donor population is also represented by N_fixed.
		const auto fixed = SolveDefectConcentrations({}, bands, gap, temperature, donorDensity);
		ExpectNeutral(fixed);
		EXPECT_NEAR(fixed.electronsCm3 / donorDensity, 1.0, 1e-8);
	}

	TEST(DefectConcentrationsTests, MultichargedDefectsAndEitherSignOfDopingRemainNeutral)
	{
		const std::array defects{ConcentrationDefect{"D", 1e22, {{2, 0.2, 3.0}, {0, 1.0, 1.0}}},
			ConcentrationDefect{"A", 2e22, {{-1, 1.2, 2.0}}}};
		for (double fixed : {-1e17, 0.0, 1e17})
		{
			const auto solution = SolveDefectConcentrations(defects, EffectiveMassBands{0.5, 1.0}, 1.5, 800.0, fixed);
			ExpectNeutral(solution);
			ASSERT_EQ(solution.defectTotalsCm3.size(), 2u);
			EXPECT_NEAR(solution.defectTotalsCm3[0] /
				(solution.chargeStates[0].concentrationCm3 + solution.chargeStates[1].concentrationCm3), 1.0, 1e-12);
		}
	}

	TEST(DefectConcentrationsTests, NeutralDefectDegeneracyAndSitesScaleConcentrationLinearly)
	{
		const std::array defects{ConcentrationDefect{"base", 1e22, {{0, 0.4, 1.0}}},
			ConcentrationDefect{"spin", 1e22, {{0, 0.4, 4.0}}},
			ConcentrationDefect{"sites", 3e22, {{0, 0.4, 1.0}}}};
		const auto solution = SolveDefectConcentrations(defects, EffectiveMassBands{1.0, 1.0}, 1.0, 500.0);
		ExpectNeutral(solution);
		ASSERT_EQ(solution.chargeStates.size(), 3u);
		ASSERT_EQ(solution.defectTotalsCm3.size(), 3u);
		const double expected = 1e22 * std::exp(-0.4 / (kBoltzmannEvPerK * 500.0));
		EXPECT_NEAR(solution.defectTotalsCm3[0] / expected, 1.0, 1e-12);
		EXPECT_NEAR(solution.defectTotalsCm3[1] / expected, 4.0, 1e-12);
		EXPECT_NEAR(solution.defectTotalsCm3[2] / expected, 3.0, 1e-12);
	}

	TEST(DefectConcentrationsTests, CarrierFermiIntegralIncludesBandEdgeAndDegenerateLimits)
	{
		const EffectiveMassBands bands{1.0, 1.0};
		const double nc = EffectiveDensity(1.0, 300.0);
		// Normalized F_1/2(0) = (1 - 2^-1/2)*zeta(3/2), rather than the Boltzmann value 1.
		const auto edge = SolveDefectConcentrations({}, bands, 1.2, 300.0, nc * 0.765147024625408);
		ExpectNeutral(edge);
		EXPECT_NEAR(edge.fermiLevelEv, 1.2, 1e-10);
		// At 1 K the zero-temperature parabolic-band formula is an independent Sommerfeld check.
		const auto degenerate = SolveDefectConcentrations({}, bands, 1.2, 1.0, 1e23);
		ExpectNeutral(degenerate);
		EXPECT_GT(degenerate.fermiLevelEv, 2.2); // needs expansion beyond the initial bracket
		const double excess = kBoltzmannEvPerK * std::pow(1e23 / EffectiveDensity(1.0, 1.0) *
			3.0 * std::sqrt(std::numbers::pi) / 4.0, 2.0 / 3.0);
		EXPECT_NEAR((degenerate.fermiLevelEv - 1.2) / excess, 1.0, 1e-8);
	}

	TEST(DefectConcentrationsTests, TabulatedDosMatchesFiniteBandIntegralsEvenOnACoarseGrid)
	{
		constexpr double dos = 1e20;
		const TabulatedDosBands bands{{{-2.0, dos}, {0.0, dos}}, {{1.0, dos}, {3.0, dos}}};
		for (double temperature : {1.0, 300.0, 3000.0})
		{
			const double kT = kBoltzmannEvPerK * temperature;
			const auto intrinsic = SolveDefectConcentrations({}, bands, 1.0, temperature);
			ASSERT_TRUE(intrinsic.converged) << intrinsic.error;
			EXPECT_NEAR(intrinsic.fermiLevelEv, 0.5, 1e-10);
			const double ni = dos * kT * (std::log1p(std::exp(-0.5 / kT)) - std::log1p(std::exp(-2.5 / kT)));
			EXPECT_NEAR(intrinsic.electronsCm3, ni, std::max(ni * 1e-9, 1e-300));
			// Move E_F to the middle of either band. These exact constant-DOS integrals test
			// electron/hole signs, occupancy near the edge, and a Fermi edge between samples.
			for (double sign : {-1.0, 1.0})
			{
				const double expectedFermi = sign > 0.0 ? 2.0 : -1.0;
				const double minority = dos * kT *
					(std::log1p(std::exp(-2.0 / kT)) - std::log1p(std::exp(-4.0 / kT)));
				const auto doped = SolveDefectConcentrations({}, bands, 1.0, temperature, sign * (dos - minority));
				ExpectNeutral(doped);
				EXPECT_NEAR(doped.fermiLevelEv, expectedFermi, 1e-9);
				EXPECT_NEAR((sign > 0.0 ? doped.electronsCm3 : doped.holesCm3) / dos, 1.0, 1e-9);
			}
		}
		// Linear DOS, rather than a constant: the Sommerfeld correction is exact up to the
		// exponentially small finite-band tails at 1 K, n = D'*(1/2 + pi^2*(kT)^2/6).
		const TabulatedDosBands triangular{{{-2.0, 2.0 * dos}, {0.0, 0.0}}, {{1.0, 0.0}, {3.0, 2.0 * dos}}};
		const double kT = kBoltzmannEvPerK;
		const double fixed = dos * (0.5 + std::numbers::pi * std::numbers::pi * kT * kT / 6.0);
		const auto linear = SolveDefectConcentrations({}, triangular, 1.0, 1.0, fixed);
		ExpectNeutral(linear);
		EXPECT_NEAR(linear.fermiLevelEv, 2.0, 1e-9);
	}

	TEST(DefectConcentrationsTests, LogNeutralitySurvivesOneKelvinUnderflowAndNegativeFormationEnergies)
	{
		const EffectiveMassBands bands{0.5, 2.0};
		const auto intrinsic = SolveDefectConcentrations({}, bands, 5.5, 1.0);
		ASSERT_TRUE(intrinsic.converged) << intrinsic.error;
		EXPECT_NEAR(intrinsic.fermiLevelEv, 2.75 + 0.75 * kBoltzmannEvPerK * std::log(4.0), 1e-12);
		EXPECT_DOUBLE_EQ(intrinsic.electronsCm3, 0.0);
		EXPECT_DOUBLE_EQ(intrinsic.holesCm3, 0.0);
		// Both charged exponentials overflow if evaluated naively at either bracket endpoint.
		const std::array defects{ConcentrationDefect{"pair", 1e22, {{1, -2.0, 1.0}, {-1, 2.0, 1.0}, {0, 20.0, 1.0}}}};
		const auto paired = SolveDefectConcentrations(defects, bands, 5.5, 1.0);
		ExpectNeutral(paired);
		EXPECT_NEAR(paired.fermiLevelEv, 2.0, 1e-12);
		EXPECT_DOUBLE_EQ(paired.chargeStates[2].concentrationCm3, 0.0);
	}

	TEST(DefectConcentrationsTests, GridPreservesEveryPointAndReusesFormationEnergyAtAbsoluteMu)
	{
		const FormationReference reference{-10.0, 0.2, 1.2};
		const std::array inputs{ConcentrationDefectInput{
			{{"X", 1, -9.0, {{"C", -1}}, 0.1}, {"X", 0, -8.0, {{"C", -1}}, 0.0}}, 1e22, {3.0, 1.0}}};
		const std::array temperatures{300.0, 600.0};
		const std::array muSets{SpeciesEnergies{{"C", -0.5}}, SpeciesEnergies{{"C", -0.7}}, SpeciesEnergies{}};
		const std::array fixed{0.0, 1e15};
		const auto prepared = BuildConcentrationDefects(inputs, reference, muSets[0]);
		ASSERT_TRUE(prepared.error.empty()) << prepared.error;
		ASSERT_EQ(prepared.defects.size(), 1u);
		EXPECT_DOUBLE_EQ(prepared.defects[0].chargeStates[0].interceptEv,
			*FormationEnergy(inputs[0].chargeStates[0], reference, muSets[0], 0.0));
		EXPECT_DOUBLE_EQ(prepared.defects[0].chargeStates[0].degeneracy, 3.0);
		const auto points = SolveDefectConcentrationGrid(inputs, reference, EffectiveMassBands{0.5, 1.0},
			temperatures, muSets, fixed);
		ASSERT_EQ(points.size(), 12u);
		for (std::size_t i = 0; i < points.size(); ++i)
		{
			const auto &point = points[i];
			EXPECT_EQ(point.temperatureIndex, i / 6);
			EXPECT_EQ(point.chemicalPotentialIndex, (i / 2) % 3);
			EXPECT_EQ(point.fixedChargeIndex, i % 2);
			EXPECT_DOUBLE_EQ(point.solution.temperatureK, temperatures[point.temperatureIndex]);
			EXPECT_DOUBLE_EQ(point.solution.fixedChargeCm3, fixed[point.fixedChargeIndex]);
			if (point.chemicalPotentialIndex < 2)
				ExpectNeutral(point.solution);
			else
			{
				EXPECT_FALSE(point.solution.converged);
				EXPECT_FALSE(point.solution.error.empty());
			}
		}
		ASSERT_EQ(points[2].solution.defectTotalsCm3.size(), 1u);
		ASSERT_EQ(points[0].solution.defectTotalsCm3.size(), 1u);
		EXPECT_GT(points[2].solution.defectTotalsCm3[0], points[0].solution.defectTotalsCm3[0]);
		EXPECT_TRUE(SolveDefectConcentrationGrid(inputs, reference, EffectiveMassBands{0.5, 1.0},
			{}, muSets, fixed).empty());
		auto defaultInput = inputs[0];
		defaultInput.degeneracies.clear();
		const auto defaults = BuildConcentrationDefects(std::array{defaultInput}, reference, muSets[0]);
		ASSERT_TRUE(defaults.error.empty());
		EXPECT_DOUBLE_EQ(defaults.defects[0].chargeStates[0].degeneracy, 1.0);
	}

	TEST(DefectConcentrationsTests, InvalidInputsOverflowAndInsufficientDosReturnErrors)
	{
		const EffectiveMassBands bands{1.0, 1.0};
		for (double temperature : {0.0, 3001.0, std::numeric_limits<double>::quiet_NaN()})
			EXPECT_FALSE(SolveDefectConcentrations({}, bands, 1.0, temperature).error.empty());
		EXPECT_FALSE(SolveDefectConcentrations({}, bands, -1.0, 300.0).error.empty());
		EXPECT_FALSE(SolveDefectConcentrations({}, EffectiveMassBands{}, 1.0, 300.0).error.empty());
		EXPECT_FALSE(SolveDefectConcentrations({}, bands, 1.0, 300.0,
			std::numeric_limits<double>::infinity()).error.empty());
		const std::array badDefect{ConcentrationDefect{"bad", 1e22, {{0, 1.0, 0.0}}}};
		EXPECT_FALSE(SolveDefectConcentrations(badDefect, bands, 1.0, 300.0).error.empty());
		const std::array overflow{ConcentrationDefect{"overflow", 1e22, {{0, -20.0, 1.0}}}};
		const auto enormous = SolveDefectConcentrations(overflow, bands, 1.0, 1.0);
		EXPECT_FALSE(enormous.converged);
		EXPECT_FALSE(enormous.error.empty());
		TabulatedDosBands dos{{{-1.0, 1e20}, {0.0, 1e20}}, {{1.0, 1e20}, {2.0, 1e20}}};
		const auto insufficient = SolveDefectConcentrations({}, dos, 1.0, 300.0, 2e20);
		EXPECT_FALSE(insufficient.converged);
		EXPECT_FALSE(insufficient.error.empty());
		dos.conduction[1].energyEv = 0.5;
		EXPECT_FALSE(SolveDefectConcentrations({}, dos, 1.0, 300.0).error.empty());
		const std::array input{ConcentrationDefectInput{{{"X", 0, 1.0, {}, 0.0}}, -1.0, {}}};
		EXPECT_FALSE(BuildConcentrationDefects(input, {}, {}).error.empty());
	}
} // namespace DefectStudio::Tests
