#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "Core/Utils/Uuid.hpp"
#include "IO/PhononPanelSettingsIO.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		const Path kProject = Path::FromUtf8("D:/projekty/hbn");

		PhononPanelSettings Custom()
		{
			PhononPanelSettings settings;
			settings.forceConstantDirectory = kProject / "dimer" / "dispersion";
			settings.groundStateDirectory = kProject / "dimer" / "relax";
			settings.excitedStateDirectory = Path::FromUtf8("K:/sjcm_mgr/dimer/excited/try_1_singlet");
			settings.tripletDirectory = kProject / "dimer" / "triplet";
			settings.experimentalZplEv = 4.08;
			settings.sigmaMeV = 5.5;
			settings.gammaMeV = 1.25;
			settings.bulkForceConstantDirectory = kProject / "bulk" / "phonons";
			settings.embeddingMult = "24x24x1";
			settings.dcutoffAngstrom = 6.5;
			settings.ccdOutputDirectory = kProject / "ccd" / "dimer";
			settings.ccdStep = 0.125;
			settings.dispersionDirectory = kProject / "phonons" / "bulk_dispersion";
			settings.dispersionInPlane = false;
			settings.dispersionPoints = 51;
			settings.resultDirectory = kProject / "phonons" / "dimer";
			settings.selectedMode = 485;
			settings.participationThreshold = 0.025;
			settings.carriedFraction = 0.8;
			settings.arrowLengthAngstrom = 2.25;
			settings.showArrows = false;
			settings.arrowsFollowPhase = false;
			settings.seriesStyles["beta/A1"] = {glm::vec4(0.1f, 0.2f, 0.3f, 0.4f), 3.0f, false, true, 0.6f, 2};
			settings.seriesStyles["PL/PL"] = {};
			settings.arrowLowColor = glm::vec3(0.1f, 0.2f, 0.3f);
			settings.arrowHighColor = glm::vec3(0.9f, 0.8f, 0.7f);
			settings.animationAmplitudeAngstrom = 0.45;
			settings.animationPeriodSeconds = 2.5;
			settings.plZpl = "spin_purified";
			settings.irrepFilter = "A1";
			return settings;
		}
	} // namespace

	TEST(PhononPanelSettingsIOTests, RoundTripKeepsEverySetting)
	{
		const PhononPanelSettings original = Custom();
		const std::string text = PhononPanelSettingsIO::Serialize(original, kProject);
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, kProject, loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());

		EXPECT_EQ(loaded.forceConstantDirectory, original.forceConstantDirectory);
		EXPECT_EQ(loaded.groundStateDirectory, original.groundStateDirectory);
		EXPECT_EQ(loaded.excitedStateDirectory, original.excitedStateDirectory);
		EXPECT_EQ(loaded.tripletDirectory, original.tripletDirectory);
		ASSERT_TRUE(loaded.experimentalZplEv.has_value());
		EXPECT_DOUBLE_EQ(*loaded.experimentalZplEv, 4.08);
		EXPECT_DOUBLE_EQ(loaded.sigmaMeV, 5.5);
		EXPECT_DOUBLE_EQ(loaded.gammaMeV, 1.25);
		EXPECT_EQ(loaded.bulkForceConstantDirectory, original.bulkForceConstantDirectory);
		EXPECT_EQ(loaded.embeddingMult, "24x24x1");
		ASSERT_TRUE(loaded.dcutoffAngstrom.has_value());
		EXPECT_DOUBLE_EQ(*loaded.dcutoffAngstrom, 6.5);
		EXPECT_FALSE(loaded.bcutoffAngstrom.has_value());
		EXPECT_EQ(loaded.ccdOutputDirectory, original.ccdOutputDirectory);
		EXPECT_DOUBLE_EQ(loaded.ccdStep, 0.125);
		EXPECT_EQ(loaded.dispersionDirectory, original.dispersionDirectory);
		EXPECT_FALSE(loaded.dispersionInPlane);
		EXPECT_EQ(loaded.dispersionPoints, 51);
		EXPECT_EQ(loaded.resultDirectory, original.resultDirectory);
		EXPECT_EQ(loaded.selectedMode, 485);
		EXPECT_DOUBLE_EQ(loaded.participationThreshold, 0.025);
		EXPECT_DOUBLE_EQ(loaded.carriedFraction, 0.8);
		EXPECT_DOUBLE_EQ(loaded.arrowLengthAngstrom, 2.25);
		EXPECT_FALSE(loaded.showArrows);
		EXPECT_FALSE(loaded.arrowsFollowPhase);
		ASSERT_EQ(loaded.seriesStyles.size(), 2u);
		const auto &style = loaded.seriesStyles.at("beta/A1");
		ASSERT_TRUE(style.color);
		for (int i = 0; i < 4; ++i) EXPECT_FLOAT_EQ((*style.color)[i], (*original.seriesStyles.at("beta/A1").color)[i]);
		EXPECT_FLOAT_EQ(style.lineWeight, 3.0f);
		EXPECT_FALSE(style.visible);
		EXPECT_TRUE(style.filled);
		EXPECT_FLOAT_EQ(style.fillAlpha, 0.6f);
		EXPECT_EQ(style.lineStyle, 2);
		EXPECT_FALSE(loaded.seriesStyles.at("PL/PL").color);
		EXPECT_FLOAT_EQ(loaded.arrowLowColor.y, 0.2f);
		EXPECT_FLOAT_EQ(loaded.arrowHighColor.z, 0.7f);
		EXPECT_DOUBLE_EQ(loaded.animationAmplitudeAngstrom, 0.45);
		EXPECT_DOUBLE_EQ(loaded.animationPeriodSeconds, 2.5);
		EXPECT_EQ(loaded.plZpl, "spin_purified");
		EXPECT_EQ(loaded.irrepFilter, "A1");
	}

	TEST(PhononPanelSettingsIOTests, PathsInsideTheProjectAreWrittenRelative)
	{
		const std::string text = PhononPanelSettingsIO::Serialize(Custom(), kProject);
		EXPECT_NE(text.find("dimer/dispersion"), std::string::npos);
		EXPECT_EQ(text.find("D:/projekty/hbn/dimer/dispersion"), std::string::npos);
		EXPECT_NE(text.find("K:/sjcm_mgr/dimer/excited/try_1_singlet"), std::string::npos);
	}

	TEST(PhononPanelSettingsIOTests, NoExperimentalZplStaysEmpty)
	{
		PhononPanelSettings settings = Custom();
		settings.experimentalZplEv.reset();
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(PhononPanelSettingsIO::Serialize(settings, kProject), kProject, loaded,
			warnings, error));
		EXPECT_FALSE(loaded.experimentalZplEv.has_value());
	}

	TEST(PhononPanelSettingsIOTests, EmptyTextGivesDefaults)
	{
		PhononPanelSettings loaded = Custom();
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("  \n", kProject, loaded, warnings, error)) << error;
		const PhononPanelSettings defaults;
		EXPECT_TRUE(loaded.resultDirectory.Empty());
		EXPECT_EQ(loaded.selectedMode, defaults.selectedMode);
		EXPECT_DOUBLE_EQ(loaded.participationThreshold, defaults.participationThreshold);
		EXPECT_EQ(loaded.plZpl, defaults.plZpl);
	}

	TEST(PhononPanelSettingsIOTests, ABadValueKeepsItsDefaultWithAWarning)
	{
		const std::string text = "formatVersion: 1\nview:\n  participationThreshold: lots\n  selectedMode: 7\n";
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, kProject, loaded, warnings, error)) << error;
		EXPECT_DOUBLE_EQ(loaded.participationThreshold, PhononPanelSettings{}.participationThreshold);
		EXPECT_EQ(loaded.selectedMode, 7);
		EXPECT_FALSE(warnings.empty());
	}

	TEST(PhononPanelSettingsIOTests, NewerFormatVersionIsRejected)
	{
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		EXPECT_FALSE(PhononPanelSettingsIO::Parse("formatVersion: 2\n", kProject, loaded, warnings, error));
		EXPECT_FALSE(error.empty());
	}

	TEST(PhononPanelSettingsIOTests, OldFilesUseDispersionDefaults)
	{
		PhononPanelSettings loaded = Custom();
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("formatVersion: 1\ninputs: {fcDir: bulk}\n", kProject, loaded, warnings, error));
		EXPECT_TRUE(loaded.dispersionDirectory.Empty());
		EXPECT_TRUE(loaded.dispersionInPlane);
		EXPECT_EQ(loaded.dispersionPoints, 101);
		EXPECT_TRUE(loaded.arrowsFollowPhase);
		EXPECT_TRUE(loaded.seriesStyles.empty());
		EXPECT_TRUE(warnings.empty());
	}

	TEST(PhononPanelSettingsIOTests, BadDispersionSettingsKeepDefaults)
	{
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("inputs: {dispersionPoints: 1, dispersionInPlane: wrong}\n",
			kProject, loaded, warnings, error));
		EXPECT_EQ(loaded.dispersionPoints, 101);
		EXPECT_TRUE(loaded.dispersionInPlane);
		EXPECT_EQ(warnings.size(), 2u);
	}

	TEST(PhononPanelSettingsIOTests, BadSeriesFieldsKeepDefaultsAndOtherSeriesSurvive)
	{
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(R"(
arrowsFollowPhase: wrong
seriesStyles:
  beta/A1: {color: [1, 0, .nan, 1], weight: 0, visible: wrong, filled: true, fillAlpha: 2, lineStyle: 3}
  S/other: {color: null, weight: 6, lineStyle: 1}
  broken: []
)", kProject, loaded, warnings, error)) << error;
		EXPECT_TRUE(loaded.arrowsFollowPhase);
		const auto &style = loaded.seriesStyles.at("beta/A1");
		EXPECT_FALSE(style.color);
		EXPECT_FLOAT_EQ(style.lineWeight, 1.5f);
		EXPECT_TRUE(style.visible);
		EXPECT_TRUE(style.filled);
		EXPECT_FLOAT_EQ(style.fillAlpha, 0.25f);
		EXPECT_EQ(style.lineStyle, 0);
		EXPECT_FLOAT_EQ(loaded.seriesStyles.at("S/other").lineWeight, 6.0f);
		EXPECT_EQ(loaded.seriesStyles.at("S/other").lineStyle, 1);
		EXPECT_FALSE(loaded.seriesStyles.contains("broken"));
		EXPECT_EQ(warnings.size(), 7u);
	}

	TEST(PhononPanelSettingsIOTests, NonMapSeriesStylesWarnWithoutDiscardingSettings)
	{
		for (const char *text : {"seriesStyles: []", "seriesStyles: null"})
		{
			PhononPanelSettings loaded;
			std::vector<std::string> warnings;
			std::string error;
			ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, kProject, loaded, warnings, error));
			EXPECT_TRUE(loaded.seriesStyles.empty());
			EXPECT_EQ(warnings.size(), 1u);
		}
	}

	TEST(PhononPanelSettingsIOTests, SaveThenLoadFromDisk)
	{
		const Path project =
			Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_phonon_settings_" + ToString(GenerateUuid())));
		std::filesystem::create_directories(project.Native());
		PhononPanelSettings settings;
		settings.resultDirectory = project / "phonons";
		settings.selectedMode = 12;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Save(project, settings, error)) << error;
		EXPECT_TRUE(FileSystem::Exists(PhononPanelSettingsIO::FilePath(project).Native()));

		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		ASSERT_TRUE(PhononPanelSettingsIO::Load(project, loaded, warnings, error)) << error;
		EXPECT_EQ(loaded.selectedMode, 12);
		EXPECT_EQ(loaded.resultDirectory, settings.resultDirectory);
		std::filesystem::remove_all(project.Native());
	}

	TEST(PhononPanelSettingsIOTests, MissingFileGivesDefaults)
	{
		const Path project =
			Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_phonon_settings_none_" + ToString(GenerateUuid())));
		PhononPanelSettings loaded = Custom();
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Load(project, loaded, warnings, error)) << error;
		EXPECT_TRUE(loaded.resultDirectory.Empty());
	}
} // namespace DefectStudio::Tests
