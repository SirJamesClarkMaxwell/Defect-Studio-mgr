#include <gtest/gtest.h>

#include "Domain/Crystal/PeriodicGeometry.hpp"
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

		[[nodiscard]] AtomSite MakeAtom(const std::string &species, glm::vec3 position)
		{
			AtomSite atom;
			atom.species = species;
			atom.position = position;
			return atom;
		}

		// std::span has no initializer_list constructor (it isn't a container) - a plain braced-init
		// list argument at a call site can't deduce it, so single-component tests go through this
		// helper instead of `{{0}}` directly.
		[[nodiscard]] std::vector<std::vector<int>> Single(std::vector<int> assignment)
		{
			return {std::move(assignment)};
		}

		// Big enough that maxMatchDisplacementAngstrom's default (2 A) search never wraps around and
		// touches an atom's own periodic image by accident in these tests - most of them care about
		// spatial locality, not periodicity, unless a test says otherwise.
		[[nodiscard]] LatticeCell MakeLargeCubicCell(float edgeLength = 100.0f)
		{
			LatticeCell cell;
			cell.vectors[0] = glm::vec3(edgeLength, 0.0f, 0.0f);
			cell.vectors[1] = glm::vec3(0.0f, edgeLength, 0.0f);
			cell.vectors[2] = glm::vec3(0.0f, 0.0f, edgeLength);
			return cell;
		}
	} // namespace

	// --- A: reordered atoms ---------------------------------------------------------------------

	TEST(StructureComparisonTests, ReorderedAtomsStillMatchCorrectly)
	{
		// Same three atoms, comparison lists them in a different order. POSCAR/CONTCAR ordering
		// cannot be trusted - matching must be purely spatial, not index-based.
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {
			MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(20.0f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(40.0f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {
			MakeAtom("C", glm::vec3(40.0f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(20.0f, 0.0f, 0.0f))};

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		// Atoms are 20 A apart, far beyond the 2 A default match distance - three independent 1x1
		// components, no cross-atom candidate edges at all.
		ASSERT_EQ(plan.assignableComponents.size(), 3u);
		EXPECT_TRUE(plan.isolatedReferenceIndices.empty());
		EXPECT_TRUE(plan.isolatedComparisonIndices.empty());

		// Each component is exactly 1x1.
		const std::vector<std::vector<int>> assignments(plan.assignableComponents.size(), std::vector<int>{0});

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, assignments);
		ASSERT_EQ(result.matches.size(), 3u);
		EXPECT_TRUE(result.unmatchedReferenceAtomIndices.empty());
		EXPECT_TRUE(result.unmatchedComparisonAtoms.empty());
		for (const AtomDisplacement &match : result.matches)
			EXPECT_NEAR(match.magnitudeAngstrom, 0.0f, 1e-4f);
	}

	// --- B: small relaxation ----------------------------------------------------------------------

	TEST(StructureComparisonTests, SmallRelaxationRecoversOriginalSites)
	{
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {
			MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(20.0f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {
			MakeAtom("C", glm::vec3(20.1f, -0.05f, 0.0f)), // small relaxation of atom 1, listed first
			MakeAtom("C", glm::vec3(-0.08f, 0.03f, 0.0f))}; // small relaxation of atom 0

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_EQ(plan.assignableComponents.size(), 2u);

		std::vector<std::vector<int>> assignments{{0}, {0}};
		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, assignments);
		ASSERT_EQ(result.matches.size(), 2u);
		for (const AtomDisplacement &match : result.matches)
		{
			EXPECT_LT(match.magnitudeAngstrom, 0.2f);
			// Recovered its own original site, not the other atom's.
			EXPECT_NEAR(
				glm::length(match.referencePosition - reference.atoms[match.referenceAtomIndex].position), 0.0f, 1e-5f);
		}
	}

	// --- C: periodic boundary ---------------------------------------------------------------------

	TEST(StructureComparisonTests, AtomCrossingPeriodicBoundaryMatchesWithShortDisplacement)
	{
		CrystalStructure reference;
		reference.isPeriodic = true;
		reference.cell = MakeLargeCubicCell(10.0f);
		reference.atoms = {MakeAtom("C", glm::vec3(9.9f, 5.0f, 5.0f))};

		CrystalStructure comparison;
		comparison.isPeriodic = true;
		comparison.cell = reference.cell;
		comparison.atoms = {MakeAtom("C", glm::vec3(0.1f, 5.0f, 5.0f))}; // wrapped across x=0/x=10

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		EXPECT_TRUE(plan.usePeriodicMatching);
		ASSERT_EQ(plan.assignableComponents.size(), 1u);

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_NEAR(result.matches[0].magnitudeAngstrom, 0.2f, 1e-3f);
		// The wrapped point sits just past the reference atom's own x=9.9, not all the way back at
		// the raw comparison position x=0.1 (which would draw an arrow across the whole cell).
		EXPECT_NEAR(result.matches[0].comparisonPositionWrapped.x, 10.1f, 1e-3f);
	}

	// --- D: non-periodic structure ------------------------------------------------------------------

	TEST(StructureComparisonTests, NonPeriodicStructureNeverWrapsEvenWithMatchingLatticeCell)
	{
		// Same LatticeCell on both sides (so a lattice-mismatch check alone couldn't catch this), but
		// isPeriodic=false - periodic wrapping must not be applied merely because the cells happen to
		// match (2026-08-28 spec point 7).
		CrystalStructure reference;
		reference.isPeriodic = false;
		reference.cell = MakeLargeCubicCell(1.0f);
		reference.atoms = {MakeAtom("C", glm::vec3(0.95f, 0.5f, 0.5f))};

		CrystalStructure comparison;
		comparison.isPeriodic = false;
		comparison.cell = reference.cell;
		comparison.atoms = {MakeAtom("C", glm::vec3(0.05f, 0.5f, 0.5f))};

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		EXPECT_FALSE(plan.usePeriodicMatching);
		EXPECT_TRUE(plan.latticeMismatchWarning.empty()); // not a warning case - periodic was never requested
		ASSERT_EQ(plan.assignableComponents.size(), 1u);

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		ASSERT_EQ(result.matches.size(), 1u);
		// Raw distance (0.9), NOT the periodic-wrapped short distance (0.1) a wrongly-periodic
		// treatment would report.
		EXPECT_NEAR(result.matches[0].magnitudeAngstrom, 0.9f, 1e-3f);
		EXPECT_NEAR(result.matches[0].comparisonPositionWrapped.x, 0.05f, 1e-4f);
	}

	// --- E: cross-species substitution --------------------------------------------------------------

	TEST(StructureComparisonTests, CrossSpeciesSubstitutionMatchesByDefault)
	{
		// 2026-08-24 decision: cross-species matches (e.g. a substitution, C->B) are ON by default -
		// the display threshold filters what's drawn later, not the matcher.
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {MakeAtom("O", glm::vec3(0.05f, 0.0f, 0.0f))};

		const LocalMatchingPlan defaultPlan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		EXPECT_EQ(defaultPlan.assignableComponents.size(), 1u);
		EXPECT_TRUE(defaultPlan.isolatedComparisonIndices.empty());

		const LocalMatchingPlan restrictedPlan = BuildLocalMatchingPlan(
			reference, comparison, MakeCarbonOxygenTable(), kDefaultMaxMatchDisplacementAngstrom,
			/*cutoffScale=*/1.5f, /*restrictToSameSpecies=*/true);
		EXPECT_TRUE(restrictedPlan.assignableComponents.empty());
		ASSERT_EQ(restrictedPlan.isolatedComparisonIndices.size(), 1u);
		ASSERT_EQ(restrictedPlan.isolatedReferenceIndices.size(), 1u);
	}

	// --- F: vacancy --------------------------------------------------------------------------------

	TEST(StructureComparisonTests, MissingReferenceAtomBecomesVacancyWithoutDisturbingOthers)
	{
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {
			MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(20.0f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f))}; // second atom simply absent

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_EQ(plan.assignableComponents.size(), 1u);
		ASSERT_EQ(plan.isolatedReferenceIndices.size(), 1u);
		EXPECT_EQ(plan.isolatedReferenceIndices[0], 1u);

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_EQ(result.matches[0].referenceAtomIndex, 0u);
		ASSERT_EQ(result.unmatchedReferenceAtomIndices.size(), 1u);
		EXPECT_EQ(result.unmatchedReferenceAtomIndices[0], 1u);
		EXPECT_TRUE(result.unmatchedComparisonAtoms.empty());
	}

	// --- G: interstitial ---------------------------------------------------------------------------

	TEST(StructureComparisonTests, ExtraComparisonAtomBecomesInterstitialWithoutDisturbingOthers)
	{
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {
			MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(20.0f, 0.0f, 0.0f))}; // new atom, far from anything reference has

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_EQ(plan.assignableComponents.size(), 1u);
		ASSERT_EQ(plan.isolatedComparisonIndices.size(), 1u);
		EXPECT_EQ(plan.isolatedComparisonIndices[0], 1u);

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_EQ(result.matches[0].comparisonAtomIndex, 0u);
		EXPECT_TRUE(result.unmatchedReferenceAtomIndices.empty());
		ASSERT_EQ(result.unmatchedComparisonAtoms.size(), 1u);
		EXPECT_NEAR(result.unmatchedComparisonAtoms[0].position.x, 20.0f, 1e-4f);
	}

	// --- H: the critical regression - vacancy + interstitial, same total atom count ----------------

	TEST(StructureComparisonTests, VacancyPlusInterstitialDoesNotCascadeIntoNeighborHopping)
	{
		// Regression for the reported 2026-08-28 bug: a 10-atom chain (spacing 1 A, well within the
		// old global cutoff) with one atom removed (vacancy at x=5) and one new atom added far away
		// (x=1000, an interstitial with zero real candidates). Every surviving atom stays at its
		// EXACT original position. Old behavior (one global 10x10 Hungarian assignment,
		// comparisonCount == referenceCount): scipy is forced into a full permutation, and since the
		// sentinel cost (1e6) vastly exceeds even a long chain of real 1 A hops, it was cheaper to
		// reassign x=4->5, x=3->4, x=2->3, ... (a neighbor-hopping cascade) than to pay the sentinel
		// for x=5 and x=1000 - producing exactly the reported long zigzag arrows. Local matching
		// fixes this NOT by changing Hungarian's incentives in the abstract, but by first pulling the
		// far interstitial into its own isolated singleton (zero candidate edges to anything), which
		// makes the remaining local component RECTANGULAR (9 comparison atoms vs 10 reference atoms)
		// instead of falsely square - a perfect zero-cost identity matching then exists for the
		// smaller side, which Hungarian always prefers over any nonzero-cost alternative.
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell(2000.0f);
		for (int x = 0; x <= 9; ++x)
			reference.atoms.push_back(MakeAtom("C", glm::vec3(static_cast<float>(x), 0.0f, 0.0f)));

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		for (int x : {0, 1, 2, 3, 4, 6, 7, 8, 9}) // x=5 removed
			comparison.atoms.push_back(MakeAtom("C", glm::vec3(static_cast<float>(x), 0.0f, 0.0f)));
		comparison.atoms.push_back(MakeAtom("C", glm::vec3(1000.0f, 0.0f, 0.0f))); // interstitial, index 9

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());

		// The far interstitial never shares an edge with anything - it must be pulled out BEFORE
		// Hungarian runs, not left to compete inside a square matrix.
		ASSERT_EQ(plan.isolatedComparisonIndices.size(), 1u);
		EXPECT_EQ(plan.isolatedComparisonIndices[0], 9u);
		EXPECT_TRUE(plan.isolatedReferenceIndices.empty()); // x=5 is NOT isolated - it still has real
		                                                     // candidate edges to x=4 and x=6, see below

		// The rest of the chain (spacing 1, default 2 A match distance) is transitively connected
		// into exactly one local component - still local (doesn't reach the interstitial at x=1000),
		// but not artificially split per-bin either.
		ASSERT_EQ(plan.assignableComponents.size(), 1u);
		const LocalMatchingComponent &component = plan.assignableComponents[0];
		EXPECT_EQ(component.comparisonAtomIndices.size(), 9u);
		EXPECT_EQ(component.referenceAtomIndices.size(), 10u); // rectangular: NOT forced square anymore

		// The correct (zero-cost, identity) local assignment: local comparison index i -> local
		// reference index equal to that atom's x position (comparisonAtomIndices/referenceAtomIndices
		// are both in ascending global-index order here, which equals x-position order by
		// construction) - local ref index 5 (x=5) is deliberately never referenced.
		const std::vector<int> assignment = {0, 1, 2, 3, 4, 6, 7, 8, 9};
		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single(assignment));

		ASSERT_EQ(result.matches.size(), 9u);
		for (const AtomDisplacement &match : result.matches)
			EXPECT_NEAR(match.magnitudeAngstrom, 0.0f, 1e-5f); // every real match is exact, no hopping

		ASSERT_EQ(result.unmatchedReferenceAtomIndices.size(), 1u);
		EXPECT_EQ(result.unmatchedReferenceAtomIndices[0], 5u); // exactly the true vacancy, nothing else

		ASSERT_EQ(result.unmatchedComparisonAtoms.size(), 1u);
		EXPECT_NEAR(result.unmatchedComparisonAtoms[0].position.x, 1000.0f, 1e-3f); // exactly the interstitial
	}

	// A tight, minimal companion to H: verifies BuildComparisonResultFromLocalPlan's sentinel-discard
	// logic in isolation (a component that structurally CANNOT be perfectly matched with real edges
	// alone - a "hub": two comparison atoms only close to the same one reference atom, no other
	// reference atom nearby for either). Plan/component built by hand (bypassing geometry search) to
	// pin down exactly this shape, same spirit as the old (pre-2026-08-28) unit test of this logic.
	TEST(StructureComparisonTests, ForcedSentinelWithinComponentBecomesVacancyAndInterstitial)
	{
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {
			MakeAtom("C", glm::vec3(0.1f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(0.2f, 0.0f, 0.0f))}; // both only close to the one reference atom

		LocalMatchingComponent component;
		component.comparisonAtomIndices = {0, 1};
		component.referenceAtomIndices = {0};
		component.costMatrix.comparisonCount = 2;
		component.costMatrix.referenceCount = 1;
		component.costMatrix.costs = {0.1f, 0.2f};

		LocalMatchingPlan plan;
		plan.assignableComponents = {component};
		plan.usePeriodicMatching = false;

		// A rectangular 2x1 assignment can only ever return one real pair - comparison atom 1 has no
		// column left at all (scipy's own -1-for-unassigned convention, matching
		// ScipyAssignmentBridge's documented contract), not a forced sentinel-cost pairing.
		const std::vector<int> assignment = {0, -1};
		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single(assignment));

		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_EQ(result.matches[0].comparisonAtomIndex, 0u);
		EXPECT_EQ(result.matches[0].referenceAtomIndex, 0u);
		ASSERT_EQ(result.unmatchedComparisonAtoms.size(), 1u);
		EXPECT_NEAR(result.unmatchedComparisonAtoms[0].position.x, 0.2f, 1e-4f);
		EXPECT_TRUE(result.unmatchedReferenceAtomIndices.empty());
	}

	// --- I: bin-boundary relaxation ------------------------------------------------------------------

	TEST(StructureComparisonTests, AtomCrossingASpatialBinBoundaryStillMatches)
	{
		// binCounts along an axis = floor(vectorLength / maxMatchDisplacementAngstrom) - a 6 A cell
		// edge with the default 2 A match distance gives exactly 3 bins (width 2 A each) along that
		// axis, with a boundary at x=2.0. Reference sits just inside bin 0 (x=1.9), comparison just
		// inside bin 1 (x=2.1) - the candidate search must inspect neighboring bins, not just the
		// query atom's own bin.
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.cell.vectors[0] = glm::vec3(6.0f, 0.0f, 0.0f);
		reference.atoms = {MakeAtom("C", glm::vec3(1.9f, 50.0f, 50.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {MakeAtom("C", glm::vec3(2.1f, 50.0f, 50.0f))};

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_EQ(plan.assignableComponents.size(), 1u);
		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_NEAR(result.matches[0].magnitudeAngstrom, 0.2f, 1e-3f);
	}

	// --- J: periodic bin boundary --------------------------------------------------------------------

	TEST(StructureComparisonTests, AtomNearPeriodicBinBoundaryWrapsAndMatches)
	{
		// A 10 A periodic cell with the default 2 A match distance gives 5 bins (width 2 A) along
		// each axis - bin 4 (covering fractional [0.8, 1.0)) and bin 0 (covering [0, 0.2)) are
		// PERIODIC neighbors. An atom at fractional 0.99 (bin 4) must still find one at fractional
		// 0.01 (bin 0) through the wrapped bin search, not just raw adjacency.
		CrystalStructure reference;
		reference.isPeriodic = true;
		reference.cell = MakeLargeCubicCell(10.0f);
		reference.atoms = {MakeAtom("C", glm::vec3(9.9f, 5.0f, 5.0f))}; // fractional 0.99

		CrystalStructure comparison;
		comparison.isPeriodic = true;
		comparison.cell = reference.cell;
		comparison.atoms = {MakeAtom("C", glm::vec3(0.1f, 5.0f, 5.0f))}; // fractional 0.01

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		EXPECT_TRUE(plan.usePeriodicMatching);
		ASSERT_EQ(plan.assignableComponents.size(), 1u);
		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_NEAR(result.matches[0].magnitudeAngstrom, 0.2f, 1e-3f);
	}

	// --- K: non-orthogonal cell -----------------------------------------------------------------------

	TEST(StructureComparisonTests, NonOrthogonalHexagonalCellMatchesAcrossPeriodicBoundary)
	{
		// hBN-style in-plane hexagonal cell (a, b at 120 degrees) with a large out-of-plane vacuum -
		// candidate generation must use the general reciprocal-geometry bin-range bound
		// (FractionalSearchRadius), not an orthogonal-only assumption, and MIC must still find the
		// true short displacement across the skewed a/b boundary.
		CrystalStructure reference;
		reference.isPeriodic = true;
		reference.cell.vectors[0] = glm::vec3(2.5f, 0.0f, 0.0f);
		reference.cell.vectors[1] = glm::vec3(-1.25f, 2.165f, 0.0f); // 120 degrees from a, same length
		reference.cell.vectors[2] = glm::vec3(0.0f, 0.0f, 20.0f); // vacuum
		reference.atoms = {
			MakeAtom("C", glm::vec3(0.1f, 0.05f, 10.0f)), // near the (0,0) corner
			MakeAtom("C", glm::vec3(1.2f, 1.0f, 10.0f))}; // interior atom, far from any boundary

		CrystalStructure comparison;
		comparison.isPeriodic = true;
		comparison.cell = reference.cell;
		// Reference atom 0 (near the corner) relaxes JUST across the periodic a/b boundary; atom 1
		// relaxes by a small in-cell amount.
		comparison.atoms = {
			MakeAtom("C", glm::vec3(2.55f, 2.16f, 10.0f)), // ~ (a + b) away from (0.1,0.05) -> short wrap
			MakeAtom("C", glm::vec3(1.25f, 1.03f, 10.0f))};

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		EXPECT_TRUE(plan.usePeriodicMatching);
		// Both atoms must find SOME candidate (no isolation) - whether that's 2 separate 1x1
		// components or both merged into 1 combined 2x2 component depends on whether the cell
		// (edge ~2.5A) is smaller than the search cutoff (2.0A default), which it is here, so both
		// reference atoms are legitimately reachable from both comparison atoms under PBC. Either
		// shape is physically correct; only isolation (a real match missed) would be a bug.
		EXPECT_TRUE(plan.isolatedReferenceIndices.empty());
		EXPECT_TRUE(plan.isolatedComparisonIndices.empty());
		ASSERT_GE(plan.assignableComponents.size(), 1u);
		ASSERT_LE(plan.assignableComponents.size(), 2u);

		// Construct the assignment(s) from whatever component shape actually came out.
		std::vector<std::vector<int>> assignments;
		for (const LocalMatchingComponent &component : plan.assignableComponents)
			assignments.push_back(std::vector<int>(component.comparisonAtomIndices.size(), -1));
		// Fill each component's assignment with its cheapest per-row candidate (this cell has at
		// most 2 atoms per side, so "cheapest per row" is unambiguous and correct here).
		for (std::size_t componentIndex = 0; componentIndex < plan.assignableComponents.size(); ++componentIndex)
		{
			const LocalMatchingComponent &component = plan.assignableComponents[componentIndex];
			for (std::size_t row = 0; row < component.costMatrix.comparisonCount; ++row)
			{
				int bestColumn = -1;
				float bestCost = kUnmatchedDisplacementCost;
				for (std::size_t column = 0; column < component.costMatrix.referenceCount; ++column)
				{
					const float cost = component.costMatrix.At(row, column);
					if (cost < bestCost)
					{
						bestCost = cost;
						bestColumn = static_cast<int>(column);
					}
				}
				assignments[componentIndex][row] = bestColumn;
			}
		}

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, assignments);
		ASSERT_EQ(result.matches.size(), 2u);
		for (const AtomDisplacement &match : result.matches)
			// Well under the 2.0A match cutoff (real identity), not under some tighter "small
			// relaxation" bound - the corner atom's hand-placed coordinates land its true
			// minimum-image displacement at ~1.01A.
			EXPECT_LT(match.magnitudeAngstrom, 1.5f);
	}

	// --- L: rectangular atom counts -----------------------------------------------------------------

	TEST(StructureComparisonTests, MoreComparisonAtomsThanReferenceAtoms)
	{
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {
			MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(0.3f, 0.0f, 0.0f))}; // second candidate for the SAME reference atom

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_EQ(plan.assignableComponents.size(), 1u);
		EXPECT_EQ(plan.assignableComponents[0].comparisonAtomIndices.size(), 2u);
		EXPECT_EQ(plan.assignableComponents[0].referenceAtomIndices.size(), 1u);

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0, -1}));
		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_EQ(result.matches[0].comparisonAtomIndex, 0u);
		ASSERT_EQ(result.unmatchedComparisonAtoms.size(), 1u);
	}

	TEST(StructureComparisonTests, MoreReferenceAtomsThanComparisonAtoms)
	{
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {
			MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f)),
			MakeAtom("C", glm::vec3(0.3f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f))};

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_EQ(plan.assignableComponents.size(), 1u);
		EXPECT_EQ(plan.assignableComponents[0].comparisonAtomIndices.size(), 1u);
		EXPECT_EQ(plan.assignableComponents[0].referenceAtomIndices.size(), 2u);

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		ASSERT_EQ(result.matches.size(), 1u);
		ASSERT_EQ(result.unmatchedReferenceAtomIndices.size(), 1u);
		EXPECT_EQ(result.unmatchedReferenceAtomIndices[0], 1u);
	}

	// --- lattice-mismatch policy (carried over from the previous implementation) ----------------------

	TEST(StructureComparisonTests, MismatchedLatticesProceedWithWarningInsteadOfFailing)
	{
		// The user picks which two files to compare - a lattice mismatch is a heads-up, not a
		// reason to refuse the whole comparison.
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.cell.vectors[0] = glm::vec3(200.0f, 0.0f, 0.0f); // different a-vector
		comparison.atoms = {MakeAtom("C", glm::vec3(0.1f, 0.0f, 0.0f))};

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		EXPECT_FALSE(plan.usePeriodicMatching);
		EXPECT_FALSE(plan.latticeMismatchWarning.empty());
		ASSERT_EQ(plan.assignableComponents.size(), 1u);

		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		EXPECT_FALSE(result.latticeMismatchWarning.empty());
	}

	TEST(StructureComparisonTests, MatchedLatticesProduceNoWarning)
	{
		CrystalStructure reference;
		reference.cell = MakeLargeCubicCell();
		reference.atoms = {MakeAtom("C", glm::vec3(0.0f, 0.0f, 0.0f))};

		CrystalStructure comparison;
		comparison.cell = reference.cell;
		comparison.atoms = {MakeAtom("C", glm::vec3(0.1f, 0.0f, 0.0f))};

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		EXPECT_TRUE(plan.latticeMismatchWarning.empty());
		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		EXPECT_TRUE(result.latticeMismatchWarning.empty());
	}

	TEST(StructureComparisonTests, MismatchedLatticesDoNotWrapByTheWrongCell)
	{
		// Regression: a vacuum-thickness convergence test compares two files with the same in-plane
		// (a, b) vectors but a different c vector. An atom near z=0 in both files is a genuine close
		// match - periodic-wrapping it by the REFERENCE cell's (small) c vector must NOT be applied
		// (it isn't the same cell), or the reported displacement would jump by nearly a whole cell
		// length instead of reporting the true short distance.
		CrystalStructure reference;
		reference.isPeriodic = true;
		reference.cell = MakeLargeCubicCell();
		reference.cell.vectors[2] = glm::vec3(0.0f, 0.0f, 1.0f); // small c (reference cell)
		reference.atoms = {MakeAtom("C", glm::vec3(50.0f, 50.0f, 0.05f))};

		CrystalStructure comparison;
		comparison.isPeriodic = true;
		comparison.cell = reference.cell;
		comparison.cell.vectors[2] = glm::vec3(0.0f, 0.0f, 20.0f); // much larger c (vacuum test)
		comparison.atoms = {MakeAtom("C", glm::vec3(50.0f, 50.0f, 0.15f))};

		const LocalMatchingPlan plan = BuildLocalMatchingPlan(reference, comparison, MakeCarbonOxygenTable());
		ASSERT_FALSE(plan.usePeriodicMatching);
		ASSERT_EQ(plan.assignableComponents.size(), 1u);
		const StructureComparisonResult result =
			BuildComparisonResultFromLocalPlan(reference, comparison, plan, Single({0}));
		ASSERT_EQ(result.matches.size(), 1u);
		EXPECT_NEAR(result.matches[0].magnitudeAngstrom, 0.1f, 1e-4f);
		EXPECT_NEAR(result.matches[0].comparisonPositionWrapped.z, 0.15f, 1e-4f);
	}

	// --- PeriodicGeometry: MinimumImageCartesianDelta mutation-bug regression -------------------------

	TEST(PeriodicGeometryTests, MinimumImageCartesianDeltaMatchesBruteForceOnSkewedCell)
	{
		// Regression for a confirmed 2026-08-28 bug: the search must evaluate every candidate shift
		// against the ORIGINAL (a - b), never against whichever shift currently looks best -
		// mutating the running "best" and using IT as the base for later candidates silently
		// evaluates them relative to an already-shifted point. This input was found by a randomized
		// search specifically because it diverges under that mutating version (confirmed offline: a
		// naive "chain the running best" implementation returns a norm of ~5.07 here instead of the
		// true minimum-image norm ~3.90). Brute-forces the same 27-candidate search directly (base
		// fixed by construction) as an independent reference and requires an exact match.
		glm::mat3 latticeMatrix(0.0f);
		latticeMatrix[0] = glm::vec3(6.0f, 0.0f, 0.0f);
		latticeMatrix[1] = glm::vec3(2.5f, 5.0f, 0.0f); // skewed b vector (non-orthogonal to a)
		latticeMatrix[2] = glm::vec3(0.0f, 0.0f, 12.0f);

		const glm::vec3 a(0.1249086511f, 0.1071871250f, 0.8787704424f);
		const glm::vec3 b(4.3130128366f, 0.9613655558f, 4.2276337671f);

		glm::vec3 bruteForceBest = a - b;
		float bruteForceBestDistanceSquared = glm::dot(bruteForceBest, bruteForceBest);
		for (int dx = -1; dx <= 1; ++dx)
			for (int dy = -1; dy <= 1; ++dy)
				for (int dz = -1; dz <= 1; ++dz)
				{
					if (dx == 0 && dy == 0 && dz == 0)
						continue;
					const glm::vec3 shiftVector = latticeMatrix[0] * static_cast<float>(dx) +
						latticeMatrix[1] * static_cast<float>(dy) + latticeMatrix[2] * static_cast<float>(dz);
					const glm::vec3 candidate = (a - b) + shiftVector; // always relative to the ORIGINAL delta
					const float distanceSquared = glm::dot(candidate, candidate);
					if (distanceSquared < bruteForceBestDistanceSquared)
					{
						bruteForceBestDistanceSquared = distanceSquared;
						bruteForceBest = candidate;
					}
				}

		const glm::vec3 actual = MinimumImageCartesianDelta(latticeMatrix, a, b);
		EXPECT_NEAR(actual.x, bruteForceBest.x, 1e-3f);
		EXPECT_NEAR(actual.y, bruteForceBest.y, 1e-3f);
		EXPECT_NEAR(actual.z, bruteForceBest.z, 1e-3f);
		// Sanity: this specific input's true minimum-image distance is ~3.90, not the ~5.07 a
		// mutating search would return - fail loudly (not just "doesn't match brute force") if this
		// ever regresses.
		EXPECT_NEAR(std::sqrt(bruteForceBestDistanceSquared), 3.902f, 0.01f);
	}
} // namespace DefectStudio::Tests
