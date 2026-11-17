#include <gtest/gtest.h>

#include "IO/PhononPanelSettingsIO.hpp"

namespace DefectStudio::Tests
{
	TEST(PhononDefectFolderSettingsTests, OptionalFolderAndVisibilityKeysRoundTripWithoutChangingVersion)
	{
		const auto project = Path::FromUtf8("D:/projects/phonons");
		PhononPanelSettings settings, loaded;
		settings.defectRoots = {project / "systems", Path::FromUtf8("P:/PL-spectra")};
		settings.cases.push_back({"case-1", "dimer", {}, {}});
		settings.cases[0].sourceDirectory = project / "systems/dimer";
		settings.cases[0].hidden = true;
		settings.cases.push_back({"case-2", "manual", {}, {}});
		settings.activeCaseId = "case-1";
		const auto text = PhononPanelSettingsIO::Serialize(settings, project);
		EXPECT_NE(text.find("formatVersion: 2"), std::string::npos);
		EXPECT_NE(text.find("sourceDir: systems/dimer"), std::string::npos);
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, project, loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		EXPECT_EQ(loaded.defectRoots, settings.defectRoots);
		ASSERT_EQ(loaded.cases.size(), 2u);
		EXPECT_EQ(loaded.cases[0].sourceDirectory, settings.cases[0].sourceDirectory);
		EXPECT_TRUE(loaded.cases[0].hidden);
		EXPECT_TRUE(loaded.cases[1].sourceDirectory.Empty());
		EXPECT_FALSE(loaded.cases[1].hidden);
	}

	TEST(PhononDefectFolderSettingsTests, OldCasesKeepDefaultsAndBadOptionalKeysDoNotDiscardCases)
	{
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("formatVersion: 2\ncases: [{id: case-1, name: old}]", {}, loaded, warnings, error));
		ASSERT_EQ(loaded.cases.size(), 1u);
		EXPECT_TRUE(loaded.defectRoots.empty());
		EXPECT_TRUE(loaded.cases[0].sourceDirectory.Empty());
		EXPECT_FALSE(loaded.cases[0].hidden);
		EXPECT_TRUE(warnings.empty());
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("formatVersion: 2\ndefectRoots: bad\ncases: [{id: case-1, sourceDir: [], hidden: bad}]",
			{}, loaded, warnings, error));
		ASSERT_EQ(loaded.cases.size(), 1u);
		EXPECT_EQ(warnings.size(), 3u);
		EXPECT_TRUE(loaded.cases[0].sourceDirectory.Empty());
		EXPECT_FALSE(loaded.cases[0].hidden);
	}
}
