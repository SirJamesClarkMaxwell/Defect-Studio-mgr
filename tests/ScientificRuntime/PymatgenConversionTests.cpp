#include <gtest/gtest.h>

#include "ScientificRuntime/Python/PymatgenConversion.hpp"

namespace DefectStudio::Tests
{
	TEST(PymatgenConversionTests, ConvertsPymatgenStructureToCrystalStructure)
	{
		PymatgenStructureData pymatgen;
		pymatgen.reducedFormula = "Si";
		pymatgen.lattice[0] = glm::vec3(2.0f, 0.0f, 0.0f);
		pymatgen.lattice[1] = glm::vec3(0.0f, 3.0f, 0.0f);
		pymatgen.lattice[2] = glm::vec3(0.0f, 0.0f, 4.0f);
		pymatgen.sites.push_back(PymatgenStructureSite{
			"Si",
			glm::vec3(0.25f, 0.50f, 0.75f),
			glm::vec3(0.50f, 1.50f, 3.00f)});

		const CrystalStructure structure = ConvertPymatgenStructureToCrystalStructure(pymatgen);

		EXPECT_EQ(structure.name, "Si");
		EXPECT_EQ(structure.cell.vectors[0], glm::vec3(2.0f, 0.0f, 0.0f));
		EXPECT_EQ(structure.cell.vectors[1], glm::vec3(0.0f, 3.0f, 0.0f));
		EXPECT_EQ(structure.cell.vectors[2], glm::vec3(0.0f, 0.0f, 4.0f));
		ASSERT_EQ(structure.atoms.size(), 1u);
		EXPECT_EQ(structure.atoms[0].species, "Si");
		EXPECT_EQ(structure.atoms[0].fractional, glm::vec3(0.25f, 0.50f, 0.75f));
		EXPECT_EQ(structure.atoms[0].position, glm::vec3(0.50f, 1.50f, 3.00f));
		EXPECT_EQ(structure.atoms[0].index, 0);
	}
} // namespace DefectStudio::Tests
