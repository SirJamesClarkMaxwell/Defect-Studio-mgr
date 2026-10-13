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
			settings.zplFormula = "4.08";
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
		EXPECT_EQ(loaded.zplFormula, "4.08");
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
		settings.zplFormula.clear();
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(PhononPanelSettingsIO::Serialize(settings, kProject), kProject, loaded,
			warnings, error));
		EXPECT_TRUE(loaded.zplFormula.empty());
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
		EXPECT_FALSE(PhononPanelSettingsIO::Parse("formatVersion: 3\n", kProject, loaded, warnings, error));
		EXPECT_FALSE(error.empty());
	}

	TEST(PhononPanelSettingsIOTests, VersionOneMigratesInputsResultsAndCcdIntoOneCase)
	{
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(R"(
formatVersion: 1
inputs: {fcDir: dimer/fc, gsDir: dimer/gs, esDir: dimer/es, zplFormula: "E_es - E_gs", sigma: 7, gamma: 3}
resultDir: defectstudio/phonons
datasets: {ccd: defectstudio/ccd, compared: [other/phonons]}
view: {selectedMode: 4, plZpl: formula, irrepFilter: A1, minHuangRhys: 0.2}
)", kProject, loaded, warnings, error)) << error;
		ASSERT_EQ(loaded.cases.size(), 1u);
		EXPECT_TRUE(warnings.empty());
		EXPECT_EQ(loaded.activeCaseId, "case-1");
		EXPECT_EQ(loaded.nextCaseNumber, 2);
		const auto &item = loaded.cases.front();
		EXPECT_EQ(item.name, "dimer/gs");
		EXPECT_EQ(item.settings.forceConstantDirectory, kProject / "dimer/fc");
		EXPECT_EQ(item.settings.groundStateDirectory, kProject / "dimer/gs");
		EXPECT_EQ(item.settings.excitedStateDirectory, kProject / "dimer/es");
		EXPECT_EQ(item.settings.zplFormula, "E_es - E_gs");
		EXPECT_DOUBLE_EQ(item.settings.sigmaMeV, 7.0);
		EXPECT_DOUBLE_EQ(item.settings.gammaMeV, 3.0);
		EXPECT_EQ(item.settings.selectedMode, 4);
		EXPECT_EQ(item.settings.plZpl, "formula");
		EXPECT_EQ(item.settings.irrepFilter, "A1");
		EXPECT_DOUBLE_EQ(item.settings.minHuangRhys, 0.2);
		EXPECT_EQ(item.datasets, (std::vector<Path>{kProject / "defectstudio/phonons", kProject / "defectstudio/ccd"}));
		EXPECT_EQ(loaded.resultDirectory, item.settings.resultDirectory);
		EXPECT_EQ(loaded.comparedDirectories, (std::vector<Path>{kProject / "other/phonons"}));
	}

	TEST(PhononPanelSettingsIOTests, TwoCasesRoundTripUsesTheActiveBaseAndKeepsGlobals)
	{
		auto original = Custom();
		original.ccdDirectory = kProject / "a/points";
		original.ccdAnalysisOutputDirectory = kProject / "a/ccd-output";
		original.ccdTemperatureK = 120.0;
		original.ccdPhononCurvature = true;
		original.selectedCcdDirectory = kProject / "a/ccd-result";
		original.bcutoffAngstrom = 8.0;
		original.minHuangRhys = 0.05;
		original.cases = {{"case-1", "A", {original.resultDirectory, original.selectedCcdDirectory}, original},
			{"case-2", "B", {kProject / "b/phonons"}, {}}};
		original.activeCaseId = "case-2";
		original.nextCaseNumber = 9;
		original.groundStateDirectory = kProject / "b/gs";
		original.resultDirectory = kProject / "b/phonons";
		original.selectedMode = 12;
		original.irrepFilter = "E";
		original.plZpl = "formula";
		original.comparedDirectories = {kProject / "a/phonons"};
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		const auto text = PhononPanelSettingsIO::Serialize(original, kProject);
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, kProject, loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		ASSERT_EQ(loaded.cases.size(), 2u);
		EXPECT_EQ(loaded.activeCaseId, "case-2");
		EXPECT_EQ(loaded.nextCaseNumber, 9);
		EXPECT_EQ(loaded.cases[0].id, "case-1");
		EXPECT_EQ(loaded.cases[0].name, "A");
		EXPECT_EQ(loaded.cases[0].datasets, original.cases[0].datasets);
		EXPECT_EQ(loaded.cases[0].settings.groundStateDirectory, original.cases[0].settings.groundStateDirectory);
		EXPECT_EQ(loaded.cases[0].settings.ccdDirectory, original.ccdDirectory);
		EXPECT_EQ(loaded.cases[0].settings.ccdAnalysisOutputDirectory, original.ccdAnalysisOutputDirectory);
		EXPECT_DOUBLE_EQ(loaded.cases[0].settings.ccdTemperatureK, 120.0);
		EXPECT_TRUE(loaded.cases[0].settings.ccdPhononCurvature);
		EXPECT_EQ(loaded.cases[0].settings.selectedCcdDirectory, original.selectedCcdDirectory);
		EXPECT_EQ(loaded.cases[0].settings.bcutoffAngstrom, original.bcutoffAngstrom);
		EXPECT_EQ(loaded.cases[1].settings.groundStateDirectory, original.groundStateDirectory);
		EXPECT_EQ(loaded.cases[1].settings.selectedMode, 12);
		EXPECT_EQ(loaded.cases[1].settings.irrepFilter, "E");
		EXPECT_EQ(loaded.groundStateDirectory, original.groundStateDirectory);
		EXPECT_EQ(loaded.resultDirectory, original.resultDirectory);
		EXPECT_EQ(loaded.plZpl, "formula");
		EXPECT_EQ(loaded.comparedDirectories, original.comparedDirectories);
		EXPECT_EQ(loaded.seriesStyles.size(), original.seriesStyles.size());
		EXPECT_EQ(loaded.arrowLowColor, original.arrowLowColor);
		EXPECT_EQ(loaded.dispersionDirectory, original.dispersionDirectory);
		EXPECT_FALSE(loaded.dispersionInPlane);
		EXPECT_EQ(loaded.dispersionPoints, 51);
	}

	TEST(PhononPanelSettingsIOTests, EmptyProjectAndMissingHighWaterMark)
	{
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		for (const auto &text : {std::string("formatVersion: 1\n"), PhononPanelSettingsIO::Serialize({}, kProject)})
		{
			ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, kProject, loaded, warnings, error));
			EXPECT_TRUE(loaded.cases.empty());
			EXPECT_TRUE(loaded.activeCaseId.empty());
		}
		const std::string casesText = R"(
cases:
  - {id: case-4, name: A}
  - {id: case-8, name: B}
activeCase: case-8
)";
		for (const auto &prefix : {"formatVersion: 2\n", ""})
		{
			ASSERT_TRUE(PhononPanelSettingsIO::Parse(prefix + casesText, kProject, loaded, warnings, error)) << error;
			EXPECT_EQ(loaded.activeCaseId, "case-8");
			EXPECT_EQ(loaded.nextCaseNumber, 9);
			EXPECT_TRUE(warnings.empty());
		}
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

	TEST(PhononPanelSettingsIOTests, FormulaAndDatasetsRoundTripWithRelativeAndAbsolutePaths)
	{
		PhononPanelSettings settings;
		settings.zplFormula = "2*E_es - E_T - E_gs";
		settings.groundAtExcitedDirectory = kProject / "gs-at-es";
		settings.excitedAtGroundDirectory = Path::FromUtf8("K:/es-at-gs");
		settings.outputDirectory = kProject / "defectstudio/phonons";
		settings.comparedDirectories = {kProject / "a/defectstudio/phonons", Path::FromUtf8("K:/b/defectstudio/phonons")};
		settings.selectedDispersionDirectory = kProject / "bulk/defectstudio/dispersion";
		settings.plZpl = "formula";
		const auto text = PhononPanelSettingsIO::Serialize(settings, kProject);
		EXPECT_EQ(text.find("D:/projekty/hbn"), std::string::npos);
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, kProject, loaded, warnings, error)) << error;
		EXPECT_TRUE(warnings.empty());
		EXPECT_EQ(loaded.zplFormula, settings.zplFormula);
		EXPECT_EQ(loaded.groundAtExcitedDirectory, settings.groundAtExcitedDirectory);
		EXPECT_EQ(loaded.excitedAtGroundDirectory, settings.excitedAtGroundDirectory);
		EXPECT_EQ(loaded.outputDirectory, settings.outputDirectory);
		EXPECT_EQ(loaded.comparedDirectories, settings.comparedDirectories);
		EXPECT_EQ(loaded.selectedDispersionDirectory, settings.selectedDispersionDirectory);
		EXPECT_EQ(loaded.plZpl, "formula");
	}

	TEST(PhononPanelSettingsIOTests, LegacyExperimentalZplBecomesFormulaAndNewFormulaWins)
	{
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("inputs: {zplExp: 1.945}\n", kProject, loaded, warnings, error));
		EXPECT_EQ(loaded.zplFormula, "1.945");
		EXPECT_TRUE(warnings.empty());
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("inputs: {zplExp: 1.945, zplFormula: E_es - E_gs}\n", kProject, loaded, warnings, error));
		EXPECT_EQ(loaded.zplFormula, "E_es - E_gs");
	}

	TEST(PhononPanelSettingsIOTests, MinimumHuangRhysRoundTripsAndLegacyFilesDefaultToZero)
	{
		PhononPanelSettings settings, loaded;
		settings.minHuangRhys = 0.0123;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(PhononPanelSettingsIO::Serialize(settings, kProject),
			kProject, loaded, warnings, error));
		EXPECT_DOUBLE_EQ(loaded.minHuangRhys, 0.0123);
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("view: {selectedMode: 3}", kProject, loaded, warnings, error));
		EXPECT_DOUBLE_EQ(loaded.minHuangRhys, 0.0);
		for (const char *value : {"-1", ".nan", "wrong"})
		{
			ASSERT_TRUE(PhononPanelSettingsIO::Parse(std::string("view: {minHuangRhys: ") + value + "}",
				kProject, loaded, warnings, error));
			EXPECT_DOUBLE_EQ(loaded.minHuangRhys, 0.0);
			EXPECT_EQ(warnings.size(), 1u);
		}
	}

	TEST(PhononPanelSettingsIOTests, CcdSelectionInputsAndViewRoundTripAndOldFilesLoadDefaults)
	{
		PhononPanelSettings settings, loaded;
		settings.selectedCcdDirectory = kProject / "defectstudio/ccd";
		settings.ccdDirectory = kProject / "ccd-path";
		settings.ccdAnalysisOutputDirectory = Path::FromUtf8("K:/CCD result");
		settings.ccdTemperatureK = 125.5;
		settings.ccdPhononCurvature = true;
		settings.ccdLevels = 8;
		settings.ccdWavefunctions = true;
		settings.ccdProbability = true;
		settings.ccdWavefunctionScale = 0.12;
		settings.ccdArrows = false;
		settings.ccdComparePhononPl = true;
		std::vector<std::string> warnings;
		std::string error;
		const auto text = PhononPanelSettingsIO::Serialize(settings, kProject);
		EXPECT_EQ(text.find("D:/projekty/hbn"), std::string::npos);
		ASSERT_TRUE(PhononPanelSettingsIO::Parse(text, kProject, loaded, warnings, error));
		EXPECT_TRUE(warnings.empty());
		EXPECT_EQ(loaded.selectedCcdDirectory, settings.selectedCcdDirectory);
		EXPECT_EQ(loaded.ccdDirectory, settings.ccdDirectory);
		EXPECT_EQ(loaded.ccdAnalysisOutputDirectory, settings.ccdAnalysisOutputDirectory);
		EXPECT_DOUBLE_EQ(loaded.ccdTemperatureK, 125.5);
		EXPECT_TRUE(loaded.ccdPhononCurvature);
		EXPECT_EQ(loaded.ccdLevels, 8);
		EXPECT_TRUE(loaded.ccdWavefunctions);
		EXPECT_TRUE(loaded.ccdProbability);
		EXPECT_DOUBLE_EQ(loaded.ccdWavefunctionScale, 0.12);
		EXPECT_FALSE(loaded.ccdArrows);
		EXPECT_TRUE(loaded.ccdComparePhononPl);
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("formatVersion: 1", kProject, loaded, warnings, error));
		EXPECT_TRUE(loaded.selectedCcdDirectory.Empty());
		EXPECT_TRUE(loaded.ccdDirectory.Empty());
		EXPECT_FALSE(loaded.ccdPhononCurvature);
		EXPECT_DOUBLE_EQ(loaded.ccdTemperatureK, 0);
		EXPECT_EQ(loaded.ccdLevels, PhononPanelSettings{}.ccdLevels);
	}

	TEST(PhononPanelSettingsIOTests, InvalidCcdControlsKeepDefaultsWithWarnings)
	{
		PhononPanelSettings loaded;
		std::vector<std::string> warnings;
		std::string error;
		ASSERT_TRUE(PhononPanelSettingsIO::Parse("inputs: {ccdTemperature: -1, ccdPhononCurvature: wrong}\n"
			"view: {ccdLevels: 9, ccdWavefunctionScale: .nan, ccdProbability: wrong}", kProject, loaded, warnings, error));
		EXPECT_EQ(warnings.size(), 5u);
		EXPECT_DOUBLE_EQ(loaded.ccdTemperatureK, 0);
		EXPECT_EQ(loaded.ccdLevels, 3);
		EXPECT_DOUBLE_EQ(loaded.ccdWavefunctionScale, 0.05);
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
