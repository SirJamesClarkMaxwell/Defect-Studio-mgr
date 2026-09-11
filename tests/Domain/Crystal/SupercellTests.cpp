#include <gtest/gtest.h>

#include "Domain/Crystal/Supercell.hpp"

namespace DefectStudio::Tests
{
	TEST(SupercellTests, DiagonalDoublingAlongAProducesTwiceTheAtomsAtCorrectPositions)
	{
		CrystalStructure unitCell;
		unitCell.cell.vectors = { glm::vec3(2, 0, 0), glm::vec3(0, 2, 0), glm::vec3(0, 0, 2) };
		unitCell.atoms = { AtomSite{"C", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };

		const SupercellMatrix transform = SupercellMatrix::Diagonal(2, 1, 1);
		const Result<CrystalStructure> result = BuildSupercell(unitCell, transform);

		ASSERT_TRUE(result);
		EXPECT_EQ(result->atoms.size(), 2u);
		EXPECT_NEAR(glm::length(result->cell.vectors[0]), 4.0f, 1e-5f);

		bool foundOrigin = false, foundShifted = false;
		for (const AtomSite &atom : result->atoms)
		{
			if (glm::length(atom.position - glm::vec3(0, 0, 0)) < 1e-4f) foundOrigin = true;
			if (glm::length(atom.position - glm::vec3(2, 0, 0)) < 1e-4f) foundShifted = true;
		}
		EXPECT_TRUE(foundOrigin);
		EXPECT_TRUE(foundShifted);
	}

	TEST(SupercellTests, AtomCountMatchesDeterminantTimesUnitCellAtomsForShearMatrix)
	{
		CrystalStructure unitCell;
		unitCell.cell.vectors = { glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) };
		unitCell.atoms = {
			AtomSite{"Si", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0},
			AtomSite{"Si", glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.5f, 0.5f, 0.5f), 1}};

		SupercellMatrix transform;
		transform.rows = { glm::ivec3(2, 1, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 1) };
		ASSERT_EQ(transform.Determinant(), 2);

		const Result<CrystalStructure> result = BuildSupercell(unitCell, transform);
		ASSERT_TRUE(result);
		EXPECT_EQ(result->atoms.size(), 4u); // 2 basis atoms * determinant 2
		EXPECT_FALSE(transform.IsDiagonal());
	}

	TEST(SupercellTests, RejectsNonPositiveDeterminant)
	{
		CrystalStructure unitCell;
		unitCell.cell.vectors = { glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) };
		unitCell.atoms = { AtomSite{"C", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };

		SupercellMatrix transform;
		transform.rows = { glm::ivec3(1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 0) }; // det = 0

		const Result<CrystalStructure> result = BuildSupercell(unitCell, transform);
		EXPECT_FALSE(result);
	}

	TEST(SupercellTests, DiagonalHelperIsDiagonalTrueOnlyForZeroOffDiagonalEntries)
	{
		EXPECT_TRUE(SupercellMatrix::Diagonal(3, 2, 1).IsDiagonal());
		SupercellMatrix sheared;
		sheared.rows = { glm::ivec3(2, 1, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 1) };
		EXPECT_FALSE(sheared.IsDiagonal());
	}

	TEST(SupercellTests, GeneratedBondsAreEmptyCallerMustRegenerate)
	{
		CrystalStructure unitCell;
		unitCell.cell.vectors = { glm::vec3(2, 0, 0), glm::vec3(0, 2, 0), glm::vec3(0, 0, 2) };
		unitCell.atoms = { AtomSite{"C", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };
		unitCell.bonds = { Bond{0, 0, 1.0f, BondOrigin::Auto, true, glm::ivec3(0)} }; // stale, must not carry over

		const Result<CrystalStructure> result = BuildSupercell(unitCell, SupercellMatrix::Diagonal(2, 1, 1));
		ASSERT_TRUE(result);
		EXPECT_TRUE(result->bonds.empty());
	}

	// The bonds are dropped (above) but the RULES that regenerate them are not: without this the
	// supercell re-bonded at the stock global scale, so every per-pair cutoff the user had set on
	// the unit cell was silently ignored in the supercell view and in the exported file.
	TEST(SupercellTests, BondSettingsCarryOverToTheSupercell)
	{
		CrystalStructure unitCell;
		unitCell.cell.vectors = { glm::vec3(2, 0, 0), glm::vec3(0, 2, 0), glm::vec3(0, 0, 2) };
		unitCell.atoms = { AtomSite{"C", glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 0} };
		unitCell.bondSettings.globalCutoffScale = 1.42f;
		unitCell.bondSettings.perPairCutoffOverride["C-C"] = 0.75f;

		const Result<CrystalStructure> result = BuildSupercell(unitCell, SupercellMatrix::Diagonal(2, 1, 1));
		ASSERT_TRUE(result);
		EXPECT_FLOAT_EQ(result->bondSettings.globalCutoffScale, 1.42f);
		ASSERT_TRUE(result->bondSettings.perPairCutoffOverride.contains("C-C"));
		EXPECT_FLOAT_EQ(result->bondSettings.perPairCutoffOverride.at("C-C"), 0.75f);
	}
} // namespace DefectStudio::Tests
