#include <gtest/gtest.h>

#include "Domain/Defects/DefectModel.hpp"

namespace DefectStudio::Tests
{
	TEST(VacancySiteTests, MakeVacancySiteFillsFractionalFromTheCell)
	{
		CrystalStructure structure;
		structure.cell.vectors = {
			glm::vec3(4.0f, 0.0f, 0.0f), glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 0.0f, 8.0f)};

		const VacancySite site = MakeVacancySite(structure, glm::vec3(1.0f, 1.0f, 2.0f), "C");

		EXPECT_EQ(site.position, glm::vec3(1.0f, 1.0f, 2.0f));
		EXPECT_NEAR(site.fractional.x, 0.25f, 1e-6f);
		EXPECT_NEAR(site.fractional.y, 0.5f, 1e-6f);
		EXPECT_NEAR(site.fractional.z, 0.25f, 1e-6f);
		EXPECT_EQ(site.sourceSpecies, "C");
		EXPECT_EQ(site.index, -1);
		EXPECT_TRUE(site.label.empty());
		EXPECT_EQ(site.GetLabel(), "V_C");
	}

	TEST(VacancySiteTests, MakeVacancySiteWithoutSpeciesIsPlainV)
	{
		CrystalStructure structure;
		structure.cell.vectors = {
			glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};

		const VacancySite site = MakeVacancySite(structure, glm::vec3(0.5f));

		EXPECT_EQ(site.GetLabel(), "V");
	}
} // namespace DefectStudio::Tests
