#include <gtest/gtest.h>

#include "Domain/Crystal/BondGenerator.hpp"

namespace DefectStudio::Tests
{
	TEST(BondGeneratorTests, CreatesAutoBondWithinCovalentCutoff)
	{
		CrystalStructure structure;
		structure.atoms = {
			AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0},
			AtomSite{"C", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f), 1}};

		ElementPropertiesTable properties;
		properties.ReplaceData({{"C", ElementProperties{6, 12.0f, 0.80f, 1.70f}}});

		RegenerateAutoBonds(structure, properties);

		ASSERT_EQ(structure.bonds.size(), 1u);
		EXPECT_EQ(structure.bonds[0].firstAtomIndex, 0u);
		EXPECT_EQ(structure.bonds[0].secondAtomIndex, 1u);
		EXPECT_NEAR(structure.bonds[0].lengthAngstrom, 1.0f, 1e-5f);
		EXPECT_EQ(structure.bonds[0].origin, BondOrigin::Auto);
	}

	TEST(BondGeneratorTests, PreservesManualBondsWhenRegeneratingAutoBonds)
	{
		CrystalStructure structure;
		structure.atoms = {
			AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0},
			AtomSite{"O", glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f), 1}};
		structure.bonds.push_back(Bond{0u, 1u, 5.0f, BondOrigin::Manual, true});

		ElementPropertiesTable properties;
		properties.ReplaceData({
			{"C", ElementProperties{6, 12.0f, 0.80f, 1.70f}},
			{"O", ElementProperties{8, 16.0f, 0.73f, 1.52f}}});

		RegenerateAutoBonds(structure, properties);

		ASSERT_EQ(structure.bonds.size(), 1u);
		EXPECT_EQ(structure.bonds[0].origin, BondOrigin::Manual);
		EXPECT_EQ(structure.bonds[0].firstAtomIndex, 0u);
		EXPECT_EQ(structure.bonds[0].secondAtomIndex, 1u);
	}
} // namespace DefectStudio::Tests
