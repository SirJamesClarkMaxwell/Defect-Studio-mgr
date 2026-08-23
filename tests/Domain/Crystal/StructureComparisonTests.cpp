#include <gtest/gtest.h>

#include "Domain/Crystal/StructureComparison.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] ElementPropertiesTable MakeCarbonOxygenTable()
		{
			ElementPropertiesTable properties;
			properties.ReplaceData({
				{"C", ElementProperties{6, 12.0f, 0.80f, 1.70f}},
				{"O", ElementProperties{8, 16.0f, 0.73f, 1.52f}}});
			return properties;
		}
	} // namespace

	TEST(StructureComparisonTests, MatchesSameSpeciesAtomAndReportsDisplacement)
	{
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.atoms = {AtomSite{"C", glm::vec3(0.3f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		const std::vector<AtomDisplacement> displacements =
			ComputeAtomDisplacements(reference, comparison, MakeCarbonOxygenTable());

		ASSERT_EQ(displacements.size(), 1u);
		EXPECT_EQ(displacements[0].referenceAtomIndex, 0u);
		EXPECT_NEAR(displacements[0].magnitudeAngstrom, 0.3f, 1e-5f);
	}

	TEST(StructureComparisonTests, SkipsComparisonAtomWithNoSameSpeciesMatchInRange)
	{
		// Simulates a vacancy filled by a substitution (V_2 vs V_2CBCN from the plan doc) - the
		// comparison structure has an atom of a species with no counterpart nearby in reference.
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.atoms = {AtomSite{"O", glm::vec3(0.05f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		const std::vector<AtomDisplacement> displacements =
			ComputeAtomDisplacements(reference, comparison, MakeCarbonOxygenTable());

		EXPECT_TRUE(displacements.empty());
	}

	TEST(StructureComparisonTests, SkipsDisplacementBelowMinimumThreshold)
	{
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.atoms = {AtomSite{"C", glm::vec3(0.005f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		const std::vector<AtomDisplacement> displacements = ComputeAtomDisplacements(
			reference, comparison, MakeCarbonOxygenTable(), /*cutoffScale=*/1.5f,
			/*minimumDisplacementAngstrom=*/0.02f);

		EXPECT_TRUE(displacements.empty());
	}

	TEST(StructureComparisonTests, DoesNotMatchTheSameReferenceAtomTwice)
	{
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.atoms = {
			AtomSite{"C", glm::vec3(0.1f, 0.0f, 0.0f), glm::vec3(0.0f), 0},
			AtomSite{"C", glm::vec3(0.2f, 0.0f, 0.0f), glm::vec3(0.0f), 1}};

		const std::vector<AtomDisplacement> displacements =
			ComputeAtomDisplacements(reference, comparison, MakeCarbonOxygenTable());

		// The single reference atom is claimed by the first comparison atom that wants it - the
		// second finds no unused same-species reference atom left in range.
		ASSERT_EQ(displacements.size(), 1u);
		EXPECT_NEAR(displacements[0].comparisonPosition.x, 0.1f, 1e-5f);
	}
} // namespace DefectStudio::Tests
