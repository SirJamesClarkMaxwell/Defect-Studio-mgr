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

	TEST(StructureComparisonTests, CostMatrixReportsDistanceForInRangePair)
	{
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.atoms = {AtomSite{"C", glm::vec3(0.3f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		const Result<DisplacementCostMatrix> matrixResult =
			BuildDisplacementCostMatrix(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_TRUE(matrixResult.HasValue());
		const DisplacementCostMatrix &matrix = matrixResult.Value();
		EXPECT_NEAR(matrix.At(0, 0), 0.3f, 1e-5f);

		const std::vector<int> assignment = {0};
		const StructureComparisonResult result =
			BuildComparisonResultFromAssignment(reference, comparison, matrix, assignment);

		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_EQ(result.matches[0].referenceAtomIndex, 0u);
		EXPECT_NEAR(result.matches[0].magnitudeAngstrom, 0.3f, 1e-5f);
		EXPECT_TRUE(result.unmatchedReferenceAtomIndices.empty());
		EXPECT_TRUE(result.unmatchedComparisonAtoms.empty());
	}

	TEST(StructureComparisonTests, CrossSpeciesMatchingIsIncludedByDefault)
	{
		// 2026-08-24 decision: cross-species matches (e.g. a substitution, C->B) are ON by default -
		// the display threshold filters what's drawn later, not the matcher.
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.atoms = {AtomSite{"O", glm::vec3(0.05f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		const DisplacementCostMatrix defaultMatrix =
			BuildDisplacementCostMatrix(reference, comparison, MakeCarbonOxygenTable()).Value();
		EXPECT_LT(defaultMatrix.At(0, 0), kUnmatchedDisplacementCost);

		const DisplacementCostMatrix restrictedMatrix =
			BuildDisplacementCostMatrix(
				reference, comparison, MakeCarbonOxygenTable(), /*cutoffScale=*/1.5f,
				/*restrictToSameSpecies=*/true)
				.Value();
		EXPECT_EQ(restrictedMatrix.At(0, 0), kUnmatchedDisplacementCost);
	}

	TEST(StructureComparisonTests, ForcedSentinelAssignmentBecomesVacancyAndInterstitial)
	{
		// Two comparison atoms, one reference atom: a rectangular assignment must map both
		// comparison rows somewhere, but only one can get the real (in-range) reference atom. The
		// other is forced onto a sentinel-cost pairing, which must be reported as unmatched on both
		// sides, not as a bogus long-displacement arrow.
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.atoms = {
			AtomSite{"C", glm::vec3(0.1f, 0.0f, 0.0f), glm::vec3(0.0f), 0},
			AtomSite{"C", glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f), 1}};

		const DisplacementCostMatrix matrix =
			BuildDisplacementCostMatrix(reference, comparison, MakeCarbonOxygenTable()).Value();
		ASSERT_LT(matrix.At(0, 0), kUnmatchedDisplacementCost);
		ASSERT_EQ(matrix.At(1, 0), kUnmatchedDisplacementCost);

		// Assignment forced to give both comparison atoms the only reference atom's index is not
		// possible in a real one-to-one assignment - a solver instead reports comparison atom 1 as
		// assigned to reference index 0 anyway only if forced (min(N,M) mappings); simulate that
		// forced case directly here since this test targets the *consumer* of an assignment, not the
		// solver itself.
		const std::vector<int> assignment = {0, 0};
		const StructureComparisonResult result =
			BuildComparisonResultFromAssignment(reference, comparison, matrix, assignment);

		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_NEAR(result.matches[0].comparisonPosition.x, 0.1f, 1e-5f);
		ASSERT_EQ(result.unmatchedComparisonAtoms.size(), 1u);
		EXPECT_NEAR(result.unmatchedComparisonAtoms[0].position.x, 5.0f, 1e-5f);
		EXPECT_TRUE(result.unmatchedReferenceAtomIndices.empty());
	}

	TEST(StructureComparisonTests, UnassignedComparisonAtomBecomesInterstitialAndReferenceBecomesVacancy)
	{
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.atoms = {AtomSite{"O", glm::vec3(5.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		const DisplacementCostMatrix matrix =
			BuildDisplacementCostMatrix(reference, comparison, MakeCarbonOxygenTable()).Value();

		const std::vector<int> assignment = {-1};
		const StructureComparisonResult result =
			BuildComparisonResultFromAssignment(reference, comparison, matrix, assignment);

		EXPECT_TRUE(result.matches.empty());
		ASSERT_EQ(result.unmatchedComparisonAtoms.size(), 1u);
		EXPECT_EQ(result.unmatchedComparisonAtoms[0].species, "O");
		ASSERT_EQ(result.unmatchedReferenceAtomIndices.size(), 1u);
		EXPECT_EQ(result.unmatchedReferenceAtomIndices[0], 0u);
	}

	TEST(StructureComparisonTests, MinimumImageDistanceWrapsAcrossPeriodicBoundary)
	{
		// Default LatticeCell is a 1x1x1 Angstrom cube. An atom at x=0.95 and one at x=0.05 are 0.1
		// Angstrom apart across the periodic boundary, not 0.9 apart in a straight line.
		CrystalStructure reference;
		reference.isPeriodic = true;
		reference.atoms = {AtomSite{"C", glm::vec3(0.95f, 0.5f, 0.5f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.isPeriodic = true;
		comparison.atoms = {AtomSite{"C", glm::vec3(0.05f, 0.5f, 0.5f), glm::vec3(0.0f), 0}};

		// A generous cutoffScale so the wrapped-short distance (~0.1) is in range even though these
		// default covalent radii wouldn't normally allow it - this test targets the geometry, not
		// the cutoff shape.
		const DisplacementCostMatrix matrix =
			BuildDisplacementCostMatrix(
				reference, comparison, MakeCarbonOxygenTable(), /*cutoffScale=*/1.0f)
				.Value();
		EXPECT_NEAR(matrix.At(0, 0), 0.1f, 1e-4f);
	}

	TEST(StructureComparisonTests, MismatchedLatticesProceedWithWarningInsteadOfFailing)
	{
		// The user picks which two files to compare - a lattice mismatch is a heads-up, not a
		// reason to refuse the whole comparison (see 2026-08-28 feedback).
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.cell.vectors[0] = glm::vec3(2.0f, 0.0f, 0.0f); // different a-vector
		comparison.atoms = {AtomSite{"C", glm::vec3(0.1f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		const Result<DisplacementCostMatrix> matrixResult =
			BuildDisplacementCostMatrix(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_TRUE(matrixResult.HasValue());
		const DisplacementCostMatrix &matrix = matrixResult.Value();
		EXPECT_TRUE(matrix.latticeMismatch);

		const std::vector<int> assignment = {0};
		const StructureComparisonResult result =
			BuildComparisonResultFromAssignment(reference, comparison, matrix, assignment);
		EXPECT_FALSE(result.latticeMismatchWarning.empty());
	}

	TEST(StructureComparisonTests, MatchedLatticesProduceNoWarning)
	{
		CrystalStructure reference;
		reference.atoms = {AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.atoms = {AtomSite{"C", glm::vec3(0.1f, 0.0f, 0.0f), glm::vec3(0.0f), 0}};

		const DisplacementCostMatrix matrix =
			BuildDisplacementCostMatrix(reference, comparison, MakeCarbonOxygenTable()).Value();
		EXPECT_FALSE(matrix.latticeMismatch);

		const std::vector<int> assignment = {0};
		const StructureComparisonResult result =
			BuildComparisonResultFromAssignment(reference, comparison, matrix, assignment);
		EXPECT_TRUE(result.latticeMismatchWarning.empty());
	}

	TEST(StructureComparisonTests, WrappedComparisonPositionUsesShortPeriodicPath)
	{
		// Same setup as MinimumImageDistanceWrapsAcrossPeriodicBoundary - comparisonPositionWrapped
		// should sit near referencePosition (short way, possibly just outside the cell), not at the
		// raw comparison position 0.9 Angstrom away in a straight line.
		CrystalStructure reference;
		reference.isPeriodic = true;
		reference.atoms = {AtomSite{"C", glm::vec3(0.95f, 0.5f, 0.5f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.isPeriodic = true;
		comparison.atoms = {AtomSite{"C", glm::vec3(0.05f, 0.5f, 0.5f), glm::vec3(0.0f), 0}};

		const DisplacementCostMatrix matrix =
			BuildDisplacementCostMatrix(
				reference, comparison, MakeCarbonOxygenTable(), /*cutoffScale=*/1.0f)
				.Value();
		const std::vector<int> assignment = {0};
		const StructureComparisonResult result =
			BuildComparisonResultFromAssignment(reference, comparison, matrix, assignment);

		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_NEAR(result.matches[0].comparisonPositionWrapped.x, 1.05f, 1e-4f);
	}

	TEST(StructureComparisonTests, MismatchedLatticesDoNotWrapByTheWrongCell)
	{
		// Regression for 2026-08-28 feedback: a vacuum-thickness convergence test compares two files
		// with the same in-plane (a, b) vectors but a different c vector. An atom near z=0 in both
		// files is a genuine close match - periodic-wrapping it by the REFERENCE cell's (small) c
		// vector must NOT be applied here (it isn't the same cell), or the reported displacement/arrow
		// would jump by nearly a whole cell length instead of reporting the true short distance.
		CrystalStructure reference;
		reference.isPeriodic = true;
		reference.cell.vectors[2] = glm::vec3(0.0f, 0.0f, 1.0f); // small c (reference cell)
		reference.atoms = {AtomSite{"C", glm::vec3(0.5f, 0.5f, 0.05f), glm::vec3(0.0f), 0}};

		CrystalStructure comparison;
		comparison.isPeriodic = true;
		comparison.cell.vectors[2] = glm::vec3(0.0f, 0.0f, 20.0f); // much larger c (vacuum test)
		comparison.atoms = {AtomSite{"C", glm::vec3(0.5f, 0.5f, 0.15f), glm::vec3(0.0f), 0}};

		const Result<DisplacementCostMatrix> matrixResult =
			BuildDisplacementCostMatrix(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_TRUE(matrixResult.HasValue());
		const DisplacementCostMatrix &matrix = matrixResult.Value();
		ASSERT_TRUE(matrix.latticeMismatch);
		// True separation is 0.1 (z: 0.15 - 0.05), not a wrapped-by-reference-c distance.
		EXPECT_NEAR(matrix.At(0, 0), 0.1f, 1e-4f);

		const std::vector<int> assignment = {0};
		const StructureComparisonResult result =
			BuildComparisonResultFromAssignment(reference, comparison, matrix, assignment);
		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_NEAR(result.matches[0].comparisonPositionWrapped.z, 0.15f, 1e-4f);
	}
} // namespace DefectStudio::Tests
