#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

#include "IO/AtomStyleIO.hpp"
#include "IO/SceneObjectsIO.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneObjectsVacanciesIOTests, VacanciesRoundTripBesideObjects)
	{
		SceneObjectsFile file;
		PersistedStructureSceneObjects entry;
		entry.structureKey = "structures/NV/POSCAR";
		PersistedVacancy carbon;
		carbon.position = glm::vec3(1.0f, 2.0f, 3.0f);
		carbon.fractional = glm::vec3(0.1f, 0.2f, 0.3f);
		carbon.sourceSpecies = "C";
		carbon.label = "V_C";
		carbon.index = 12;
		PersistedVacancy bare;
		bare.position = glm::vec3(-1.0f, 0.0f, 0.5f);
		entry.vacancies = {carbon, bare};
		file.structures.push_back(entry);

		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), loaded, warnings, error)) << error;

		EXPECT_TRUE(warnings.empty());
		EXPECT_EQ(loaded.formatVersion, SceneObjectsIO::kFormatVersion);
		ASSERT_EQ(loaded.structures.size(), 1u);
		EXPECT_TRUE(loaded.structures[0].objects.empty());
		ASSERT_EQ(loaded.structures[0].vacancies.size(), 2u);
		const PersistedVacancy &first = loaded.structures[0].vacancies[0];
		EXPECT_EQ(first.position, carbon.position);
		EXPECT_NEAR(first.fractional.y, 0.2f, 1e-6f);
		EXPECT_EQ(first.sourceSpecies, "C");
		EXPECT_EQ(first.label, "V_C");
		EXPECT_EQ(first.index, 12);
		const PersistedVacancy &second = loaded.structures[0].vacancies[1];
		EXPECT_EQ(second.position, bare.position);
		EXPECT_TRUE(second.sourceSpecies.empty());
		EXPECT_TRUE(second.label.empty());
	}

	TEST(SceneObjectsVacanciesIOTests, DefectFrameRoundTripsAndIsOptional)
	{
		SceneObjectsFile file;
		PersistedStructureSceneObjects entry;
		entry.structureKey = "structures/NV/POSCAR";
		PersistedDefectFrame frame;
		frame.origin = glm::vec3(1.0f, 2.0f, 3.0f);
		frame.x = glm::vec3(0.0f, 1.0f, 0.0f);
		frame.y = glm::vec3(0.0f, 0.0f, 1.0f);
		frame.z = glm::vec3(1.0f, 0.0f, 0.0f);
		frame.axisLength = 2.75f;
		frame.axisWidth = 4.0f;
		frame.negativeAxes = false;
		frame.shown = false;
		entry.defectFrame = frame;
		file.structures.push_back(entry);
		file.structures.push_back({"structures/bulk/POSCAR", {}});

		const std::string text = SceneObjectsIO::Serialize(file);
		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(text, loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.structures.size(), 2u);
		ASSERT_TRUE(loaded.structures[0].defectFrame.has_value());
		EXPECT_EQ(loaded.structures[0].defectFrame->origin, frame.origin);
		EXPECT_EQ(loaded.structures[0].defectFrame->x, frame.x);
		EXPECT_EQ(loaded.structures[0].defectFrame->y, frame.y);
		EXPECT_EQ(loaded.structures[0].defectFrame->z, frame.z);
		EXPECT_EQ(loaded.structures[0].defectFrame->axisLength, std::optional<float>(2.75f));
		EXPECT_EQ(loaded.structures[0].defectFrame->axisWidth, std::optional<float>(4.0f));
		EXPECT_EQ(loaded.structures[0].defectFrame->negativeAxes, std::optional<bool>(false));
		EXPECT_EQ(loaded.structures[0].defectFrame->shown, std::optional<bool>(false));
		EXPECT_FALSE(loaded.structures[1].defectFrame.has_value());
		EXPECT_EQ(text.find("defectFrame"), text.rfind("defectFrame"));
	}

	TEST(SceneObjectsVacanciesIOTests, VacancyColourIsOptionalAndRoundTrips)
	{
		SceneObjectsFile file;
		PersistedStructureSceneObjects entry;
		entry.structureKey = "structures/hBN/POSCAR";
		PersistedVacancy boron;
		boron.color = glm::vec3(1.0f, 0.5f, 0.25f);
		boron.hidden = true;
		entry.vacancies = {boron, PersistedVacancy{}};
		file.structures.push_back(entry);

		const std::string text = SceneObjectsIO::Serialize(file);
		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(text, loaded, warnings, error)) << error;
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].vacancies.size(), 2u);
		EXPECT_EQ(loaded.structures[0].vacancies[0].color, boron.color);
		EXPECT_FALSE(loaded.structures[0].vacancies[1].color.has_value());
		EXPECT_TRUE(loaded.structures[0].vacancies[0].hidden);
		EXPECT_FALSE(loaded.structures[0].vacancies[1].hidden);
	}

	TEST(SceneObjectsVacanciesIOTests, NoVacanciesKeyIsNotWritten)
	{
		SceneObjectsFile file;
		PersistedStructureSceneObjects entry;
		entry.structureKey = "structures/NV/POSCAR";
		file.structures.push_back(entry);

		EXPECT_EQ(SceneObjectsIO::Serialize(file).find("vacancies"), std::string::npos);
	}

	TEST(SceneObjectsVacanciesIOTests, FileWithoutVacanciesLoadsNone)
	{
		const char *legacy =
			"formatVersion: 2\n"
			"structures:\n"
			"  - structureKey: structures/NV/POSCAR\n"
			"    objects: []\n";

		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(legacy, loaded, warnings, error)) << error;

		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.structures.size(), 1u);
		EXPECT_TRUE(loaded.structures[0].vacancies.empty());
	}

	TEST(SceneObjectsVacanciesIOTests, VacancyWithoutPositionIsSkippedWithAWarning)
	{
		const char *text =
			"formatVersion: 2\n"
			"structures:\n"
			"  - structureKey: structures/NV/POSCAR\n"
			"    vacancies:\n"
			"      - label: lost\n"
			"      - position: [1, 1, 1]\n";

		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(text, loaded, warnings, error)) << error;

		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].vacancies.size(), 1u);
		EXPECT_EQ(loaded.structures[0].vacancies[0].position, glm::vec3(1.0f));
		ASSERT_EQ(warnings.size(), 1u);
		EXPECT_EQ(warnings[0].code, "scene_objects.entry_skipped");
	}

	TEST(SceneObjectsVacanciesIOTests, VacancyStyleReadsDashesAndRingWidth)
	{
		const std::string text =
			"vacancy:\n"
			"  opacity: 0.5\n"
			"  dash_count: 7\n"
			"  ring_width: 0.08\n"
			"elements: {}\n";

		std::unordered_map<std::string, AtomRenderStyle> styles;
		VacancyRenderStyle vacancy;
		std::string error;
		ASSERT_TRUE(AtomStyleIO::ParseYaml(text, styles, vacancy, error)) << error;

		EXPECT_EQ(vacancy.dashCount, 7);
		EXPECT_FLOAT_EQ(vacancy.ringWidth, 0.08f);
	}

	TEST(SceneObjectsVacanciesIOTests, VacancyStyleWithoutDashKeysKeepsDefaults)
	{
		const std::string text =
			"vacancy:\n"
			"  opacity: 0.5\n"
			"elements: {}\n";

		std::unordered_map<std::string, AtomRenderStyle> styles;
		VacancyRenderStyle vacancy;
		std::string error;
		ASSERT_TRUE(AtomStyleIO::ParseYaml(text, styles, vacancy, error)) << error;

		EXPECT_EQ(vacancy.dashCount, VacancyRenderStyle{}.dashCount);
		EXPECT_FLOAT_EQ(vacancy.ringWidth, VacancyRenderStyle{}.ringWidth);
	}
} // namespace DefectStudio::Tests
