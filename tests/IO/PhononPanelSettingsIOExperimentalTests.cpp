#include <gtest/gtest.h>

#include <algorithm>
#include "IO/PhononPanelSettingsIO.hpp"

namespace DefectStudio::Tests
{
	TEST(PhononPanelSettingsIOExperimentalTests, MultipleSpectraRoundTripWithRelativeAndExternalPaths)
	{
		const auto project = Path::FromUtf8("C:/project");
		PhononPanelSettings settings, loaded;
		settings.activeCaseId = "case-1";
		settings.cases.push_back({"case-1", "A", {}, {}});
		settings.cases.push_back({"case-2", "B", {}, {}});
		settings.cases[0].experimentalSpectra = {{project / "measurements/pl.txt", 2, 0, "eV", "PL A", 2.1, false},
			{Path::FromUtf8("D:/measurements/pl.csv"), 0, 1, "eV", "PL B", std::nullopt, true}};
		settings.cases[1].experimentalSpectra = {{project / "pl.tsv"}};
		settings.seriesStyles["pl/exp/case-1/0"].lineStyle = 2;
		const auto text = PhononPanelSettingsIO::Serialize(settings, project);
		EXPECT_NE(text.find("measurements/pl.txt"), std::string::npos);
		EXPECT_EQ(text.find("C:/project/measurements"), std::string::npos);
		EXPECT_NE(text.find("D:/measurements/pl.csv"), std::string::npos);
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, project, loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.cases.size(), 2u);
		const auto &spectra = loaded.cases[0].experimentalSpectra;
		ASSERT_EQ(spectra.size(), 2u);
		EXPECT_EQ(spectra[0].file, settings.cases[0].experimentalSpectra[0].file);
		EXPECT_EQ(spectra[0].xColumn, 2); EXPECT_EQ(spectra[0].yColumn, 0);
		EXPECT_EQ(spectra[0].unit, "eV"); EXPECT_EQ(spectra[0].label, "PL A");
		EXPECT_EQ(spectra[0].zplEv, 2.1); EXPECT_FALSE(spectra[0].visible);
		EXPECT_EQ(spectra[1].file, settings.cases[0].experimentalSpectra[1].file);
		EXPECT_FALSE(spectra[1].zplEv); EXPECT_TRUE(spectra[1].visible);
		ASSERT_EQ(loaded.cases[1].experimentalSpectra.size(), 1u);
		EXPECT_EQ(loaded.cases[1].experimentalSpectra[0].xColumn, 0);
		EXPECT_EQ(loaded.seriesStyles.at("pl/exp/case-1/0").lineStyle, 2);
		EXPECT_EQ(PhononPanelSettingsIO::kFormatVersion, 2);
	}

	TEST(PhononPanelSettingsIOExperimentalTests, UnknownUnitsAndMalformedEntriesWarnAndSkipOnlyThatEntry)
	{
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(R"(formatVersion: 2
cases:
  - id: case-1
    experimental:
      - {file: a.txt, unit: nm}
      - {file: b.txt, x: -1}
      - {file: c.txt, zpl: .nan}
      - {file: d.txt}
)", Path::FromUtf8("C:/project"), loaded, warnings, error)) << error;
		ASSERT_EQ(loaded.cases.size(), 1u);
		ASSERT_EQ(loaded.cases[0].experimentalSpectra.size(), 1u);
		EXPECT_EQ(loaded.cases[0].experimentalSpectra[0].label, "d.txt");
		EXPECT_EQ(warnings.size(), 3u);
		EXPECT_TRUE(std::any_of(warnings.begin(), warnings.end(), [](const auto &warning) { return warning.find("unknown unit: nm") != std::string::npos; }));
		EXPECT_EQ(ExperimentalSpectrumUnit("eV"), SpectrumAxisUnit::ElectronVolt);
		EXPECT_FALSE(ExperimentalSpectrumUnit("cm-1"));
	}

	TEST(PhononPanelSettingsIOExperimentalTests, OldSettingsKeepEmptyExperimentalLists)
	{
		for (const auto text : {"formatVersion: 1\ninputs: {gsDir: gs}", "formatVersion: 2\ncases: [{id: case-1}]"})
		{
			PhononPanelSettings loaded;
			std::vector<std::string> warnings;
			std::string error;
			ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, Path::FromUtf8("C:/project"), loaded, warnings, error));
			ASSERT_EQ(loaded.cases.size(), 1u);
			EXPECT_TRUE(loaded.cases[0].experimentalSpectra.empty());
			EXPECT_TRUE(warnings.empty());
		}
	}
}
