#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "Core/Utils/Uuid.hpp"
#include "ScientificRuntime/Python/PhononAnalysisBridge.hpp"
#include "ScientificRuntimeTestPaths.hpp"

namespace DefectStudio::Tests
{
	namespace
	{
		Path ReferenceCase()
		{
			return FindRepoRoot() / "test-directory" / "sjcm_mgr" / "PL-spectra" / "dimer" / "CH0" / "R2SCAN" / "9x9x1";
		}
	} // namespace

	TEST(PhononAnalysisBridgeTests, ArgumentsFollowTheScriptInterface)
	{
		PhononAnalysisRequest request;
		request.forceConstantDirectory = Path::FromUtf8("D:/calc/disp");
		request.groundStateDirectory = Path::FromUtf8("D:/calc/relax");
		request.excitedStateDirectory = Path::FromUtf8("D:/calc/es");
		request.sigmaMeV = 5.0;
		request.gammaMeV = 1.5;
		request.outputDirectory = Path::FromUtf8("D:/calc/out");
		const std::vector<std::string> minimal = BuildPhononAnalysisArguments(request);
		const std::vector<std::string> expectedMinimal = {
			"--fc-dir", request.forceConstantDirectory.Utf8(), "--gs-dir", request.groundStateDirectory.Utf8(),
			"--es-dir", request.excitedStateDirectory.Utf8(), "--sigma", "5", "--gamma", "1.5",
			"--out-dir", request.outputDirectory.Utf8()};
		EXPECT_EQ(minimal, expectedMinimal);

		request.tripletDirectory = Path::FromUtf8("D:/calc/triplet");
		request.experimentalZplEv = 4.08;
		const std::vector<std::string> full = BuildPhononAnalysisArguments(request);
		const std::vector<std::string> expectedFull = {
			"--fc-dir", request.forceConstantDirectory.Utf8(), "--gs-dir", request.groundStateDirectory.Utf8(),
			"--es-dir", request.excitedStateDirectory.Utf8(), "--triplet-dir", request.tripletDirectory->Utf8(),
			"--zpl-exp", "4.08", "--sigma", "5", "--gamma", "1.5", "--out-dir", request.outputDirectory.Utf8()};
		EXPECT_EQ(full, expectedFull);
	}

	TEST(PhononAnalysisBridgeTests, ParsesTheSuccessLineAfterLogNoise)
	{
		const Result<PhononAnalysisRunSummary> summary = ParsePhononAnalysisOutput(
			"some log line\n{\"ok\": true, \"out_dir\": \"D:/calc/out\", \"s_total\": 1.4066, \"stable\": false}\n");
		ASSERT_TRUE(summary) << summary.Error().userMessage;
		EXPECT_EQ(summary->outputDirectory.Utf8(), Path::FromUtf8("D:/calc/out").Utf8());
		EXPECT_NEAR(summary->sTotal, 1.4066, 1e-12);
		EXPECT_FALSE(summary->stable);
	}

	TEST(PhononAnalysisBridgeTests, ScriptErrorMessageReachesTheUser)
	{
		const Result<PhononAnalysisRunSummary> summary =
			ParsePhononAnalysisOutput("{\"ok\": false, \"error\": \"FC, ground and excited structures must have the same cell\"}\n");
		ASSERT_FALSE(summary);
		EXPECT_EQ(summary.Error().code, "python.phonon.script_failed");
		EXPECT_NE(summary.Error().userMessage.find("same cell"), std::string::npos);
	}

	TEST(PhononAnalysisBridgeTests, MissingOrMalformedJsonIsAnError)
	{
		for (const char *output : {"", "Traceback (most recent call last):\n", "{\"ok\": true}\n"})
		{
			const Result<PhononAnalysisRunSummary> summary = ParsePhononAnalysisOutput(output);
			ASSERT_FALSE(summary) << output;
			EXPECT_EQ(summary.Error().code, "python.phonon.invalid_json") << output;
		}
	}

	TEST(PhononAnalysisBridgeTests, MissingInputDirectoryIsRejectedBeforePythonStarts)
	{
		PhononAnalysisRequest request;
		request.forceConstantDirectory = ScientificRuntimeFixturePath("phonon/NO_SUCH_DIR");
		request.groundStateDirectory = request.forceConstantDirectory;
		request.excitedStateDirectory = request.forceConstantDirectory;
		request.outputDirectory = Path::FromResolved(std::filesystem::temp_directory_path() / "ds_phonon_never_written");
		const Result<PhononAnalysisRunSummary> summary = PhononAnalysisBridge{}.Run(request);
		ASSERT_FALSE(summary);
		EXPECT_EQ(summary.Error().code, "python.phonon.request_invalid");
		EXPECT_FALSE(std::filesystem::exists(std::filesystem::temp_directory_path() / "ds_phonon_never_written"));
	}

	TEST(PhononAnalysisBridgeTests, NonPositiveBroadeningIsRejected)
	{
		PhononAnalysisRequest request;
		request.forceConstantDirectory = FindRepoRoot();
		request.groundStateDirectory = FindRepoRoot();
		request.excitedStateDirectory = FindRepoRoot();
		request.outputDirectory = Path::FromResolved(std::filesystem::temp_directory_path() / "ds_phonon_never_written");
		request.gammaMeV = 0.0;
		const Result<PhononAnalysisRunSummary> summary = PhononAnalysisBridge{}.Run(request);
		ASSERT_FALSE(summary);
		EXPECT_EQ(summary.Error().code, "python.phonon.request_invalid");
	}

	// End to end through the app's Python; needs the gitignored reference data and dephonopy.
	TEST(PhononAnalysisBridgeTests, ReferenceCaseRunsEndToEnd)
	{
		const Path root = ReferenceCase();
		if (!FileSystem::Exists((root / "dispersion").Native()))
			GTEST_SKIP() << "reference data in test-directory/ not present";
		PhononAnalysisRequest request;
		request.forceConstantDirectory = root / "dispersion";
		request.groundStateDirectory = root / "relax";
		request.excitedStateDirectory = root / "excited" / "try_1_singlet";
		request.tripletDirectory = root / "excited" / "try_1_tryplet";
		request.experimentalZplEv = 4.08;
		request.outputDirectory =
			Path::FromResolved(std::filesystem::temp_directory_path() / ("ds_phonon_e2e_" + ToString(GenerateUuid())));
		const Result<PhononAnalysisRunSummary> summary = PhononAnalysisBridge{}.Run(request);
		if (!summary && summary.Error().code.find("not_installed") != std::string::npos)
			GTEST_SKIP() << summary.Error().userMessage;
		ASSERT_TRUE(summary) << summary.Error().userMessage << "\n" << summary.Error().technicalDetails;
		EXPECT_NEAR(summary->sTotal, 1.407, 0.01);
		EXPECT_TRUE(summary->stable);
		EXPECT_TRUE(FileSystem::Exists((request.outputDirectory / "summary.json").Native()));
		std::filesystem::remove_all(request.outputDirectory.Native());
	}
} // namespace DefectStudio::Tests
