#include <gtest/gtest.h>

#include "Domain/Symmetry/PointGroupBasis.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] AtomSite MakeAtom(const std::string &species, const glm::vec3 &position)
		{
			AtomSite atom;
			atom.species = species;
			atom.position = position;
			return atom;
		}

		[[nodiscard]] CrystalStructure MakeCubicCell(float edge)
		{
			CrystalStructure structure;
			structure.cell.vectors = {glm::vec3(edge, 0.0f, 0.0f), glm::vec3(0.0f, edge, 0.0f), glm::vec3(0.0f, 0.0f, edge)};
			structure.isPeriodic = true;
			return structure;
		}
	} // namespace

	TEST(PointGroupBasisTests, NonPeriodicUsesPlainCartesianCentredPositions)
	{
		CrystalStructure structure;
		structure.isPeriodic = false;
		structure.atoms = {MakeAtom("C", glm::vec3(1.0f, 2.0f, 3.0f)), MakeAtom("N", glm::vec3(9.0f, 2.0f, 3.0f))};

		const Result<SelectionBasis> basis = BuildSelectionBasis(structure, {1, 0}, glm::dvec3(1.0, 2.0, 3.0));
		ASSERT_TRUE(basis) << basis.Error().technicalDetails;
		EXPECT_FALSE(basis->periodicUnwrapped);
		ASSERT_EQ(basis->sites.size(), 2u);
		EXPECT_EQ(basis->atomIndices, (std::vector<std::size_t>{1, 0}));
		EXPECT_EQ(basis->sites[0].label, "N1");
		EXPECT_EQ(basis->sites[0].element, "N");
		EXPECT_NEAR(basis->sites[0].position.x, 8.0, 1e-6); // no minimum image without periodicity
		EXPECT_NEAR(basis->sites[1].position.x, 0.0, 1e-6);
	}

	TEST(PointGroupBasisTests, PeriodicUnwrapsByMinimumImageAroundCentre)
	{
		CrystalStructure structure = MakeCubicCell(10.0f);
		structure.atoms = {MakeAtom("C", glm::vec3(0.5f, 5.0f, 5.0f)), MakeAtom("C", glm::vec3(9.5f, 5.0f, 5.0f))};

		const Result<SelectionBasis> basis = BuildSelectionBasis(structure, {0, 1}, glm::dvec3(0.0, 5.0, 5.0));
		ASSERT_TRUE(basis) << basis.Error().technicalDetails;
		EXPECT_TRUE(basis->periodicUnwrapped);
		EXPECT_NEAR(basis->sites[0].position.x, 0.5, 1e-5);
		EXPECT_NEAR(basis->sites[1].position.x, -0.5, 1e-5); // 9.5 wraps to -0.5 across the face
		EXPECT_NEAR(basis->sites[1].position.y, 0.0, 1e-5);
	}

	TEST(PointGroupBasisTests, HashIsStableAndTracksGeometry)
	{
		CrystalStructure structure = MakeCubicCell(10.0f);
		structure.atoms = {MakeAtom("C", glm::vec3(1.0f)), MakeAtom("N", glm::vec3(2.0f))};

		const Result<SelectionBasis> first = BuildSelectionBasis(structure, {0, 1}, glm::dvec3(1.5));
		const Result<SelectionBasis> again = BuildSelectionBasis(structure, {0, 1}, glm::dvec3(1.5));
		ASSERT_TRUE(first);
		ASSERT_TRUE(again);
		EXPECT_EQ(first->hash, again->hash);

		structure.atoms[1].position.x += 0.01f;
		const Result<SelectionBasis> moved = BuildSelectionBasis(structure, {0, 1}, glm::dvec3(1.5));
		ASSERT_TRUE(moved);
		EXPECT_NE(first->hash, moved->hash);
	}

	TEST(PointGroupBasisTests, RejectsEmptySelection)
	{
		const Result<SelectionBasis> basis = BuildSelectionBasis(MakeCubicCell(10.0f), {}, glm::dvec3(0.0));
		ASSERT_FALSE(basis);
		EXPECT_EQ(basis.Error().code, "symmetry.basis.empty_selection");
	}

	TEST(PointGroupBasisTests, RejectsIndexOutOfRange)
	{
		CrystalStructure structure = MakeCubicCell(10.0f);
		structure.atoms = {MakeAtom("C", glm::vec3(1.0f))};
		const Result<SelectionBasis> basis = BuildSelectionBasis(structure, {0, 3}, glm::dvec3(0.0));
		ASSERT_FALSE(basis);
		EXPECT_EQ(basis.Error().code, "symmetry.basis.index_out_of_range");
	}

	TEST(PointGroupBasisTests, RejectsSingularLatticeWhenPeriodic)
	{
		CrystalStructure structure = MakeCubicCell(10.0f);
		structure.cell.vectors[2] = structure.cell.vectors[0]; // two identical lattice vectors
		structure.atoms = {MakeAtom("C", glm::vec3(1.0f))};
		const Result<SelectionBasis> basis = BuildSelectionBasis(structure, {0}, glm::dvec3(0.0));
		ASSERT_FALSE(basis);
		EXPECT_EQ(basis.Error().code, "symmetry.basis.singular_lattice");
	}
} // namespace DefectStudio::Tests
