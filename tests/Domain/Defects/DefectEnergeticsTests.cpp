#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "Domain/Defects/DefectEnergetics.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		// The BSc-thesis GeV run, path A (test-directory/fnv-diamond/analysis/formation/A_sx_thesisC).
		const FormationReference kDiamond{-5400.79969151, 9.3518, 5.347455};
		const SpeciesEnergies kMu{{"C", -10.54843689748047}, {"Ge", -5.36072499375}, {"Si", -6.29289638125},
			{"N", -10.25441569}};

		DefectChargeState GeV(int charge, double energy, double correction)
		{
			return {"GeV", charge, energy, {{"C", -2}, {"Ge", 1}}, correction};
		}
	} // namespace

	TEST(DefectEnergeticsTests, FormationEnergyReproducesTheThesisGeV)
	{
		const auto plus = FormationEnergy(GeV(1, -5387.04375652, 0.202685), kDiamond, kMu, 0.0);
		ASSERT_TRUE(plus.has_value());
		EXPECT_NEAR(*plus, 7.574271, 1e-6); // thesis defects_creation_energy.json: 7.57427
		const auto neutral = FormationEnergy(GeV(0, -5377.00332106, 0.0), kDiamond, kMu, 1.0);
		ASSERT_TRUE(neutral.has_value());
		EXPECT_NEAR(*neutral, 8.060222, 1e-6); // q = 0 does not depend on E_F
		const auto minus = FormationEnergy(GeV(-1, -5365.90561526, 0.250185), kDiamond, kMu, 2.0);
		ASSERT_TRUE(minus.has_value());
		EXPECT_NEAR(*minus, 10.056312 - 2.0, 1e-6);

		SpeciesEnergies noGe = kMu;
		noGe.erase("Ge");
		EXPECT_FALSE(FormationEnergy(GeV(1, -5387.04375652, 0.202685), kDiamond, noGe, 0.0).has_value());
		// A species with n_i = 0 needs no mu.
		DefectChargeState zero = GeV(0, -5377.00332106, 0.0);
		zero.addedAtoms["N"] = 0;
		SpeciesEnergies noN = kMu;
		noN.erase("N");
		EXPECT_TRUE(FormationEnergy(zero, kDiamond, noN, 0.0).has_value());
	}

	TEST(DefectEnergeticsTests, TransitionLevelIsWhereTheLinesCross)
	{
		EXPECT_NEAR(*TransitionLevel(7.574271, 1, 8.060222, 0), 0.485951, 1e-9);
		EXPECT_NEAR(*TransitionLevel(10.056312, -1, 12.897445, -2), 2.841133, 1e-9);
		EXPECT_FALSE(TransitionLevel(1.0, 1, 2.0, 1).has_value());
	}

	TEST(DefectEnergeticsTests, LowerEnvelopeAndLevelsOfTheThesisGeV)
	{
		const std::array<double, 4> intercepts{8.060222, 7.574271, 12.897445, 10.056312}; // any order
		const std::array<int, 4> charges{0, 1, -2, -1};
		const auto envelope = LowerEnvelope(intercepts, charges, kDiamond.gap);
		ASSERT_EQ(envelope.size(), 4u);
		EXPECT_EQ(envelope[0].line, 1u);
		EXPECT_EQ(envelope[1].line, 0u);
		EXPECT_EQ(envelope[2].line, 3u);
		EXPECT_EQ(envelope[3].line, 2u);
		EXPECT_DOUBLE_EQ(envelope[0].from, 0.0);
		EXPECT_NEAR(envelope[0].to, 0.485951, 1e-9);
		EXPECT_NEAR(envelope[1].to, 1.996090, 1e-9);
		EXPECT_NEAR(envelope[2].to, 2.841133, 1e-9);
		EXPECT_DOUBLE_EQ(envelope[3].to, kDiamond.gap);
		for (std::size_t i = 1; i < envelope.size(); ++i)
			EXPECT_DOUBLE_EQ(envelope[i].from, envelope[i - 1].to);

		const auto levels = EnvelopeTransitions(envelope, charges);
		ASSERT_EQ(levels.size(), 3u);
		EXPECT_EQ(levels[0].chargeFrom, 1);
		EXPECT_EQ(levels[0].chargeTo, 0);
		EXPECT_NEAR(levels[0].level, 0.485951, 1e-9);
		EXPECT_EQ(levels[2].chargeFrom, -1);
		EXPECT_EQ(levels[2].chargeTo, -2);
	}

	TEST(DefectEnergeticsTests, NegativeUSkipsTheNeutralStateAndTiesKeepTheFirstLine)
	{
		// +1: E_F, 0: 2, -1: 3 - E_F. They meet at E_F = 1.5 below the neutral line.
		const std::array<double, 3> intercepts{0.0, 2.0, 3.0};
		const std::array<int, 3> charges{1, 0, -1};
		const auto envelope = LowerEnvelope(intercepts, charges, 3.0);
		ASSERT_EQ(envelope.size(), 2u);
		EXPECT_EQ(envelope[0].line, 0u);
		EXPECT_EQ(envelope[1].line, 2u);
		EXPECT_NEAR(envelope[0].to, 1.5, 1e-12);
		const auto levels = EnvelopeTransitions(envelope, charges);
		ASSERT_EQ(levels.size(), 1u);
		EXPECT_EQ(levels[0].chargeFrom, 1);
		EXPECT_EQ(levels[0].chargeTo, -1);
		EXPECT_NEAR(levels[0].level, 1.5, 1e-12);

		// Two identical lines (two configurations of one charge): one segment, the first one.
		const std::array<double, 2> same{1.0, 1.0};
		const std::array<int, 2> sameCharge{0, 0};
		const auto tie = LowerEnvelope(same, sameCharge, 2.0);
		ASSERT_EQ(tie.size(), 1u);
		EXPECT_EQ(tie[0].line, 0u);
		// A line touching the envelope at one point only (at E_F = 1) is not a segment.
		const std::array<double, 3> touch{0.0, 1.0, 2.0};
		const std::array<int, 3> touchCharges{1, 0, -1};
		const auto touched = LowerEnvelope(touch, touchCharges, 2.0);
		ASSERT_EQ(touched.size(), 2u);
		EXPECT_EQ(touched[0].line, 0u);
		EXPECT_EQ(touched[1].line, 2u);

		EXPECT_TRUE(LowerEnvelope(intercepts, charges, 0.0).empty());
	}

	TEST(DefectEnergeticsTests, DiamondWithElementalReferencesOnlyHasOneRichCorner)
	{
		ChemicalPotentialSystem system;
		system.references = kMu;
		system.species = {"C", "Ge", "Si", "N"};
		system.host = {"diamond", {{"C", 1}}, kDiamond.bulkEnergy / 512.0};
		EXPECT_NEAR(*FormationEnthalpy(system.host, system.references), 0.0, 1e-9);

		const ChemicalPotentialLimits limits = ChemicalPotentialVertices(system);
		ASSERT_TRUE(limits.error.empty()) << limits.error;
		ASSERT_EQ(limits.vertices.size(), 1u);
		for (const auto &species : system.species)
			EXPECT_NEAR(limits.vertices[0].deltaMu.at(species), 0.0, 1e-9) << species;
		EXPECT_EQ(limits.vertices[0].limitingPhases, (std::vector<std::string>{"Ge", "Si", "N"}));

		const SpeciesEnergies mu = AbsoluteChemicalPotentials(system, {{"N", -1.0}});
		EXPECT_DOUBLE_EQ(mu.at("N"), kMu.at("N") - 1.0);
		EXPECT_DOUBLE_EQ(mu.at("Ge"), kMu.at("Ge"));

		ChemicalPotentialSystem broken = system;
		broken.references.erase("Ge");
		EXPECT_FALSE(ChemicalPotentialVertices(broken).error.empty());
	}

	TEST(DefectEnergeticsTests, CompetingPhasesCutTheImpurityRichCorners)
	{
		// Delta H_f(SiC) = -0.5 eV, Delta H_f(Si3N4) = -8 eV per formula unit.
		ChemicalPotentialSystem system;
		system.references = {{"C", -10.0}, {"Si", -6.0}, {"N", -9.0}};
		system.species = {"C", "Si", "N"};
		system.host = {"diamond", {{"C", 1}}, -10.0};
		system.competing = {{"SiC", {{"Si", 1}, {"C", 1}}, -16.5}, {"Si3N4", {{"Si", 3}, {"N", 4}}, -18.0 - 36.0 - 8.0}};
		EXPECT_NEAR(*FormationEnthalpy(system.competing[0], system.references), -0.5, 1e-12);
		EXPECT_NEAR(*FormationEnthalpy(system.competing[1], system.references), -8.0, 1e-12);

		const ChemicalPotentialLimits limits = ChemicalPotentialVertices(system);
		ASSERT_TRUE(limits.error.empty()) << limits.error;
		ASSERT_EQ(limits.vertices.size(), 2u);
		// Sorted by limitingPhases: {"N", "Si3N4"} before {"SiC", "Si3N4"}.
		EXPECT_EQ(limits.vertices[0].limitingPhases, (std::vector<std::string>{"N", "Si3N4"}));
		EXPECT_NEAR(limits.vertices[0].deltaMu.at("Si"), -8.0 / 3.0, 1e-9);
		EXPECT_NEAR(limits.vertices[0].deltaMu.at("N"), 0.0, 1e-9);
		EXPECT_EQ(limits.vertices[1].limitingPhases, (std::vector<std::string>{"SiC", "Si3N4"}));
		EXPECT_NEAR(limits.vertices[1].deltaMu.at("Si"), -0.5, 1e-9);
		EXPECT_NEAR(limits.vertices[1].deltaMu.at("N"), -1.625, 1e-9);
		for (const auto &vertex : limits.vertices)
			EXPECT_TRUE(ViolatedChemicalPotentialBounds(system, vertex.deltaMu).empty());

		EXPECT_EQ(ViolatedChemicalPotentialBounds(system, {{"Si", 0.0}, {"N", 0.0}}),
			(std::vector<std::string>{"SiC", "Si3N4"}));
		EXPECT_EQ(ViolatedChemicalPotentialBounds(system, {{"C", 0.1}, {"Si", -3.0}, {"N", 0.2}}),
			(std::vector<std::string>{"diamond", "N"}));
	}

	TEST(DefectEnergeticsTests, BinaryHostHasARichCornerForEachElement)
	{
		// hBN: Delta H_f = -17.6 - (-6.7 - 8.3) = -2.6 eV per formula unit.
		ChemicalPotentialSystem system;
		system.references = {{"B", -6.7}, {"N", -8.3}};
		system.species = {"B", "N"};
		system.host = {"hBN", {{"B", 1}, {"N", 1}}, -17.6};
		const ChemicalPotentialLimits limits = ChemicalPotentialVertices(system);
		ASSERT_TRUE(limits.error.empty()) << limits.error;
		ASSERT_EQ(limits.vertices.size(), 2u);
		EXPECT_EQ(limits.vertices[0].limitingPhases, (std::vector<std::string>{"B"})); // B-rich
		EXPECT_NEAR(limits.vertices[0].deltaMu.at("N"), -2.6, 1e-9);
		EXPECT_EQ(limits.vertices[1].limitingPhases, (std::vector<std::string>{"N"})); // N-rich
		EXPECT_NEAR(limits.vertices[1].deltaMu.at("B"), -2.6, 1e-9);

		// A host that is less stable than its elements has no allowed region.
		ChemicalPotentialSystem unstable = system;
		unstable.host.energyPerFormulaUnit = -14.0;
		EXPECT_FALSE(ChemicalPotentialVertices(unstable).error.empty());
	}
} // namespace DefectStudio::Tests
