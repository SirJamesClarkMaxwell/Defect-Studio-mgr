#include <gtest/gtest.h>

#include <unordered_map>

#include "Renderer/StructureRendererDataBuilder.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		[[nodiscard]] CrystalStructure StructureWithVacancies()
		{
			CrystalStructure structure;
			structure.cell.vectors = {
				glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, 0.0f, 3.0f)};
			structure.atoms = {AtomSite{"N", glm::vec3(0.0f), glm::vec3(0.0f), 0}};
			VacancySite carbon;
			carbon.position = glm::vec3(1.0f, 1.0f, 1.0f);
			carbon.sourceSpecies = "C";
			VacancySite labelled;
			labelled.position = glm::vec3(2.0f, 0.0f, 0.0f);
			labelled.label = "V_custom";
			structure.vacancies = {carbon, labelled};
			return structure;
		}
	} // namespace

	TEST(VacancyRendererDataTests, VacanciesAreCopiedInOrderWithTheVacancyStyle)
	{
		VacancyRenderStyle style;
		style.color = glm::vec3(0.1f, 0.6f, 0.3f);
		style.displayRadius = 0.6f;
		style.opacity = 0.5f;
		style.renderMode = VacancyRenderMode::Wireframe;
		style.dashCount = 9;
		style.ringWidth = 0.07f;
		AtomStyleTable table;
		table.ReplaceStyles({}, style);

		const RendererStructureData data =
			BuildRendererStructureData(StructureWithVacancies(), Path("POSCAR"), "NV", table);

		ASSERT_EQ(data.vacancies.size(), 2u);
		EXPECT_EQ(data.vacancies[0].cartesianPosition, glm::vec3(1.0f, 1.0f, 1.0f));
		EXPECT_EQ(data.vacancies[0].label, "V_C");
		EXPECT_EQ(data.vacancies[1].label, "V_custom");
		for (const RendererVacancyData &vacancy : data.vacancies)
		{
			EXPECT_EQ(vacancy.color, style.color);
			EXPECT_FLOAT_EQ(vacancy.radius, 0.6f);
			EXPECT_FLOAT_EQ(vacancy.opacity, 0.5f);
			EXPECT_EQ(vacancy.renderMode, VacancyRenderMode::Wireframe);
			EXPECT_EQ(vacancy.dashCount, 9);
			EXPECT_FLOAT_EQ(vacancy.ringWidth, 0.07f);
		}
	}

	TEST(VacancyRendererDataTests, NoVacanciesNoMarkers)
	{
		CrystalStructure structure = StructureWithVacancies();
		structure.vacancies.clear();

		const RendererStructureData data =
			BuildRendererStructureData(structure, Path("POSCAR"), "NV", AtomStyleTable{});

		EXPECT_TRUE(data.vacancies.empty());
	}

	// The table's copies share their data (AtomStyleTable.hpp), so a vacancy-style edit through one
	// copy must be visible through every other, as an element edit through SetStyle is.
	TEST(VacancyRendererDataTests, SetVacancyStyleIsSharedBetweenCopies)
	{
		AtomStyleTable table;
		const AtomStyleTable copy = table;
		VacancyRenderStyle style;
		style.dashCount = 0;
		style.color = glm::vec3(1.0f, 0.0f, 0.0f);

		table.SetVacancyStyle(style);

		EXPECT_EQ(copy.GetVacancyStyle().dashCount, 0);
		EXPECT_EQ(copy.GetVacancyStyle().color, glm::vec3(1.0f, 0.0f, 0.0f));
	}
} // namespace DefectStudio::Tests
