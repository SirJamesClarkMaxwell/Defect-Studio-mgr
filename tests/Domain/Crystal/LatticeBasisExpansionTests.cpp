#include <gtest/gtest.h>

#include <algorithm>

#include "Domain/Crystal/LatticeBasisExpansion.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] AtomSite MakeBasisRow(std::string species, glm::vec3 fractional)
		{
			AtomSite row;
			row.species = std::move(species);
			row.fractional = fractional;
			return row;
		}

		[[nodiscard]] bool ContainsSite(
			const std::vector<AtomSite> &atoms,
			const std::string &species,
			glm::vec3 fractional)
		{
			return std::any_of(atoms.begin(), atoms.end(), [&](const AtomSite &atom) {
				return atom.species == species &&
					   glm::length(atom.fractional - fractional) < 1e-4f;
			});
		}
	} // namespace

	TEST(LatticeBasisExpansionTests, PrimitiveLeavesTheMotifUntouched)
	{
		const std::vector<AtomSite> basis = {MakeBasisRow("Si", {0.0f, 0.0f, 0.0f})};

		const std::vector<AtomSite> expanded =
			ExpandBasisOverLattice(basis, BravaisCenteringPreset::Primitive);

		ASSERT_EQ(expanded.size(), 1u);
		EXPECT_EQ(expanded[0].species, "Si");
		EXPECT_NEAR(glm::length(expanded[0].fractional), 0.0f, 1e-5f);
	}

	TEST(LatticeBasisExpansionTests, BodyCenteredIronGivesTwoAtoms)
	{
		const std::vector<AtomSite> basis = {MakeBasisRow("Fe", {0.0f, 0.0f, 0.0f})};

		const std::vector<AtomSite> expanded =
			ExpandBasisOverLattice(basis, BravaisCenteringPreset::BodyCentered);

		ASSERT_EQ(expanded.size(), 2u);
		EXPECT_TRUE(ContainsSite(expanded, "Fe", {0.0f, 0.0f, 0.0f}));
		EXPECT_TRUE(ContainsSite(expanded, "Fe", {0.5f, 0.5f, 0.5f}));
	}

	// The case the whole lattice-vs-basis separation exists for: 2 typed rows must become the
	// 8 atoms of the diamond conventional cell.
	TEST(LatticeBasisExpansionTests, DiamondIsFccTimesTwoCarbons)
	{
		const std::vector<AtomSite> basis = {
			MakeBasisRow("C", {0.0f, 0.0f, 0.0f}),
			MakeBasisRow("C", {0.25f, 0.25f, 0.25f})};

		const std::vector<AtomSite> expanded =
			ExpandBasisOverLattice(basis, BravaisCenteringPreset::FaceCentered);

		ASSERT_EQ(expanded.size(), 8u);
		EXPECT_TRUE(ContainsSite(expanded, "C", {0.5f, 0.5f, 0.0f}));
		EXPECT_TRUE(ContainsSite(expanded, "C", {0.75f, 0.75f, 0.25f}));
		EXPECT_TRUE(ContainsSite(expanded, "C", {0.25f, 0.75f, 0.75f}));
	}

	TEST(LatticeBasisExpansionTests, RocksaltKeepsBothSpecies)
	{
		const std::vector<AtomSite> basis = {
			MakeBasisRow("Na", {0.0f, 0.0f, 0.0f}),
			MakeBasisRow("Cl", {0.5f, 0.5f, 0.5f})};

		const std::vector<AtomSite> expanded =
			ExpandBasisOverLattice(basis, BravaisCenteringPreset::FaceCentered);

		ASSERT_EQ(expanded.size(), 8u);
		EXPECT_EQ(std::count_if(expanded.begin(), expanded.end(), [](const AtomSite &atom) {
					  return atom.species == "Na";
				  }),
			4);
		EXPECT_TRUE(ContainsSite(expanded, "Cl", {0.0f, 0.0f, 0.5f}));
	}

	TEST(LatticeBasisExpansionTests, BaseCenteredWrapsCoordinatesIntoTheUnitCell)
	{
		const std::vector<AtomSite> basis = {MakeBasisRow("X", {0.75f, 0.75f, 0.0f})};

		const std::vector<AtomSite> expanded =
			ExpandBasisOverLattice(basis, BravaisCenteringPreset::BaseCentered);

		ASSERT_EQ(expanded.size(), 2u);
		EXPECT_TRUE(ContainsSite(expanded, "X", {0.25f, 0.25f, 0.0f})); // 0.75 + 0.5 wrapped
	}

	// Expansion is basis-major, so `index / translationCount` must map an expanded atom back to
	// the row the user typed.
	TEST(LatticeBasisExpansionTests, ExpandedAtomsStayGroupedByBasisRow)
	{
		const std::vector<AtomSite> basis = {
			MakeBasisRow("A", {0.0f, 0.0f, 0.0f}),
			MakeBasisRow("B", {0.1f, 0.2f, 0.3f})};

		const std::vector<AtomSite> expanded =
			ExpandBasisOverLattice(basis, BravaisCenteringPreset::FaceCentered);

		ASSERT_EQ(expanded.size(), 8u);
		for (std::size_t i = 0; i < expanded.size(); ++i)
		{
			EXPECT_EQ(expanded[i].index, static_cast<int>(i));
			EXPECT_EQ(expanded[i].species, i < 4u ? "A" : "B");
		}
	}

	TEST(LatticeBasisExpansionTests, CoincidentAtomsAreReportedNotRemoved)
	{
		// Two basis rows that differ by a centering translation expand onto the same four sites:
		// the F translations form a group, so a single row never collides with itself, but a second
		// row at (1/2,1/2,0) reproduces the first row exactly.
		const std::vector<AtomSite> basis = {
			MakeBasisRow("X", {0.0f, 0.0f, 0.0f}),
			MakeBasisRow("X", {0.5f, 0.5f, 0.0f})};

		const std::vector<AtomSite> expanded =
			ExpandBasisOverLattice(basis, BravaisCenteringPreset::FaceCentered);
		const std::vector<std::size_t> coincident = FindCoincidentAtomIndices(expanded);

		EXPECT_EQ(expanded.size(), 8u); // nothing silently deduplicated
		EXPECT_EQ(coincident.size(), 8u);
	}

	TEST(LatticeBasisExpansionTests, CoincidenceDetectionIsPeriodic)
	{
		const std::vector<AtomSite> atoms = {
			MakeBasisRow("X", {0.999f, 0.0f, 0.0f}),
			MakeBasisRow("X", {0.0f, 0.0f, 0.0f})};

		EXPECT_EQ(FindCoincidentAtomIndices(atoms, 1e-2f).size(), 2u);
		EXPECT_TRUE(FindCoincidentAtomIndices(atoms, 1e-4f).empty());
	}

	TEST(LatticeBasisExpansionTests, DistinctSitesAreNotFlagged)
	{
		const std::vector<AtomSite> basis = {
			MakeBasisRow("C", {0.0f, 0.0f, 0.0f}),
			MakeBasisRow("C", {0.25f, 0.25f, 0.25f})};

		const std::vector<AtomSite> expanded =
			ExpandBasisOverLattice(basis, BravaisCenteringPreset::FaceCentered);

		EXPECT_TRUE(FindCoincidentAtomIndices(expanded).empty());
	}
} // namespace DefectStudio::Tests
