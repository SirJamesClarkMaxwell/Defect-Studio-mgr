#include <gtest/gtest.h>

#include "IO/StructureToRenderer.hpp"

#include <unordered_map>

namespace DefectStudio::Tests
{
	TEST(StructureToRendererTests, ConvertsCrystalStructureToRendererData)
	{
		CrystalStructure structure;
		structure.name = "Carbon pair";
		structure.cell.vectors = {
			glm::vec3(2.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 2.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 2.0f)};
		structure.atoms = {
			AtomSite{"C", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f), 0},
			AtomSite{"C", glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f), 1}};

		AtomStyleTable atomStyleTable;
		std::unordered_map<std::string, AtomRenderStyle> styles;
		styles.emplace("C", AtomRenderStyle{glm::vec3(0.1f, 0.2f, 0.3f), 0.44f});
		atomStyleTable.ReplaceStyles(std::move(styles), VacancyRenderStyle{});

		ElementPropertiesTable elementPropertiesTable;
		std::unordered_map<std::string, ElementProperties> properties;
		properties.emplace("C", ElementProperties{6, 12.0f, 0.80f, 1.70f});
		elementPropertiesTable.ReplaceData(std::move(properties));

		const Path sourcePath("POSCAR");
		const RendererStructureData rendererData = ConvertCrystalStructureToRendererData(
			structure,
			sourcePath,
			"Renderer name",
			atomStyleTable,
			elementPropertiesTable);

		EXPECT_EQ(rendererData.name, "Renderer name");
		EXPECT_EQ(rendererData.sourcePath.String(), sourcePath.String());
		ASSERT_EQ(rendererData.atoms.size(), 2u);
		EXPECT_EQ(rendererData.atoms[0].element, "C");
		EXPECT_EQ(rendererData.atoms[0].cartesianPosition, glm::vec3(0.0f, 0.0f, 0.0f));
		EXPECT_EQ(rendererData.atoms[0].color, glm::vec3(0.1f, 0.2f, 0.3f));
		EXPECT_NEAR(rendererData.atoms[0].radius, 0.44f, 1e-5f);
		EXPECT_EQ(rendererData.cellEdges.size(), 12u);
		ASSERT_EQ(rendererData.bonds.size(), 1u);
		EXPECT_EQ(rendererData.bonds[0].firstAtomIndex, 0u);
		EXPECT_EQ(rendererData.bonds[0].secondAtomIndex, 1u);
		EXPECT_NEAR(rendererData.reciprocalLattice[0].x, 0.5f, 1e-5f);
		EXPECT_NEAR(rendererData.reciprocalLattice[1].y, 0.5f, 1e-5f);
		EXPECT_NEAR(rendererData.reciprocalLattice[2].z, 0.5f, 1e-5f);
	}
} // namespace DefectStudio::Tests
