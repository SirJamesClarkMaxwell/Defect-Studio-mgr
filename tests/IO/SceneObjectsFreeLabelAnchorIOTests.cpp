#include <gtest/gtest.h>

#include "IO/SceneObjectsIO.hpp"

namespace DefectStudio::Tests
{
	TEST(SceneObjectsFreeLabelAnchorIOTests, AtomAndVacancyAnchorsRoundTrip)
	{
		PersistedFreeLabel atom;
		atom.text = "C_1";
		atom.position = glm::vec3(1.0f, 2.0f, 3.0f);
		atom.anchorAtoms = {{12, "C", glm::vec3(1.0f)}};
		atom.anchorOffset = glm::vec3(0.0f, 1.0f, 2.0f);
		PersistedFreeLabel vacancy;
		vacancy.text = "V_B";
		vacancy.anchorVacancy = 0;
		vacancy.anchorOffset = glm::vec3(0.5f);
		SceneObjectsFile file;
		file.structures.push_back({"structures/test/POSCAR", {atom, vacancy}});

		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(SceneObjectsIO::Serialize(file), loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 2u);
		const auto &gotAtom = std::get<PersistedFreeLabel>(loaded.structures[0].objects[0]);
		ASSERT_EQ(gotAtom.anchorAtoms.size(), 1u);
		EXPECT_EQ(gotAtom.anchorAtoms[0].index, 12u);
		EXPECT_EQ(gotAtom.anchorAtoms[0].element, "C");
		EXPECT_EQ(gotAtom.anchorAtoms[0].position, glm::vec3(1.0f));
		EXPECT_EQ(gotAtom.anchorOffset, atom.anchorOffset);
		EXPECT_EQ(gotAtom.position, atom.position);
		EXPECT_FALSE(gotAtom.anchorVacancy);
		const auto &gotVacancy = std::get<PersistedFreeLabel>(loaded.structures[0].objects[1]);
		EXPECT_EQ(gotVacancy.anchorVacancy, std::optional<int>(0));
		EXPECT_TRUE(gotVacancy.anchorAtoms.empty());
		EXPECT_EQ(gotVacancy.anchorOffset, vacancy.anchorOffset);
	}

	TEST(SceneObjectsFreeLabelAnchorIOTests, LegacyLabelHasNoAnchor)
	{
		const std::string yaml = R"(formatVersion: 1
structures:
  - structureKey: structures/test/POSCAR
    objects:
      - kind: FreeLabel
        text: old label
        position: [1, 2, 3]
)";
		SceneObjectsFile loaded;
		std::vector<StructuredError> warnings;
		std::string error;
		ASSERT_TRUE(SceneObjectsIO::Parse(yaml, loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.structures.size(), 1u);
		ASSERT_EQ(loaded.structures[0].objects.size(), 1u);
		const auto &label = std::get<PersistedFreeLabel>(loaded.structures[0].objects[0]);
		EXPECT_TRUE(label.anchorAtoms.empty());
		EXPECT_FALSE(label.anchorVacancy);
		EXPECT_EQ(label.anchorOffset, glm::vec3(0.0f));
		EXPECT_EQ(label.position, glm::vec3(1.0f, 2.0f, 3.0f));
	}
} // namespace DefectStudio::Tests
