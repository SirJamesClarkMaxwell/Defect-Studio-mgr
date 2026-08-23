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

	TEST(BondGeneratorTests, PreservesHiddenAutoBondVisibilityAcrossRegen)
	{
		CrystalStructure structure;
		structure.atoms = {
			AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0},
			AtomSite{"C", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f), 1}};

		ElementPropertiesTable properties;
		properties.ReplaceData({{"C", ElementProperties{6, 12.0f, 0.80f, 1.70f}}});

		RegenerateAutoBonds(structure, properties);
		ASSERT_EQ(structure.bonds.size(), 1u);
		structure.bonds[0].visible = false; // simulates the bond-delete command hiding an Auto bond

		// An unrelated atom edit (e.g. moving a third atom) triggers another regen - the hide must
		// survive it, not just the one call that set it.
		RegenerateAutoBonds(structure, properties);

		ASSERT_EQ(structure.bonds.size(), 1u);
		EXPECT_EQ(structure.bonds[0].origin, BondOrigin::Auto);
		EXPECT_FALSE(structure.bonds[0].visible);
	}

	TEST(BondGeneratorTests, CreatesPeriodicBondAcrossCellBoundary)
	{
		CrystalStructure structure;
		structure.cell.vectors = {
			glm::vec3(10.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 10.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 10.0f)};
		// Direct distance is 9.8 (far beyond any covalent cutoff) - only close via wraparound.
		structure.atoms = {
			AtomSite{"C", glm::vec3(0.1f, 5.0f, 5.0f), glm::vec3(0.01f, 0.5f, 0.5f), 0},
			AtomSite{"C", glm::vec3(9.9f, 5.0f, 5.0f), glm::vec3(0.99f, 0.5f, 0.5f), 1}};

		ElementPropertiesTable properties;
		properties.ReplaceData({{"C", ElementProperties{6, 12.0f, 0.80f, 1.70f}}});

		RegenerateAutoBonds(structure, properties);

		ASSERT_EQ(structure.bonds.size(), 1u);
		EXPECT_NEAR(structure.bonds[0].lengthAngstrom, 0.2f, 1e-4f);
		EXPECT_EQ(structure.bonds[0].periodicShift.x, -1);
		EXPECT_EQ(structure.bonds[0].periodicShift.y, 0);
		EXPECT_EQ(structure.bonds[0].periodicShift.z, 0);
	}

	TEST(BondGeneratorTests, SkipsPeriodicBondingForDegenerateCell)
	{
		CrystalStructure structure;
		// C-C cutoff here is 1.18*(0.8+0.8)=1.888, so the safety threshold is 2x that = 3.776 -
		// this 3.0 cell is below it, so periodic search must be skipped even though these atoms
		// would otherwise be a clean wraparound match, same as the test above.
		structure.cell.vectors = {
			glm::vec3(3.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 3.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 3.0f)};
		structure.atoms = {
			AtomSite{"C", glm::vec3(0.1f, 1.5f, 1.5f), glm::vec3(0.033f, 0.5f, 0.5f), 0},
			AtomSite{"C", glm::vec3(2.9f, 1.5f, 1.5f), glm::vec3(0.967f, 0.5f, 0.5f), 1}};

		ElementPropertiesTable properties;
		properties.ReplaceData({{"C", ElementProperties{6, 12.0f, 0.80f, 1.70f}}});

		RegenerateAutoBonds(structure, properties);

		EXPECT_TRUE(structure.bonds.empty());
	}
} // namespace DefectStudio::Tests
