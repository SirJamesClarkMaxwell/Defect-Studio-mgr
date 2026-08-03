#include <gtest/gtest.h>

#include "Domain/Crystal/CrystalStructure.hpp"

namespace DefectStudio::Tests
{
	TEST(CrystalStructureTests, FractionalAndCartesianConversionsRoundTrip)
	{
		CrystalStructure structure;
		structure.cell.vectors = {
			glm::vec3(2.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 3.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 4.0f)};

		const glm::vec3 fractional(0.25f, 0.50f, 0.75f);
		const glm::vec3 cartesian = structure.FractionalToCartesian(fractional);
		EXPECT_FLOAT_EQ(cartesian.x, 0.50f);
		EXPECT_FLOAT_EQ(cartesian.y, 1.50f);
		EXPECT_FLOAT_EQ(cartesian.z, 3.00f);

		const glm::vec3 recoveredFractional = structure.CartesianToFractional(cartesian);
		EXPECT_NEAR(recoveredFractional.x, fractional.x, 1e-6f);
		EXPECT_NEAR(recoveredFractional.y, fractional.y, 1e-6f);
		EXPECT_NEAR(recoveredFractional.z, fractional.z, 1e-6f);
	}

	TEST(CrystalStructureTests, UniqueSpeciesKeepsFirstOccurrenceOrder)
	{
		CrystalStructure structure;
		structure.atoms = {
			AtomSite{"C", glm::vec3(0.0f), glm::vec3(0.0f), 0},
			AtomSite{"O", glm::vec3(0.0f), glm::vec3(0.0f), 1},
			AtomSite{"C", glm::vec3(0.0f), glm::vec3(0.0f), 2},
			AtomSite{"H", glm::vec3(0.0f), glm::vec3(0.0f), 3}};

		const std::vector<std::string> species = structure.UniqueSpecies();
		ASSERT_EQ(species.size(), 3u);
		EXPECT_EQ(species[0], "C");
		EXPECT_EQ(species[1], "O");
		EXPECT_EQ(species[2], "H");
	}

	TEST(CrystalStructureTests, AtomSiteDefaultsToFullOccupancyAndNoSelectiveDynamics)
	{
		AtomSite atom;

		EXPECT_FLOAT_EQ(atom.occupancy, 1.0f);
		EXPECT_FALSE(atom.hasSelectiveDynamics);
		EXPECT_TRUE(atom.selectiveDynamics[0]);
		EXPECT_TRUE(atom.selectiveDynamics[1]);
		EXPECT_TRUE(atom.selectiveDynamics[2]);
		EXPECT_FLOAT_EQ(atom.charge, 0.0f);
		EXPECT_FLOAT_EQ(atom.magnetization, 0.0f);
	}

	TEST(CrystalStructureTests, HasAnySelectiveDynamicsReturnsTrueWhenAnyAtomFlagged)
	{
		CrystalStructure structure;
		structure.atoms = {
			AtomSite{"C", glm::vec3(0.0f), glm::vec3(0.0f), 0},
			AtomSite{"O", glm::vec3(0.0f), glm::vec3(0.0f), 1}};

		EXPECT_FALSE(structure.HasAnySelectiveDynamics());

		structure.atoms[1].hasSelectiveDynamics = true;
		EXPECT_TRUE(structure.HasAnySelectiveDynamics());
	}

	TEST(CrystalStructureTests, BondDefaultsDescribeAutoVisibleBond)
	{
		Bond bond;
		BondGenerationSettings settings;

		EXPECT_EQ(bond.firstAtomIndex, 0u);
		EXPECT_EQ(bond.secondAtomIndex, 0u);
		EXPECT_EQ(bond.origin, BondOrigin::Auto);
		EXPECT_TRUE(bond.visible);
		EXPECT_FLOAT_EQ(settings.globalCutoffScale, 1.18f);
		EXPECT_TRUE(settings.perPairCutoffOverride.empty());
	}

	TEST(CrystalStructureTests, VacancySiteGeneratesFallbackLabel)
	{
		VacancySite vacancy;
		vacancy.sourceSpecies = "Fe";
		EXPECT_EQ(vacancy.GetLabel(), "V_Fe");

		vacancy.label = "V_custom";
		EXPECT_EQ(vacancy.GetLabel(), "V_custom");
	}
} // namespace DefectStudio::Tests
